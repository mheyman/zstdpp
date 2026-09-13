#include "eval_common.h"

#include <sph/zstd++/zstd_decompress.h>

#include <cstdint>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <new>
#include <span>
#include <stdexcept>
#include <vector>

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

int main(int argc, char** argv)
{
    try
    {
        if (argc != 5)
        {
            throw std::invalid_argument{"usage: sph_decompress_worker encoded expected iterations metrics"};
        }
        auto const encoded{sph::zstd::eval::read_binary(argv[1])};
        auto const expected{sph::zstd::eval::read_binary(argv[2])};
        auto const iterations{sph::zstd::eval::parse_iterations(argv[3])};
        std::vector<std::uint8_t> output(expected.size());
        auto decompressor = sph::zstd::zstd_decompress{
            [](std::span<std::uint8_t const>) {}};
        auto const decompress = [&]
        {
            decompressor.reset();
            decompressor.decompress_frame(encoded, output);
        };
        decompress();
        if (output != expected)
        {
            throw std::runtime_error{"sph decompression output mismatch"};
        }
        track_allocations.store(false, std::memory_order_relaxed);
        auto const ordinary_allocations_after_warmup{allocation_count.load(std::memory_order_relaxed)};
        auto const start{sph::zstd::eval::clock::now()};
        for (std::uint64_t iteration{}; iteration < iterations; ++iteration)
        {
            decompress();
        }
        auto const end{sph::zstd::eval::clock::now()};
        if (decompressor.decoded_size() != expected.size())
        {
            throw std::runtime_error{"sph decompression output size mismatch"};
        }
        if (allocation_count.load(std::memory_order_relaxed) != ordinary_allocations_after_warmup)
        {
            throw std::runtime_error{"ordinary allocation changed after warmup"};
        }
        sph::zstd::eval::write_result(argv[4], {
            .elapsed_nanoseconds = sph::zstd::eval::elapsed_nanoseconds(start, end),
            .peak_resident_bytes = sph::zstd::eval::peak_resident_bytes(),
            .input_bytes = encoded.size(),
            .output_bytes = decompressor.decoded_size(),
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
