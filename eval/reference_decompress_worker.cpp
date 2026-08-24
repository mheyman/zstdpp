#include "eval_common.h"

#include <zstd.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace
{
    auto decompress(std::span<std::uint8_t const> encoded, std::size_t expected_size)
        -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> output(expected_size);
        auto const size{ZSTD_decompress(output.data(), output.size(), encoded.data(), encoded.size())};
        if (ZSTD_isError(size) != 0U)
        {
            throw std::runtime_error{ZSTD_getErrorName(size)};
        }
        output.resize(size);
        return output;
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 5)
        {
            throw std::invalid_argument{"usage: reference_decompress_worker encoded expected iterations metrics"};
        }
        auto const encoded{sph::zstd::eval::read_binary(argv[1])};
        auto const expected{sph::zstd::eval::read_binary(argv[2])};
        auto const iterations{sph::zstd::eval::parse_iterations(argv[3])};
        auto output{decompress(encoded, expected.size())};
        if (output != expected)
        {
            throw std::runtime_error{"reference decompression output mismatch"};
        }
        auto const start{sph::zstd::eval::clock::now()};
        for (std::uint64_t iteration{}; iteration < iterations; ++iteration)
        {
            output = decompress(encoded, expected.size());
        }
        auto const end{sph::zstd::eval::clock::now()};
        if (output != expected)
        {
            throw std::runtime_error{"reference decompression output mismatch"};
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
