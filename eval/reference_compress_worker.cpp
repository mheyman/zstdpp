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
        std::vector<std::uint8_t> output(ZSTD_compressBound(input.size()));
        auto* const context{ZSTD_createCCtx()};
        if (context == nullptr)
        {
            throw std::runtime_error{"cannot allocate reference compression context"};
        }
        std::size_t output_size{};
        auto const compress = [&]
        {
            output_size = ZSTD_compressCCtx(context, output.data(), output.size(),
                input.data(), input.size(), SPH_EVAL_LEVEL);
            if (ZSTD_isError(output_size) != 0U)
            {
                throw std::runtime_error{ZSTD_getErrorName(output_size)};
            }
        };
        compress();
        auto const start{sph::zstd::eval::clock::now()};
        for (std::uint64_t iteration{}; iteration < iterations; ++iteration)
        {
            compress();
        }
        auto const end{sph::zstd::eval::clock::now()};
        ZSTD_freeCCtx(context);
        sph::zstd::eval::write_binary(argv[2],
            std::span<std::uint8_t const>{output}.first(output_size));
        sph::zstd::eval::write_result(argv[4], {
            .elapsed_nanoseconds = sph::zstd::eval::elapsed_nanoseconds(start, end),
            .peak_resident_bytes = sph::zstd::eval::peak_resident_bytes(),
            .input_bytes = input.size(),
            .output_bytes = output_size,
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
