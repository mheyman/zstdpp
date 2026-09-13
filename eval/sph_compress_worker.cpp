#include "eval_common.h"

#include <sph/zstd++/zstd_compress.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <span>
#include <vector>

#ifndef SPH_EVAL_LEVEL
#error "SPH_EVAL_LEVEL must be defined"
#endif

#ifndef SPH_EVAL_CORPUS_SIZE
#error "SPH_EVAL_CORPUS_SIZE must be defined"
#endif

namespace
{
    std::atomic<bool> track_allocations{true};
    std::atomic<std::size_t> allocation_count{};
}

void* operator new(std::size_t size)
{
    if (auto* memory = std::malloc(size == 0U ? 1U : size))
    {
        if (track_allocations.load(std::memory_order_relaxed))
            allocation_count.fetch_add(1U, std::memory_order_relaxed);
        return memory;
    }
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace
{
    constexpr auto parameters = []
    {
        auto value = sph::zstd::compression_parameters{};
        value.compression_level = SPH_EVAL_LEVEL;
        value.pledged_source_size = SPH_EVAL_CORPUS_SIZE;
        return value;
    }();
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 5)
        {
            throw std::invalid_argument{"usage: sph_compress_worker input output iterations metrics"};
        }
        auto const input{sph::zstd::eval::read_binary(argv[1])};
        if (input.size() != SPH_EVAL_CORPUS_SIZE)
        {
            throw std::invalid_argument{"input size does not match compile-time corpus size"};
        }
        auto const iterations{sph::zstd::eval::parse_iterations(argv[3])};
        std::vector<std::uint8_t> output;
        output.reserve(input.size() + input.size() / 128U + 64U);
        auto compressor = sph::zstd::make_zstd_compress<parameters>(
            [&output](std::span<std::uint8_t const> bytes)
            {
                output.insert(output.end(), bytes.begin(), bytes.end());
            });
        auto const compress = [&]
        {
            output.clear();
            compressor.reset();
            compressor.compress_frame(input);
        };
        compress(); // Warm caches and allocate reusable workspace before timing.
        auto const allocations_after_warmup{sph::zstd::detail::cache_aligned_allocation_count};
        auto const deallocations_after_warmup{sph::zstd::detail::cache_aligned_deallocation_count};
        track_allocations.store(false, std::memory_order_relaxed);
        auto const ordinary_allocations_after_warmup{allocation_count.load(std::memory_order_relaxed)};
        auto const start{sph::zstd::eval::clock::now()};
        for (std::uint64_t iteration{}; iteration < iterations; ++iteration)
        {
            compress();
        }
        auto const end{sph::zstd::eval::clock::now()};
        if (sph::zstd::detail::cache_aligned_allocation_count != allocations_after_warmup ||
            sph::zstd::detail::cache_aligned_deallocation_count != deallocations_after_warmup ||
            allocation_count.load(std::memory_order_relaxed) != ordinary_allocations_after_warmup)
        {
            throw std::runtime_error{"cache-aligned allocation changed after warmup"};
        }
#ifdef SPH_ZSTDPP_TRACE_BT
        std::cerr << "binary-tree visits unsorted=" << sph::zstd::detail::binary_tree_trace.unsorted_visits
                  << " main=" << sph::zstd::detail::binary_tree_trace.main_visits
                  << " extensions=" << sph::zstd::detail::binary_tree_trace.match_extensions << '\n';
#endif
#ifdef SPH_ZSTDPP_TRACE_PHASES
        std::cerr << "compression phases parse_ns=" << sph::zstd::compression_trace.parse_ns
                  << " encode_ns=" << sph::zstd::compression_trace.encode_ns
                  << " emit_ns=" << sph::zstd::compression_trace.emit_ns << '\n';
#endif
        sph::zstd::eval::write_binary(argv[2], output);
        sph::zstd::eval::write_result(argv[4], {
            .elapsed_nanoseconds = sph::zstd::eval::elapsed_nanoseconds(start, end),
            .peak_resident_bytes = sph::zstd::eval::peak_resident_bytes(),
            .input_bytes = input.size(),
            .output_bytes = output.size(),
            .iterations = iterations
        });
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
