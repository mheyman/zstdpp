#include "eval_common.h"

#include <zstd.h>

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
            throw std::invalid_argument{"usage: reference_decompress_worker encoded expected iterations metrics"};
        }
        auto const encoded{sph::zstd::eval::read_binary(argv[1])};
        auto const expected{sph::zstd::eval::read_binary(argv[2])};
        auto const iterations{sph::zstd::eval::parse_iterations(argv[3])};
        std::vector<std::uint8_t> output(expected.size());
        auto* const context{ZSTD_createDCtx()};
        if (context == nullptr)
        {
            throw std::runtime_error{"cannot allocate reference decompression context"};
        }
        std::size_t output_size{};
        auto const decompress = [&]
        {
            output_size = ZSTD_decompressDCtx(context, output.data(), output.size(),
                encoded.data(), encoded.size());
            if (ZSTD_isError(output_size) != 0U)
            {
                throw std::runtime_error{ZSTD_getErrorName(output_size)};
            }
        };
        decompress();
        if (output != expected)
        {
            throw std::runtime_error{"reference decompression output mismatch"};
        }
        auto const start{sph::zstd::eval::clock::now()};
        for (std::uint64_t iteration{}; iteration < iterations; ++iteration)
        {
            decompress();
        }
        auto const end{sph::zstd::eval::clock::now()};
        if (output_size != expected.size() || output != expected)
        {
            throw std::runtime_error{"reference decompression output mismatch"};
        }
        ZSTD_freeDCtx(context);
        sph::zstd::eval::write_result(argv[4], {
            .elapsed_nanoseconds = sph::zstd::eval::elapsed_nanoseconds(start, end),
            .peak_resident_bytes = sph::zstd::eval::peak_resident_bytes(),
            .input_bytes = encoded.size(),
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
