#include "eval_common.h"

#include <sph/zstd++/zstd_compress.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <atomic>
#include <cstdlib>
#include <cstdio>
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
    std::atomic<std::size_t> allocation_count{};
    std::atomic<bool> report_allocations{};
}

void* operator new(std::size_t size)
{
    if (auto* memory = std::malloc(size == 0U ? 1U : size))
    {
        allocation_count.fetch_add(1U, std::memory_order_relaxed);
        if (report_allocations.load(std::memory_order_relaxed))
            std::fprintf(stderr, "post-warmup allocation size=%zu\n", size);
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
        auto const ordinary_allocations_after_warmup{allocation_count.load(std::memory_order_relaxed)};
        report_allocations.store(true, std::memory_order_relaxed);
        auto const start{sph::zstd::eval::clock::now()};
        for (std::uint64_t iteration{}; iteration < iterations; ++iteration)
        {
            compress();
        }
        auto const end{sph::zstd::eval::clock::now()};
        report_allocations.store(false, std::memory_order_relaxed);
        if (sph::zstd::detail::cache_aligned_allocation_count != allocations_after_warmup ||
            sph::zstd::detail::cache_aligned_deallocation_count != deallocations_after_warmup ||
            allocation_count.load(std::memory_order_relaxed) != ordinary_allocations_after_warmup)
        {
            std::cerr << "allocation delta ordinary="
                      << (allocation_count.load(std::memory_order_relaxed) - ordinary_allocations_after_warmup)
                      << " aligned=" << (sph::zstd::detail::cache_aligned_allocation_count - allocations_after_warmup)
                      << " aligned-free="
                      << (sph::zstd::detail::cache_aligned_deallocation_count - deallocations_after_warmup)
                      << '\n';
            throw std::runtime_error{"ordinary or cache-aligned allocation changed after warmup"};
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
        auto const& entropy{sph::zstd::detail::entropy_trace};
        std::cerr << "entropy phases gather_ns=" << entropy.gather_ns
                  << " literals_ns=" << entropy.literals_ns
                  << " tables_ns=" << entropy.tables_ns << " bits_ns=" << entropy.bits_ns
                  << " blocks=" << entropy.blocks << " sequences=" << entropy.sequences
                  << " literal_bytes=" << entropy.literal_bytes << '\n';
        for (unsigned table{}; table < 3U; ++table)
        {
            std::cerr << "sequence table " << table
                      << " select_ns=" << entropy.sequence_table_select_ns[table];
            for (unsigned mode{}; mode < 4U; ++mode)
                std::cerr << " mode" << mode << '=' << entropy.sequence_table_mode_counts[table][mode];
            std::cerr << '\n';
        }
        std::cerr << "sequence table phases histogram_ns=" << entropy.sequence_histogram_ns
                  << " normalize_ns=" << entropy.sequence_normalize_ns
                  << " build_ns=" << entropy.sequence_build_ns
                  << " history_copy_ns=" << entropy.sequence_history_copy_ns << '\n';
        std::cerr << "Huffman phases histogram_ns=" << entropy.huffman_histogram_ns
                  << " tree_ns=" << entropy.huffman_tree_ns
                  << " weights_ns=" << entropy.huffman_weights_ns
                  << " selection_ns=" << entropy.huffman_selection_ns
                  << " streams_ns=" << entropy.huffman_streams_ns
                  << " emit_ns=" << entropy.huffman_emit_ns
                  << " codeprep_ns=" << entropy.huffman_codeprep_ns
                  << " new_codeprep_ns=" << entropy.huffman_new_codeprep_ns
                  << " repeat_codeprep_ns=" << entropy.huffman_repeat_codeprep_ns
                  << " body_ns=" << entropy.huffman_body_ns
                  << " new_blocks=" << entropy.huffman_new_blocks
                  << " repeat_blocks=" << entropy.huffman_repeat_blocks
                  << " discarded_weights=" << entropy.huffman_discarded_weights
                  << " histogram_reject_blocks=" << entropy.huffman_histogram_reject_blocks
                  << " histogram_reject_bytes=" << entropy.huffman_histogram_reject_bytes
                  << " sample_eligible_blocks=" << entropy.huffman_sample_eligible_blocks << '\n';
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
