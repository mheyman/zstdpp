#include <sph/zstd++/detail/zstd_entropy.h>

#include <array>
#include <cstdio>
#include <cstdlib>

namespace
{
    void require(bool condition)
    {
        if (!condition) std::abort();
    }
}

int main()
{
    using sph::zstd::detail::reverse_bit_reader;
    using sph::zstd::detail::scheduled_sequence_reader;
    unsigned random{0x573a31U};
    auto next = [&] { random = random * 1664525U + 1013904223U; return random; };
    for (unsigned trial{}; trial < 32768U; ++trial)
    {
        std::array<std::uint8_t, 257> bytes{};
        auto const size{1U + next() % bytes.size()};
        for (auto& byte : bytes) byte = static_cast<std::uint8_t>(next() >> 24U);
        bytes[size - 1U] = static_cast<std::uint8_t>(1U << (trial % 8U));
        auto const source{std::span<std::uint8_t const>{bytes}.first(size)};
        reverse_bit_reader original{source};
        scheduled_sequence_reader scheduled{source};
        auto read = [&](unsigned width)
        {
            require(original.read_fast(width) == scheduled.read_fast(width));
            require(original.remaining() == scheduled.remaining());
            require(original.overflowed() == scheduled.overflowed());
            require(original.at_end() == scheduled.at_end());
        };
        read(next() % 10U);
        read(next() % 9U);
        read(next() % 10U);
        scheduled.reload();
        for (unsigned sequence{}; sequence < 64U; ++sequence)
        {
            auto const offset_bits{next() % 32U};
            auto const match_bits{next() % 17U};
            auto const literal_bits{next() % 17U};
            read(offset_bits);
            read(match_bits);
            if (offset_bits + match_bits + literal_bits >= 31U) scheduled.reload();
            read(literal_bits);
            if (sequence != 63U)
            {
                auto const literal_state_bits{next() % 10U};
                auto const match_state_bits{next() % 10U};
                read(literal_state_bits + match_state_bits);
                read(next() % 9U);
                scheduled.reload();
            }
        }
    }

    // Exercise every short-head size/marker and exact end, including zero reads
    // at exhaustion and the persistent overflow flag after an overread.
    for (std::size_t size{1U}; size <= 17U; ++size)
    {
        for (unsigned marker{}; marker < 8U; ++marker)
        {
            std::array<std::uint8_t, 17> bytes{};
            bytes.fill(0xD3U);
            bytes[size - 1U] = static_cast<std::uint8_t>(1U << marker);
            auto const source{std::span<std::uint8_t const>{bytes}.first(size)};
            scheduled_sequence_reader reader{source};
            reverse_bit_reader original{source};
            while (original.remaining() != 0U)
            {
                require(reader.read_fast(1U) == original.read_fast(1U));
                reader.reload();
            }
            require(reader.at_end());
            require(reader.read_fast(0U) == 0U && reader.at_end());
            require(reader.read_fast(1U) == 0U && reader.overflowed() && !reader.at_end());
        }
    }
    std::array<std::uint8_t, 1> const missing_marker{0U};
    for (auto source : {std::span<std::uint8_t const>{},
        std::span<std::uint8_t const>{missing_marker}})
    {
        bool rejected{};
        try { scheduled_sequence_reader reader{source}; (void)reader; }
        catch (sph::zstd::detail::entropy_error const&) { rejected = true; }
        require(rejected);
    }
    std::puts("scheduled sequence reader tests passed");
}
