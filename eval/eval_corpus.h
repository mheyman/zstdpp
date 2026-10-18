#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace sph::zstd::eval
{
    // Exercises optimal-strategy pre-splitting and mutations at block boundaries.
    [[nodiscard]] inline auto make_optimal_presplit_corpus(unsigned seed = 3U) -> std::vector<std::uint8_t>
    {
        constexpr std::size_t block{128U * 1024U};
        std::vector<std::uint8_t> input(8U * block);
        std::uint32_t random{seed};
        for (std::size_t i{}; i < input.size(); ++i)
        {
            random = random * 1664525U + 1013904223U;
            input[i] = static_cast<std::uint8_t>((i % (seed * 3U)) ^ (i / 8191U));
            if (i % (149U + seed) == 0U)
                input[i] ^= static_cast<std::uint8_t>(random >> 16U);
        }
        for (auto boundary{block}; boundary < input.size(); boundary += block)
            for (std::size_t delta{}; delta <= 17U; ++delta)
            {
                input[boundary - delta] ^= static_cast<std::uint8_t>(seed + delta);
                input[boundary + delta] ^= static_cast<std::uint8_t>(255U - seed - delta);
            }
        return input;
    }

    // Alternating diverse/text blocks exercise direct-to-tagged parser history.
    [[nodiscard]] inline auto make_tag_transition_corpus(unsigned seed = 1U)
        -> std::vector<std::uint8_t>
    {
        constexpr std::size_t block{128U * 1024U};
        std::vector<std::uint8_t> input(8U * block);
        std::uint32_t random{seed};
        auto next = [&]
        {
            random ^= random << 13U;
            random ^= random >> 17U;
            random ^= random << 5U;
            return random;
        };
        for (auto& byte : input) byte = static_cast<std::uint8_t>(next());
        for (std::size_t begin{}; begin < input.size(); begin += 2U * block)
        {
            for (std::size_t record{}; record < 128U; ++record)
                for (std::size_t j{}; j < 64U; ++j)
                    input[begin + record * 1024U + 7U + j] =
                        static_cast<std::uint8_t>('A' + next() % 26U);
            for (std::size_t j{}; j < block; ++j)
                input[begin + block + j] =
                    input[begin + ((j / 64U) % 128U) * 1024U + 7U + (j % 64U)];
        }
        return input;
    }

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
