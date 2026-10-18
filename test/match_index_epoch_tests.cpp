#include <sph/zstd++/detail/zstd_compression.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace sph::zstd::detail
{
    // Deliberately seed a reachable near-limit origin without processing GiB.
    // This friend has no implementation or data in the production library.
    struct index_epoch_test_access
    {
        template <typename State>
        static void seed(State& state, std::uint32_t base)
        {
            state.frame_index_base_ = base;
            if constexpr (requires { state.next_to_update_; })
            {
                state.next_to_update_ = base + 2U;
                state.next_to_update3_ = base + 2U;
            }
        }

        template <typename State>
        static auto base(State const& state) -> std::uint32_t { return state.frame_index_base_; }

        template <typename State>
        static void check_ring(State& state, std::uint32_t origin)
        {
            seed(state, origin);
            state.next_to_update_ = origin + 17U;
            state.next_to_update3_ = origin + 31U;
            state.repeat_offsets_ = {17U, 31U, 29U};
            for (std::size_t slot{}; slot < state.chain_table_.size(); ++slot)
                state.chain_table_[slot] = slot % 4U == 0U ? 1U :
                    slot % 4U == 1U ? origin - 1U : origin + 2U + static_cast<std::uint32_t>(slot);
            for (std::size_t slot{}; slot < state.chain_tags_.size(); ++slot)
                state.chain_tags_[slot] = static_cast<std::uint8_t>(slot);
            auto const before{state.chain_table_};
            auto const tags{state.chain_tags_};
            auto const repeat{state.repeat_offsets_};
            auto const allocation_count{cache_aligned_allocation_count};
            auto const deallocation_count{cache_aligned_deallocation_count};
            state.ensure_index_headroom(2U * 1024U * 1024U);
            if (allocation_count != cache_aligned_allocation_count ||
                deallocation_count != cache_aligned_deallocation_count)
                throw std::runtime_error{"epoch rebasing allocated workspace"};
            // Trees have two links per node and no byte-tag ring.
            auto const tree{tags.empty()};
            auto const shift{tree ? 2U * (origin & state.tree_mask_) : origin & state.chain_mask_};
            for (std::size_t slot{}; slot < before.size(); ++slot)
            {
                auto const old_slot{(slot + shift) % before.size()};
                auto const old{before[old_slot]};
                auto const expected{tree && old == 1U ? 1U : old >= origin + 2U ? old - origin : 0U};
                if (state.chain_table_[slot] != expected ||
                    (!tree && state.chain_tags_[slot] != tags[old_slot]))
                    throw std::runtime_error{"epoch rebasing changed ring links, tags or sentinels"};
            }
            if (state.frame_index_base_ != 0U || state.next_to_update_ != 17U ||
                state.next_to_update3_ != 31U || state.repeat_offsets_ != repeat)
                throw std::runtime_error{"epoch rebasing changed update positions or repeat offsets"};
            bool rejected{};
            try { state.ensure_index_headroom(std::numeric_limits<std::uint32_t>::max()); }
            catch (entropy_error const&) { rejected = true; }
            if (!rejected) throw std::runtime_error{"unrepresentable block end was accepted"};
        }

        template <typename State>
        static void check_row(State& state)
        {
            auto& row{state.row_state_};
            row.frame_base = 0x7ff00000U;
            row.next = 512U;
            auto* entries{row.table.hash_table()};
            entries[1] = row.frame_base + 100U;
            entries[2] = row.frame_base - 1U;
            auto const salt{row.salt.salt()};
            state.ensure_index_headroom(0x80100000U);
            if (row.frame_base != 0U || entries[1] != 100U || entries[2] != 0U ||
                row.next != 512U || row.salt.salt() != salt)
                throw std::runtime_error{"row epoch rebasing changed local history or salt"};
        }
    };
}

