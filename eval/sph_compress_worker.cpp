#include "eval_common.h"

#include <sph/zstd++/zstd_compress.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
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
    constexpr auto parameters = []
    {
        auto value = sph::zstd::compression_parameters{};
        value.compression_level = SPH_EVAL_LEVEL;
        value.pledged_source_size = SPH_EVAL_CORPUS_SIZE;
        return value;
    }();

    auto compress(std::span<std::uint8_t const> input) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> output;
        output.reserve(input.size() + input.size() / 128U + 64U);
        auto compressor = sph::zstd::make_zstd_compress<parameters>(
            [&output](std::span<std::uint8_t const> bytes)
            {
                output.insert(output.end(), bytes.begin(), bytes.end());
            });
        compressor.update(input);
        compressor.finish();
        return output;
    }
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
        auto output{compress(input)}; // Warm caches and instantiate all lazy state before timing.
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
