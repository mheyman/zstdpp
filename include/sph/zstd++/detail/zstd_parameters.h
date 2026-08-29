#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

#include <sph/zstd++/zstd_common.h>

namespace sph::zstd::detail
{
    struct level_parameters
    {
        std::uint8_t window_log;
        std::uint8_t chain_log;
        std::uint8_t hash_log;
        std::uint8_t search_log;
        std::uint8_t minimum_match;
        std::uint32_t target_length;
        compression_strategy strategy;
    };

    // Zstandard 1.6.0 presets from reference_zstd/lib/compress/clevels.h.
    inline constexpr std::array<std::array<level_parameters, 23>, 4> default_level_parameters{{
        {{
            {19, 12, 13, 1, 6, 1, compression_strategy::fast},
            {19, 13, 14, 1, 7, 0, compression_strategy::fast},
            {20, 15, 16, 1, 6, 0, compression_strategy::fast},
            {21, 16, 17, 1, 5, 0, compression_strategy::double_fast},
            {21, 18, 18, 1, 5, 0, compression_strategy::double_fast},
            {21, 18, 19, 3, 5, 2, compression_strategy::greedy},
            {21, 18, 19, 3, 5, 4, compression_strategy::lazy},
            {21, 19, 20, 4, 5, 8, compression_strategy::lazy},
            {21, 19, 20, 4, 5, 16, compression_strategy::lazy2},
            {22, 20, 21, 4, 5, 16, compression_strategy::lazy2},
            {22, 21, 22, 5, 5, 16, compression_strategy::lazy2},
            {22, 21, 22, 6, 5, 16, compression_strategy::lazy2},
            {22, 22, 23, 6, 5, 32, compression_strategy::lazy2},
            {22, 22, 22, 4, 5, 32, compression_strategy::binary_tree_lazy2},
            {22, 22, 23, 5, 5, 32, compression_strategy::binary_tree_lazy2},
            {22, 23, 23, 6, 5, 32, compression_strategy::binary_tree_lazy2},
            {22, 22, 22, 5, 5, 48, compression_strategy::binary_tree_optimal},
            {23, 23, 22, 5, 4, 64, compression_strategy::binary_tree_optimal},
            {23, 23, 22, 6, 3, 64, compression_strategy::binary_tree_ultra},
            {23, 24, 22, 7, 3, 256, compression_strategy::binary_tree_ultra2},
            {25, 25, 23, 7, 3, 256, compression_strategy::binary_tree_ultra2},
            {26, 26, 24, 7, 3, 512, compression_strategy::binary_tree_ultra2},
            {27, 27, 25, 9, 3, 999, compression_strategy::binary_tree_ultra2}
        }},
        {{
            {18, 12, 13, 1, 5, 1, compression_strategy::fast},
            {18, 13, 14, 1, 6, 0, compression_strategy::fast},
            {18, 14, 14, 1, 5, 0, compression_strategy::double_fast},
            {18, 16, 16, 1, 4, 0, compression_strategy::double_fast},
            {18, 16, 17, 3, 5, 2, compression_strategy::greedy},
            {18, 17, 18, 5, 5, 2, compression_strategy::greedy},
            {18, 18, 19, 3, 5, 4, compression_strategy::lazy},
            {18, 18, 19, 4, 4, 4, compression_strategy::lazy},
            {18, 18, 19, 4, 4, 8, compression_strategy::lazy2},
            {18, 18, 19, 5, 4, 8, compression_strategy::lazy2},
            {18, 18, 19, 6, 4, 8, compression_strategy::lazy2},
            {18, 18, 19, 5, 4, 12, compression_strategy::binary_tree_lazy2},
            {18, 19, 19, 7, 4, 12, compression_strategy::binary_tree_lazy2},
            {18, 18, 19, 4, 4, 16, compression_strategy::binary_tree_optimal},
            {18, 18, 19, 4, 3, 32, compression_strategy::binary_tree_optimal},
            {18, 18, 19, 6, 3, 128, compression_strategy::binary_tree_optimal},
            {18, 19, 19, 6, 3, 128, compression_strategy::binary_tree_ultra},
            {18, 19, 19, 8, 3, 256, compression_strategy::binary_tree_ultra},
            {18, 19, 19, 6, 3, 128, compression_strategy::binary_tree_ultra2},
            {18, 19, 19, 8, 3, 256, compression_strategy::binary_tree_ultra2},
            {18, 19, 19, 10, 3, 512, compression_strategy::binary_tree_ultra2},
            {18, 19, 19, 12, 3, 512, compression_strategy::binary_tree_ultra2},
            {18, 19, 19, 13, 3, 999, compression_strategy::binary_tree_ultra2}
        }},
        {{
            {17, 12, 12, 1, 5, 1, compression_strategy::fast},
            {17, 12, 13, 1, 6, 0, compression_strategy::fast},
            {17, 13, 15, 1, 5, 0, compression_strategy::fast},
            {17, 15, 16, 2, 5, 0, compression_strategy::double_fast},
            {17, 17, 17, 2, 4, 0, compression_strategy::double_fast},
            {17, 16, 17, 3, 4, 2, compression_strategy::greedy},
            {17, 16, 17, 3, 4, 4, compression_strategy::lazy},
            {17, 16, 17, 3, 4, 8, compression_strategy::lazy2},
            {17, 16, 17, 4, 4, 8, compression_strategy::lazy2},
            {17, 16, 17, 5, 4, 8, compression_strategy::lazy2},
            {17, 16, 17, 6, 4, 8, compression_strategy::lazy2},
            {17, 17, 17, 5, 4, 8, compression_strategy::binary_tree_lazy2},
            {17, 18, 17, 7, 4, 12, compression_strategy::binary_tree_lazy2},
            {17, 18, 17, 3, 4, 12, compression_strategy::binary_tree_optimal},
            {17, 18, 17, 4, 3, 32, compression_strategy::binary_tree_optimal},
            {17, 18, 17, 6, 3, 256, compression_strategy::binary_tree_optimal},
            {17, 18, 17, 6, 3, 128, compression_strategy::binary_tree_ultra},
            {17, 18, 17, 8, 3, 256, compression_strategy::binary_tree_ultra},
            {17, 18, 17, 10, 3, 512, compression_strategy::binary_tree_ultra},
            {17, 18, 17, 5, 3, 256, compression_strategy::binary_tree_ultra2},
            {17, 18, 17, 7, 3, 512, compression_strategy::binary_tree_ultra2},
            {17, 18, 17, 9, 3, 512, compression_strategy::binary_tree_ultra2},
            {17, 18, 17, 11, 3, 999, compression_strategy::binary_tree_ultra2}
        }},
        {{
            {14, 12, 13, 1, 5, 1, compression_strategy::fast},
            {14, 14, 15, 1, 5, 0, compression_strategy::fast},
            {14, 14, 15, 1, 4, 0, compression_strategy::fast},
            {14, 14, 15, 2, 4, 0, compression_strategy::double_fast},
            {14, 14, 14, 4, 4, 2, compression_strategy::greedy},
            {14, 14, 14, 3, 4, 4, compression_strategy::lazy},
            {14, 14, 14, 4, 4, 8, compression_strategy::lazy2},
            {14, 14, 14, 6, 4, 8, compression_strategy::lazy2},
            {14, 14, 14, 8, 4, 8, compression_strategy::lazy2},
            {14, 15, 14, 5, 4, 8, compression_strategy::binary_tree_lazy2},
            {14, 15, 14, 9, 4, 8, compression_strategy::binary_tree_lazy2},
            {14, 15, 14, 3, 4, 12, compression_strategy::binary_tree_optimal},
            {14, 15, 14, 4, 3, 24, compression_strategy::binary_tree_optimal},
            {14, 15, 14, 5, 3, 32, compression_strategy::binary_tree_ultra},
            {14, 15, 15, 6, 3, 64, compression_strategy::binary_tree_ultra},
            {14, 15, 15, 7, 3, 256, compression_strategy::binary_tree_ultra},
            {14, 15, 15, 5, 3, 48, compression_strategy::binary_tree_ultra2},
            {14, 15, 15, 6, 3, 128, compression_strategy::binary_tree_ultra2},
            {14, 15, 15, 7, 3, 256, compression_strategy::binary_tree_ultra2},
            {14, 15, 15, 8, 3, 256, compression_strategy::binary_tree_ultra2},
            {14, 15, 15, 8, 3, 512, compression_strategy::binary_tree_ultra2},
            {14, 15, 15, 9, 3, 512, compression_strategy::binary_tree_ultra2},
            {14, 15, 15, 10, 3, 999, compression_strategy::binary_tree_ultra2}
        }}
    }};