template <bool Optimal = false, typename Factory>
std::size_t check(std::string_view name, Factory factory, std::span<std::uint8_t const> input)
{
    using namespace sph::zstd::detail;
    std::size_t blocks{};
    for (auto origin : {0xfff00000U, 0xfff00123U})
    {
        auto seeded{factory()}, fresh{factory()};
        index_epoch_test_access::seed(seeded, origin);
        compression_workspace a, b;
        a.reserve(131072U, 43691U); b.reserve(131072U, 43691U);
        if constexpr (Optimal) { a.optimal.reserve(); b.optimal.reserve(); }
        for (std::size_t begin{}; begin < input.size(); begin += 131072U)
        {
            auto const length{std::min<std::size_t>(131072U, input.size() - begin)};
            auto parse = [&](auto& state, auto& workspace) {
                if constexpr (Optimal)
                    return parse_optimal_block(state, input, begin, length, workspace, parsed_block{}, false, 64U);
                else { (void)workspace; return state.parse(input, begin, length); }
            };
            auto const actual{parse(seeded, a)}, expected{parse(fresh, b)};
            if (actual.sequences != expected.sequences ||
                actual.trailing_literal_position != expected.trailing_literal_position ||
                actual.trailing_literal_length != expected.trailing_literal_length ||
                static_cast<std::uint64_t>(index_epoch_test_access::base(seeded)) + begin + length + 11U >
                    std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error{"near-limit epoch changed parser output or wrapped"};
            ++blocks;
        }
    }
    std::cout << name << ": " << blocks << " boundary blocks passed\n";
    return blocks;
}

int main()
{
    using namespace sph::zstd::detail;
    std::vector<std::uint8_t> input(2U * 1024U * 1024U);
    constexpr std::string_view text{"Header-only compression makes specialization visible to the optimizer. Huffman coding handles the literals while FSE reconstructs match sequences.\n"};
    for (std::size_t i{}; i < input.size(); ++i) input[i] = static_cast<std::uint8_t>(text[i % text.size()]);
    for (std::size_t i{96U}; i < input.size(); i += 149U) input[i] = static_cast<std::uint8_t>((i / 149U) & 255U);
    using D = double_fast_match_state<19, 10, 10, 5>;
    using D4 = double_fast_match_state<19, 10, 10, 4>;
    using DR = double_fast_match_state<19, 10, 10, 0>;
    using G = greedy_match_state<0, false, 19, 10, 10, 3, 4>;
    using L = greedy_match_state<1, false, 19, 10, 10, 3, 4>;
    using B = greedy_match_state<2, true, 19, 10, 10, 3, 4>;
    using O = greedy_match_state<0, true, 19, 10, 10, 3, 3, false, true>;
    using O4 = greedy_match_state<0, true, 19, 10, 10, 3, 4>;
    using R = greedy_match_state<1, false, 19, 10, 10, 4, 4, true>;
    for (unsigned pattern{}; pattern < 3U; ++pattern)
    {
        if (pattern == 1U)
        {
            std::uint32_t random{0x31415926U};
            for (std::size_t i{}; i < input.size(); ++i)
            {
                random ^= random << 13U; random ^= random >> 17U; random ^= random << 5U;
                // Repeated random tiles make non-rep match distances vary.
                input[i] = i >= 8192U && (i / 8192U) % 3U != 0U ?
                    input[(i / 8192U % 7U) * 1024U + i % 1024U] : static_cast<std::uint8_t>(random);
            }
        }
        if (pattern == 2U) std::ranges::fill(input, std::uint8_t{42U});
        check("double-fast", [] { return D{19, 10, 10, 5}; }, input);
        check("double-fast tags", [] { return D4{19, 10, 10, 4}; }, input);
        check("double-fast runtime", [] { return DR{19, 10, 10, 5}; }, input);
        check("hash-chain", [] { return G{19, 10, 10, 3, 4}; }, input);
        check("lazy hash-chain", [] { return L{19, 10, 10, 3, 4}; }, input);
        check("binary-tree", [] { return B{19, 10, 10, 3, 4}; }, input);
        check<true>("optimal-tree", [] { return O{19, 10, 10, 3, 3}; }, input);
        check<true>("optimal-tree four-byte", [] { return O4{19, 10, 10, 3, 4}; }, input);
    }
    for (auto origin : {0xfff00000U, 0xfff00123U})
    {
        G chain{19, 10, 10, 3, 4}; B tree{19, 10, 10, 3, 4}; O optimal{19, 10, 10, 3, 3};
        index_epoch_test_access::check_ring(chain, origin);
        index_epoch_test_access::check_ring(tree, origin);
        index_epoch_test_access::check_ring(optimal, origin);
    }
    R row{19, 10, 10, 4, 4};
    index_epoch_test_access::check_row(row);
    std::cout << "Ring rotation, sentinels, row origin, range rejection and allocation checks passed\n";
}
