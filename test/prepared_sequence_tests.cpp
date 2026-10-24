#include <sph/zstd++/detail/zstd_compression.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    void require(bool condition)
    {
        if (!condition) throw std::runtime_error{"prepared double-fast sequence mismatch"};
    }

    void check_store()
    {
        using sph::zstd::detail::parsed_sequence;
        using sph::zstd::detail::prepared_sequence_store;
        prepared_sequence_store source;
        source.reserve(8U);
        auto const* storage{source.data()};
        source.push_back(parsed_sequence{3U, 5U, 17U, 0U});
        source.push_back(parsed_sequence{0U, 9U, 1U, 1U});
        auto moved{std::move(source)};
        require(source.empty() && moved.size() == 2U && moved.data() == storage);
        require(moved[0] == parsed_sequence{3U, 5U, 17U, 0U});
        source.reserve(8U);
        source.push_back(parsed_sequence{1U, 7U, 4U, 0U});
        require(source.size() == 1U);
        prepared_sequence_store assigned;
        assigned = std::move(moved);
        require(moved.empty() && assigned.size() == 2U && assigned.data() == storage);
        assigned.clear();
        assigned.push_back(parsed_sequence{2U, 11U, 8U, 0U});
        require(assigned.size() == 1U && assigned.data() == storage);
    }

    auto make_input(std::size_t size, unsigned pattern) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> result(size);
        std::uint32_t random{0x31415926U};
        for (std::size_t index{}; index < size; ++index)
        {
            random ^= random << 13U;
            random ^= random >> 17U;
            random ^= random << 5U;
            result[index] = pattern == 0U ? 42U : pattern == 1U ?
                static_cast<std::uint8_t>(random) : pattern == 2U ?
                static_cast<std::uint8_t>(index % 149U) :
                static_cast<std::uint8_t>(index % 16381U == 0U ? random : index % 37U);
        }
        return result;
    }

    auto check_parser() -> std::size_t
    {
        using namespace sph::zstd::detail;
        double_fast_match_state<20U, 17U, 16U, 5U> original{20U, 17U, 16U, 5U};
        double_fast_match_state<20U, 17U, 16U, 5U> prepared{20U, 17U, 16U, 5U};
        parsed_block original_block;
        prepared_parsed_block prepared_block;
        constexpr std::size_t block_size{131072U};
        constexpr std::size_t maximum_sequences{block_size / 3U + 1U};
        original_block.sequences.reserve(maximum_sequences);
        prepared_block.sequences.reserve(maximum_sequences);
        auto const* storage{prepared_block.sequences.data()};
        compression_workspace original_workspace;
        compression_workspace prepared_workspace;
        original_workspace.reserve(block_size, maximum_sequences);
        prepared_workspace.reserve(block_size, maximum_sequences);
        std::vector<std::uint8_t> original_frame;
        std::vector<std::uint8_t> prepared_frame;
        std::size_t checked{};
        for (auto size : {8U, 9U, 16U, 1024U, 65536U, 131072U, 262144U})
        {
            for (unsigned pattern{}; pattern < 4U; ++pattern)
            {
                auto const input{make_input(size, pattern)};
                original.reset();
                prepared.reset();
                original_workspace.reset_entropy_history();
                prepared_workspace.reset_entropy_history();
                for (std::size_t begin{}; begin < input.size(); begin += block_size)
                {
                    auto const length{std::min(block_size, input.size() - begin)};
                    original_block = original.parse(input, begin, length, std::move(original_block));
                    prepared_block = prepared.parse(input, begin, length, std::move(prepared_block));
                    require(original_block.sequences.size() == prepared_block.sequences.size());
                    require(original_block.input_begin == prepared_block.input_begin &&
                        original_block.trailing_literal_position == prepared_block.trailing_literal_position &&
                        original_block.trailing_literal_length == prepared_block.trailing_literal_length);
                    for (std::size_t index{}; index < original_block.sequences.size(); ++index)
                        require(original_block.sequences[index] == prepared_block.sequences[index]);
                    encode_sequences_block<false, false>(input, original_block,
                        original_workspace, original_frame);
                    encode_sequences_block<false, false>(input, prepared_block,
                        prepared_workspace, prepared_frame);
                    require(original_frame == prepared_frame);
                    require(prepared_block.sequences.data() == storage);
                    ++checked;
                }
            }
        }
        return checked;
    }
}

int main()
{
    check_store();
    std::cout << check_parser() << " prepared double-fast blocks passed\n";
}
