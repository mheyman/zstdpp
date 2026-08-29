#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace sph::zstd::eval
{
    /** The deterministic mixed corpus shared by benchmarks and parity tests. */
    [[nodiscard]] inline auto make_mixed_corpus(std::size_t size) -> std::vector<std::uint8_t>
    {
        if (size == 0U || size % 4U != 0U)
        {
            throw std::invalid_argument{"evaluation corpus size must be a nonzero multiple of four"};
        }
        std::vector<std::uint8_t> corpus(size);
        auto const quarter{corpus.size() / 4U};

        std::fill_n(corpus.begin(), static_cast<std::ptrdiff_t>(quarter), std::uint8_t{});
        constexpr std::string_view phrase{"The quick brown fox jumps over the lazy dog. Zstandard C++ evaluation.\n"};
        for (std::size_t index{quarter}; index < quarter * 2U; ++index)
        {
            corpus[index] = static_cast<std::uint8_t>(phrase[(index - quarter) % phrase.size()]);
        }
        for (std::size_t index{quarter * 2U}; index < quarter * 3U; ++index)
        {
            auto const relative{index - quarter * 2U};
            corpus[index] = static_cast<std::uint8_t>((relative / 16U + relative % 7U) & 0xFFU);
        }
        std::uint32_t random{0xC001D00DU};
        for (std::size_t index{quarter * 3U}; index < corpus.size(); ++index)
        {
            random ^= random << 13U;
            random ^= random >> 17U;
            random ^= random << 5U;
            corpus[index] = static_cast<std::uint8_t>(random);
        }
        return corpus;
    }
}
