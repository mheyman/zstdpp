#include <sph/zstd++/zstd_compress.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

template <unsigned MinimumMatch, unsigned HashLog, bool Collect>
std::size_t check_epochs()
{
    using namespace sph::zstd::detail;
    constexpr std::size_t source_size{4U * 1024U * 1024U};
    constexpr std::size_t block_size{65536U};
    std::vector<std::uint8_t> input(source_size);
    constexpr std::string_view text{"Index epochs must preserve current-frame matches while rejecting stale history. "};
    for (std::size_t i{}; i < input.size(); ++i)
        input[i] = static_cast<std::uint8_t>(text[i % text.size()]);
    for (std::size_t i{96U}; i < input.size(); i += 149U)
        input[i] = static_cast<std::uint8_t>((i / 149U) & 255U);

    using parser = fast_match_state<19U, HashLog, MinimumMatch, 0U>;
    constexpr unsigned runtime_minimum{MinimumMatch == 0U ? 7U : MinimumMatch};
    parser reused{19U, HashLog, runtime_minimum, 0U};
    std::array<std::uint8_t, block_size> actual_literals{}, expected_literals{};
    std::size_t cases{};
    // Cross twice: the first rollover's reset-to-zero changes the epoch phase.
    // The 4 MiB extent exceeds the 512 KiB window; earlier tests did not.
    for (unsigned frame{}; frame < 2200U; ++frame)
    {
        reused.reset();
        parser fresh{19U, HashLog, runtime_minimum, 0U};
        for (auto begin : {std::size_t{}, source_size / 4U - block_size,
            source_size / 4U, source_size - block_size})
        {
            auto const actual{reused.template parse<Collect>(input, begin, block_size,
                parsed_block{}, std::nullopt, actual_literals)};
            auto const expected{fresh.template parse<Collect>(input, begin, block_size,
                parsed_block{}, std::nullopt, expected_literals)};
            if (actual.sequences != expected.sequences ||
                actual.trailing_literal_position != expected.trailing_literal_position ||
                actual.trailing_literal_length != expected.trailing_literal_length)
                throw std::runtime_error{"fast index epoch changed parser output after reset"};
            if constexpr (Collect)
            {
                std::size_t used{};
                if (!actual.sequences.empty())
                {
                    used = actual.trailing_literal_length;
                    for (auto const& sequence : actual.sequences) used += sequence.literal_length;
                }
                if (!std::equal(actual_literals.begin(), actual_literals.begin() +
                    static_cast<std::ptrdiff_t>(used), expected_literals.begin()))
                    throw std::runtime_error{"fast index epoch changed gathered literals"};
            }
            ++cases;
        }
    }
    return cases;
}

void check_complete_frames()
{
    constexpr std::size_t size{4U * 1024U * 1024U};
    constexpr auto parameters = [] {
        auto p = sph::zstd::compression_parameters{};
        p.compression_level = 1;
        p.pledged_source_size = size;
        return p;
    }();
    std::vector<std::uint8_t> input(size), output;
    constexpr std::string_view text{"Header-only compression makes specialization visible to the optimizer. Huffman coding handles the literals while FSE reconstructs match sequences.\n"};
    for (std::size_t i{}; i < size; ++i) input[i] = static_cast<std::uint8_t>(text[i % text.size()]);
    for (std::size_t i{96U}; i < size; i += 149U) input[i] = static_cast<std::uint8_t>((i / 149U) & 255U);
    output.reserve(size + size / 128U + 64U);
    auto compressor{sph::zstd::make_zstd_compress<parameters>(
        [&output](std::span<std::uint8_t const> bytes) {
            output.insert(output.end(), bytes.begin(), bytes.end());
        })};
    compressor.reset();
    compressor.compress_frame(input);
    auto const expected{output};
    for (unsigned frame{}; frame < 2048U; ++frame)
    {
        output.clear();
        compressor.reset();
        compressor.compress_frame(input);
        if (output != expected)
            throw std::runtime_error{"complete frame changed across fast index rollover"};
    }
    std::cout << "2049 complete 4 MiB frames preserved across epoch rollovers\n";
}

int main()
{
    auto const cases{check_epochs<7U, 8U, false>() + check_epochs<7U, 14U, true>() +
        check_epochs<6U, 8U, false>() + check_epochs<0U, 8U, true>()};
    std::cout << cases << " fast index-epoch blocks passed\n";
    check_complete_frames();
}
