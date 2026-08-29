#include "eval_common.h"

#include <sph/zstd++/zstd_decompress.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

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
        std::vector<std::uint8_t> output;
        output.reserve(expected.size());
        auto decompressor = sph::zstd::zstd_decompress{
            [&output](std::span<std::uint8_t const> bytes)
            {
                output.insert(output.end(), bytes.begin(), bytes.end());
            }};
        auto const decompress = [&]
        {
            output.clear();
            decompressor.reset();
            decompressor.update(encoded);
            decompressor.finish();
        };
        decompress();
        if (output != expected)
        {
            throw std::runtime_error{"sph decompression output mismatch"};
        }
        auto const start{sph::zstd::eval::clock::now()};
        for (std::uint64_t iteration{}; iteration < iterations; ++iteration)
        {
            decompress();
        }
        auto const end{sph::zstd::eval::clock::now()};
        if (output != expected)
        {
            throw std::runtime_error{"sph decompression output mismatch"};
        }
        sph::zstd::eval::write_result(argv[4], {
            .elapsed_nanoseconds = sph::zstd::eval::elapsed_nanoseconds(start, end),
            .peak_resident_bytes = sph::zstd::eval::peak_resident_bytes(),
            .input_bytes = encoded.size(),
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