    [[nodiscard]] consteval auto resolve_parameters(compression_parameters requested)
        -> compression_parameters
    {
        auto const source_size{requested.pledged_source_size};
        auto const tier = source_size == unknown_content_size ? std::size_t{0} :
            source_size <= 16U * 1024U ? std::size_t{3} :
            source_size <= 128U * 1024U ? std::size_t{2} :
            source_size <= 256U * 1024U ? std::size_t{1} : std::size_t{0};
        auto const level = requested.compression_level == 0 ? 3 :
            std::clamp(requested.compression_level, 1, 22);
        auto preset{default_level_parameters[tier][requested.compression_level < 0 ? 0 :
            static_cast<std::size_t>(level)]};
        if (requested.compression_level < 0)
        {
            preset.target_length = static_cast<std::uint32_t>(
                std::min(-static_cast<std::int64_t>(requested.compression_level),
                    static_cast<std::int64_t>(maximum_block_size)));
        }

        if (requested.window_log != 0U) preset.window_log = requested.window_log;
        if (requested.chain_log != 0U) preset.chain_log = requested.chain_log;
        if (requested.hash_log != 0U) preset.hash_log = requested.hash_log;
        if (requested.search_log != 0U) preset.search_log = requested.search_log;
        if (requested.minimum_match != 0U) preset.minimum_match = requested.minimum_match;
        if (requested.target_length != 0U) preset.target_length = requested.target_length;
        if (requested.strategy != compression_strategy::automatic) preset.strategy = requested.strategy;

        if (source_size != unknown_content_size && source_size <= (std::uint64_t{1} << 30U))
        {
            auto const source_log = source_size < (std::uint64_t{1} << 6U) ? 6U :
                static_cast<unsigned>(std::bit_width(source_size - 1U));
            preset.window_log = static_cast<std::uint8_t>(
                std::min<unsigned>(preset.window_log, source_log));
            auto const cycle_log{static_cast<unsigned>(preset.chain_log) -
                (preset.strategy >= compression_strategy::binary_tree_lazy2 ? 1U : 0U)};
            preset.hash_log = static_cast<std::uint8_t>(
                std::min<unsigned>(preset.hash_log, preset.window_log + 1U));
            if (cycle_log > preset.window_log)
            {
                preset.chain_log = static_cast<std::uint8_t>(
                    preset.chain_log - (cycle_log - preset.window_log));
            }
        }
        preset.window_log = std::max<std::uint8_t>(preset.window_log, 10U);

        requested.window_log = preset.window_log;
        requested.chain_log = preset.chain_log;
        requested.hash_log = preset.hash_log;
        requested.search_log = preset.search_log;
        requested.minimum_match = preset.minimum_match;
        requested.target_length = preset.target_length;
        requested.strategy = preset.strategy;
        return requested;
    }
}
