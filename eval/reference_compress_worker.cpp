#include "eval_common.h"

#include <zstd.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

#ifdef SPH_ZSTDPP_TRACE_REFERENCE_PHASES
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" unsigned long long sph_zstdpp_reference_parse_ticks;
extern "C" unsigned long long sph_zstdpp_reference_split_ticks;
extern "C" unsigned long long sph_zstdpp_reference_encode_ticks;
extern "C" unsigned long long sph_zstdpp_reference_literals_ticks;
extern "C" unsigned long long sph_zstdpp_reference_tables_ticks;
extern "C" unsigned long long sph_zstdpp_reference_bits_ticks;
extern "C" unsigned long long sph_zstdpp_reference_huf_hist_ticks;
extern "C" unsigned long long sph_zstdpp_reference_huf_tree_ticks;
extern "C" unsigned long long sph_zstdpp_reference_huf_weights_ticks;
extern "C" unsigned long long sph_zstdpp_reference_huf_stream_ticks;
#endif

#ifndef SPH_EVAL_LEVEL
#error "SPH_EVAL_LEVEL must be defined"
#endif

#ifdef SPH_ZSTDPP_TRACE_BT
extern "C" unsigned long long sph_zstdpp_bt_unsorted_visits;
extern "C" unsigned long long sph_zstdpp_bt_main_visits;
extern "C" unsigned long long sph_zstdpp_bt_match_extensions;
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
#ifdef SPH_ZSTDPP_TRACE_REFERENCE_PHASES
#ifdef _WIN32
        LARGE_INTEGER clock_frequency{};
        if (!QueryPerformanceFrequency(&clock_frequency))
            throw std::runtime_error{"cannot read performance-counter frequency"};
        auto const to_nanoseconds = [&](unsigned long long ticks) {
            return static_cast<std::uint64_t>(ticks * 1000000000ULL /
                static_cast<unsigned long long>(clock_frequency.QuadPart));
        };
        std::cerr << "reference parser ns=" << to_nanoseconds(sph_zstdpp_reference_parse_ticks)
                  << " split_ns=" << to_nanoseconds(sph_zstdpp_reference_split_ticks)
                  << " encode_ns=" << to_nanoseconds(sph_zstdpp_reference_encode_ticks)
                  << " literals_ns=" << to_nanoseconds(sph_zstdpp_reference_literals_ticks)
                  << " tables_ns=" << to_nanoseconds(sph_zstdpp_reference_tables_ticks)
                  << " bits_ns=" << to_nanoseconds(sph_zstdpp_reference_bits_ticks) << '\n';
        std::cerr << "reference Huffman histogram_ns=" << to_nanoseconds(sph_zstdpp_reference_huf_hist_ticks)
                  << " tree_ns=" << to_nanoseconds(sph_zstdpp_reference_huf_tree_ticks)
                  << " weights_ns=" << to_nanoseconds(sph_zstdpp_reference_huf_weights_ticks)
                  << " streams_ns=" << to_nanoseconds(sph_zstdpp_reference_huf_stream_ticks) << '\n';
#else
        std::cerr << "reference parser ns=" << sph_zstdpp_reference_parse_ticks
                  << " split_ns=" << sph_zstdpp_reference_split_ticks
                  << " encode_ns=" << sph_zstdpp_reference_encode_ticks
                  << " literals_ns=" << sph_zstdpp_reference_literals_ticks
                  << " tables_ns=" << sph_zstdpp_reference_tables_ticks
                  << " bits_ns=" << sph_zstdpp_reference_bits_ticks << '\n';
        std::cerr << "reference Huffman histogram_ns=" << sph_zstdpp_reference_huf_hist_ticks
                  << " tree_ns=" << sph_zstdpp_reference_huf_tree_ticks
                  << " weights_ns=" << sph_zstdpp_reference_huf_weights_ticks
                  << " streams_ns=" << sph_zstdpp_reference_huf_stream_ticks << '\n';
#endif
#endif
#ifdef SPH_ZSTDPP_TRACE_BT
        std::cerr << "binary-tree visits unsorted=" << sph_zstdpp_bt_unsorted_visits
                  << " main=" << sph_zstdpp_bt_main_visits
                  << " extensions=" << sph_zstdpp_bt_match_extensions << '\n';
#endif
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
