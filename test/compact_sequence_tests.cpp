#include <sph/zstd++/detail/zstd_compression.h>
#include <iostream>
#include <stdexcept>

void require(bool okay) { if (!okay) throw std::runtime_error("scoped fast parser mismatch"); }
template <unsigned MinimumMatch>
std::size_t check_parser() {
    using namespace sph::zstd::detail;
    fast_match_state<19U, 14U, MinimumMatch, 0U> logical{19U,14U,MinimumMatch,0U};
    fast_match_state<19U, 14U, MinimumMatch, 0U> compact{19U,14U,MinimumMatch,0U};
    parsed_block baseline;
    compact_parsed_block candidate;
    baseline.sequences.reserve(43691U);
    candidate.sequences.reserve(43691U);
    auto const* allocation = candidate.sequences.data();
    compression_workspace base_workspace, compact_workspace;
    base_workspace.reserve(131072U,43691U);
    compact_workspace.reserve(131072U,43691U);
    std::vector<std::uint8_t> encoded_base, encoded_compact;
    std::size_t blocks{};
    for (auto size : {8U,9U,16U,31U,65535U,65536U,65539U,131071U,131072U,262144U,1048576U}) {
        for (unsigned pattern{}; pattern < 4U; ++pattern) {
            std::vector<std::uint8_t> input(size);
            std::uint32_t rng{0x31415926U};
            for (std::size_t i{}; i < input.size(); ++i) {
                rng ^= rng << 13U; rng ^= rng >> 17U; rng ^= rng << 5U;
                input[i] = pattern == 0U ? 42U : pattern == 1U ? static_cast<std::uint8_t>(rng) :
                    pattern == 2U ? static_cast<std::uint8_t>(i % 149U) :
                    static_cast<std::uint8_t>(i % 16381U == 0U ? rng : i % 37U);
            }
            logical.reset(); compact.reset();
            base_workspace.reset_entropy_history();
            compact_workspace.reset_entropy_history();
            for (std::size_t begin{}; begin < input.size(); begin += 131072U) {
                auto const length = std::min<std::size_t>(131072U,input.size()-begin);
                baseline = logical.parse(input,begin,length,std::move(baseline));
                candidate = compact.parse(input,begin,length,std::move(candidate));
                require(baseline.sequences.size() == candidate.sequences.size());
                require(baseline.input_begin == candidate.input_begin &&
                    baseline.trailing_literal_position == candidate.trailing_literal_position &&
                    baseline.trailing_literal_length == candidate.trailing_literal_length);
                for (std::size_t i{}; i < baseline.sequences.size(); ++i) {
                    auto const& old = baseline.sequences[i];
                    auto const value = candidate.sequences[i];
                    require(old.literal_length == value.literal_length && old.match_length == value.match_length &&
                        old.repeat_code == value.repeat_code && (old.repeat_code != 0U || old.offset == value.offset));
                }
                encode_sequences_block<false,true>(input,baseline,base_workspace,encoded_base);
                encode_sequences_block<false,true>(input,candidate,compact_workspace,encoded_compact);
                require(encoded_base == encoded_compact);
                require(candidate.sequences.data() == allocation);
                ++blocks;
            }
        }
    }
    return blocks;
}
void check_storage() {
    using namespace sph::zstd::detail;
    compact_sequence_store storage;
    storage.reserve(43691U);
    auto const* allocation = storage.data();
    for (std::uint32_t length{}; length <= 131072U; ++length) {
        for (std::uint8_t repeat{}; repeat < 4U; ++repeat) {
            for (bool literals : {false, true}) {
                if ((literals && length > 131069U) || (!literals && length < 3U)) continue;
                fast_parsed_sequence const expected{literals ? length : 0U, literals ? 3U : length,
                    repeat == 0U ? 2147483648U : 0U, repeat};
                storage.clear();
                storage.push_back(expected);
                require(storage.front() == expected && storage.back() == expected);
                auto const view = static_cast<compact_sequence_view>(storage);
                require(view[0] == expected && view.first(0U).empty() && view.subspan(1U,0U).empty());
            }
        }
    }
    for (bool literals : {false,true}) {
        storage.clear();
        std::array<fast_parsed_sequence,4> expected{{{0U,3U,1U,0U},
            {literals ? 65536U : 0U,literals ? 3U : 65539U,0U,1U},
            {1U,3U,0U,2U},{0U,3U,0U,3U}}};
        for (auto const& value : expected) storage.push_back(value);
        auto const full = static_cast<compact_sequence_view>(storage);
        for (std::size_t begin{}; begin <= expected.size(); ++begin)
            for (auto end = begin; end <= expected.size(); ++end) {
                auto const part = full.subspan(begin,end-begin);
                auto index = begin;
                for (auto const& value : part) require(value == expected[index++]);
                require(part.data() == storage.data()+begin);
            }
    }
    storage.clear();
    storage.resize(43690U,{0U,3U,1U,0U});
    require(storage.size() == 43690U && storage.data() == allocation);
    storage.clear();
    storage.push_back({0U,3U,1U,0U});
    require(storage.front() == fast_parsed_sequence{0U,3U,1U,0U});
}

int main() {
    static_assert(sizeof(sph::zstd::detail::parsed_sequence) == 12U);
    static_assert(sizeof(sph::zstd::detail::compact_sequence) == 8U);
    check_storage();
    std::cout << check_parser<6U>() + check_parser<7U>() << " scoped parser/entropy blocks passed\n";
}

