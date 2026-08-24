#include "eval_common.h"

#include <zstd.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

#ifndef SPH_EVAL_LEVEL
#error "SPH_EVAL_LEVEL must be defined"
#endif

namespace
{
    auto compress(std::span<std::uint8_t const> input) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> output(ZSTD_compressBound(input.size()));
        auto const size{ZSTD_compress(output.data(), output.size(), input.data(), input.size(), SPH_EVAL_LEVEL)};
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
            throw std::invalid_argument{"usage: reference_compress_worker input output iterations metrics"};
        }
        auto const input{sph::zstd::eval::read_binary(argv[1])};
        auto const iterations{sph::zstd::eval::parse_iterations(argv[3])};
        auto output{compress(input)};
        auto const start{sph::zstd::eval::clock::now()};
        for (std::uint64_t iteration{}; iteration < iterations; ++iteration)
        {
            output = compress(input);
        }
        auto const end{sph::zstd::eval::clock::now()};
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
