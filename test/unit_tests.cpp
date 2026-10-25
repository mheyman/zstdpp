#include <sph/zstd++/zstd_compress.h>
#include <sph/zstd++/zstd_decompress.h>

#include "eval_corpus.h"

#include <zstd.h>
extern "C" {
#include <common/fse.h>
}

// Exported by the bundled reference library (zstd_compress_sequences.h).
extern "C" std::size_t ZSTD_fseBitCost(FSE_CTable const*, unsigned const*, unsigned);
extern "C" std::size_t ZSTD_crossEntropyCost(short const*, unsigned, unsigned const*, unsigned);
extern "C" std::size_t FSE_buildCTable_wksp(FSE_CTable*, short const*, unsigned, unsigned,
    void*, std::size_t);
// HUF_CElt is size_t in the bundled reference's huf.h.
extern "C" std::size_t HUF_buildCTable_wksp(std::size_t*, unsigned const*, unsigned,
    unsigned, void*, std::size_t);
extern "C" unsigned HUF_getNbBitsFromCTable(std::size_t const*, unsigned);
extern "C" std::size_t HUF_writeCTable_wksp(void*, std::size_t, std::size_t const*,
    unsigned, unsigned, void*, std::size_t);
extern "C" unsigned HUF_optimalTableLog(unsigned, std::size_t, unsigned, void*, std::size_t,
    std::size_t*, unsigned const*, int);

#include <algorithm>
#include <array>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numeric>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    auto failures = 0;

    void check(bool condition, std::string_view message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    template <typename Function>
    void check_error(Function&& function, sph::zstd::error_code expected, std::string_view message)
    {
        try
        {
            function();
            check(false, message);
        }
        catch (sph::zstd::zstd_error const& error)
        {
            check(error.code() == expected, message);
        }
    }

    constexpr auto small_blocks = []
    {
        auto parameters = sph::zstd::compression_parameters{};
        parameters.block_size = 17;
        return parameters;
    }();

    constexpr auto checked_content = []
    {
        auto parameters = small_blocks;
        parameters.checksum = true;
        parameters.pledged_source_size = 257;
        return parameters;
    }();

    auto make_input(std::size_t size) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> input(size);
        for (std::size_t index{}; index < input.size(); ++index)
        {
            input[index] = static_cast<std::uint8_t>((index * 37U + index / 11U) & 0xFFU);
        }
        return input;
    }

    auto read_file(std::filesystem::path const& path) -> std::vector<std::uint8_t>
    {
        std::ifstream input{path, std::ios::binary};
        if (!input)
        {
            throw std::runtime_error{"cannot open reference test file: " + path.string()};
        }
        return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    template <sph::zstd::compression_parameters Parameters = {}>
    auto compress(std::span<std::uint8_t const> input) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> encoded;
        auto compressor = sph::zstd::make_zstd_compress<Parameters>(
            [&encoded](std::span<std::uint8_t const> output)
            {
                encoded.insert(encoded.end(), output.begin(), output.end());
            });
        compressor.update(input);
        compressor.finish();
        check(compressor.status() == sph::zstd::stream_status::finished, "compressor reaches finished state");
        check(compressor.source_size() == input.size(), "compressor reports source size");
        check(compressor.encoded_size() == encoded.size(), "compressor reports encoded size");
        return encoded;
    }

    template <sph::zstd::compression_parameters Parameters = {}>
    auto compress_frame(std::span<std::uint8_t const> input) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> encoded;
        auto compressor = sph::zstd::make_zstd_compress<Parameters>(
            [&encoded](std::span<std::uint8_t const> output)
            {
                encoded.insert(encoded.end(), output.begin(), output.end());
            });
        compressor.compress_frame(input);
        check(compressor.status() == sph::zstd::stream_status::finished,
            "one-shot compressor reaches finished state");
        check(compressor.source_size() == input.size(), "one-shot compressor reports source size");
        check(compressor.encoded_size() == encoded.size(), "one-shot compressor reports encoded size");
        return encoded;
    }

    auto reference_decompress(std::span<std::uint8_t const> encoded, std::size_t expected_size)
        -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> decoded(expected_size);
        auto const result{ZSTD_decompress(decoded.data(), decoded.size(), encoded.data(), encoded.size())};
        check(ZSTD_isError(result) == 0U, "reference zstd accepts sph::zstd output");
        if (ZSTD_isError(result) != 0U)
        {
            std::cerr << "  reference error: " << ZSTD_getErrorName(result) << '\n';
        }
        if (ZSTD_isError(result) == 0U)
        {
            check(result == expected_size, "reference zstd reports expected decoded size");
            decoded.resize(result);
        }
        return decoded;
    }

    auto reference_compress(std::span<std::uint8_t const> input, int level) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> encoded(ZSTD_compressBound(input.size()));
        auto const result{ZSTD_compress(encoded.data(), encoded.size(), input.data(), input.size(), level)};
        check(ZSTD_isError(result) == 0U, "reference zstd creates entropy-coded test frame");
        if (ZSTD_isError(result) != 0U)
        {
            throw std::runtime_error{ZSTD_getErrorName(result)};
        }
        encoded.resize(result);
        return encoded;
    }

    auto cpp_decompress(std::span<std::uint8_t const> encoded, std::size_t expected_size,
        std::size_t chunk_size) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> decoded;
        auto decompressor = sph::zstd::zstd_decompress{
            [&decoded](std::span<std::uint8_t const> output)
            {
                decoded.insert(decoded.end(), output.begin(), output.end());
            }};
        for (auto chunk : encoded | std::views::chunk(chunk_size))
        {
            decompressor.update(chunk);
        }
        decompressor.finish();
        check(decompressor.decoded_size() == expected_size,
            "zstd++ reports the reference frame's decoded size");
        return decoded;
    }

    auto make_entropy_input() -> std::vector<std::uint8_t>
    {
        constexpr std::string_view paragraph{
            "Header-only compression makes specialization visible to the optimizer. "
            "Huffman coding handles the literals while FSE reconstructs match sequences.\n"};
        std::vector<std::uint8_t> input;
        input.reserve(600U * 1024U);
        for (std::size_t index{}; input.size() < 600U * 1024U; ++index)
        {
            for (auto const character : paragraph)
            {
                input.push_back(static_cast<std::uint8_t>(character));
            }
            input.push_back(static_cast<std::uint8_t>(index));
            input.push_back(static_cast<std::uint8_t>(index >> 8U));
        }
        input.resize(600U * 1024U);
        return input;
    }

    auto make_mixed_entropy_input() -> std::vector<std::uint8_t>
    {
        return sph::zstd::eval::make_mixed_corpus(1024U * 1024U);
    }

    template <std::uint8_t MinimumMatch, bool Mixed>
    void check_custom_double_fast_frames()
    {
        constexpr auto parameters = []
        {
            sph::zstd::compression_parameters value{};
            value.compression_level = 1;
            value.strategy = sph::zstd::compression_strategy::double_fast;
            value.window_log = 16;
            value.hash_log = 15;
            value.chain_log = 14;
            value.minimum_match = MinimumMatch;
            value.block_size = 65536U;
            value.checksum = true;
            value.content_size = false;
            // Equal final-block knowledge is needed for byte parity: an unknown-size
            // streaming frame ending on a full block adds an empty final block.
            value.pledged_source_size = Mixed ? 1024U * 1024U : 600U * 1024U;
            return value;
        }();
        static_assert(sph::zstd::detail::resolve_parameters(parameters).strategy ==
            sph::zstd::compression_strategy::double_fast);
        std::vector<std::uint8_t> encoded;
        auto compressor = sph::zstd::make_zstd_compress<parameters>(
            [&encoded](std::span<std::uint8_t const> bytes)
            {
                encoded.insert(encoded.end(), bytes.begin(), bytes.end());
            });
        auto const input{Mixed ? make_mixed_entropy_input() : make_entropy_input()};
        for (unsigned repetition{}; repetition < 2U; ++repetition)
        {
            compressor.reset();
            encoded.clear();
            compressor.compress_frame(input);
            auto const one_shot{encoded};
            check(reference_decompress(one_shot, input.size()) == input,
                "reference decodes explicit double-fast strategy at level 1");
            check(cpp_decompress(one_shot, input.size(), 17U) == input,
                "custom double-fast frame streams through the C++ decoder");
            compressor.reset();
            encoded.clear();
            for (auto chunk : std::span<std::uint8_t const>{input} | std::views::chunk(997U))
                compressor.update(chunk);
            compressor.finish();
            check(encoded == one_shot,
                "custom double-fast reset and chunked input preserve frame bytes");
            check(reference_decompress(encoded, input.size()) == input,
                "reference decodes custom double-fast streaming output after reset");
        }
    }

    void test_literal_workspace_alignment()
    {
        static_assert(std::is_same_v<sph::zstd::detail::compression_workspace::literal_allocator,
            std::allocator<std::uint8_t>>);
        static_assert(std::is_same_v<
            sph::zstd::detail::basic_compression_workspace<true>::literal_allocator,
            sph::zstd::detail::cache_aligned_allocator<std::uint8_t>>);
        auto const allocations{sph::zstd::detail::cache_aligned_allocation_count};
        auto const deallocations{sph::zstd::detail::cache_aligned_deallocation_count};
        {
            sph::zstd::detail::basic_compression_workspace<true> workspace;
            workspace.reserve(131072U, 43690U);
            check(reinterpret_cast<std::uintptr_t>(workspace.literals.data()) % 64U == 0U,
                "literal workspace is cache-line aligned");
            auto const* const storage{workspace.literals.data()};
            auto const initialized_allocations{sph::zstd::detail::cache_aligned_allocation_count};
            for (std::size_t size : {1U, 4095U, 4096U, 65536U, 131072U})
            {
                workspace.reserve(size, 1U);
                check(workspace.literals.data() == storage && workspace.literals.size() == size,
                    "literal workspace keeps its allocation through size changes");
            }
            check(sph::zstd::detail::cache_aligned_allocation_count == initialized_allocations,
                "literal workspace reuses the initial aligned allocation");
        }
        check(sph::zstd::detail::cache_aligned_allocation_count - allocations ==
            sph::zstd::detail::cache_aligned_deallocation_count - deallocations,
            "literal workspace releases its aligned allocation");
    }

    template <unsigned WindowLog, unsigned MinimumMatch>
    void check_double_fast_reset_indices()
    {
        using state_type = sph::zstd::detail::double_fast_match_state<WindowLog, 10U, 9U, MinimumMatch>;
        state_type reused{WindowLog, 10U, 9U, MinimumMatch};
        // A large window advances the logical base enough to exercise its
        // overflow-clearing reset path within these six frames. Small windows
        // also exercise prefix clipping while parsing consecutive blocks.
        for (unsigned frame{}; frame < 6U; ++frame)
        {
            auto input{make_input(8192U)};
            for (std::size_t index{frame}; index < input.size(); index += 113U)
                input[index] ^= static_cast<std::uint8_t>(frame + 1U);
            state_type fresh{WindowLog, 10U, 9U, MinimumMatch};
            for (std::size_t begin{}; begin < input.size(); begin += 1024U)
            {
                auto const expected{fresh.parse(input, begin, 1024U)};
                auto const actual{reused.parse(input, begin, 1024U)};
                check(actual.sequences == expected.sequences &&
                    actual.trailing_literal_position == expected.trailing_literal_position &&
                    actual.trailing_literal_length == expected.trailing_literal_length,
                    "double-fast reset indices preserve fresh-state block parsing");
            }
            reused.reset();
        }
    }

    template <unsigned MinimumMatch>
    void check_double_fast_tag_storage()
    {
        using namespace sph::zstd::detail;
        auto before{cache_aligned_allocation_count};
        std::size_t overhead{};
        {
            match_table long_positions, short_positions;
            row_tag_table long_tags, short_tags;
            overhead = cache_aligned_allocation_count - before;
        }
        using state_type = double_fast_match_state<30U, 10U, 9U, MinimumMatch>;
        auto const expected{overhead + 4U};
        before = cache_aligned_allocation_count;
        auto const released{cache_aligned_deallocation_count};
        {
            state_type state{30U, 10U, 9U, MinimumMatch == 0U ? 5U : MinimumMatch};
            check(cache_aligned_allocation_count == before + expected,
                "double-fast allocates its four table buffers at construction");
            auto const input{make_input(8192U)};
            parsed_block scratch;
            scratch.sequences.reserve(input.size() / 3U);
            for (unsigned frame{}; frame < 6U; ++frame)
            {
                scratch = state.parse(input, 0U, input.size(), std::move(scratch));
                state.reset();
            }
            check(cache_aligned_allocation_count == before + expected,
                "double-fast table storage is reused across parsing and index-wrap resets");
        }
        check(cache_aligned_deallocation_count == released + expected,
            "double-fast releases exactly its allocated table storage");
    }

    void test_double_fast_tag_transitions()
    {
        using namespace sph::zstd::detail;
        double_fast_match_state<20U, 17U, 16U, 5U> automatic{20U, 17U, 16U, 5U};
        // Runtime minimum-match selection uses the always-maintained tag path.
        double_fast_match_state<20U, 17U, 16U, 0U> tagged{20U, 17U, 16U, 5U};
        for (unsigned seed{1U}; seed <= 3U; ++seed)
        {
            auto input{sph::zstd::eval::make_tag_transition_corpus(seed)};
            if (seed % 2U == 0U)
                std::rotate(input.begin(), input.begin() + 131072U, input.end());
            for (std::size_t begin{}; begin < input.size(); begin += 131072U)
            {
                auto const actual{automatic.parse(input, begin, 131072U)};
                auto const expected{tagged.parse(input, begin, 131072U)};
                check(actual.sequences == expected.sequences &&
                    actual.trailing_literal_position == expected.trailing_literal_position &&
                    actual.trailing_literal_length == expected.trailing_literal_length,
                    "double-fast mode transitions preserve match history across blocks and resets");
            }
            automatic.reset();
            tagged.reset();
        }
    }

    void test_packed_double_fast_storage()
    {
#if defined(_MSC_VER)
        using namespace sph::zstd::detail;
        auto before{cache_aligned_allocation_count};
        std::size_t overhead{};
        {
            packed_match_table packed_long, packed_short;
            overhead = cache_aligned_allocation_count - before;
        }
        auto const expected{overhead + 2U};
        before = cache_aligned_allocation_count;
        auto const released{cache_aligned_deallocation_count};
        {
            double_fast_match_state<20U, 17U, 16U, 5U> state{20U, 17U, 16U, 5U};
            check(cache_aligned_allocation_count == before + expected,
                "packed double-fast allocates only two table buffers");
            auto const input{make_input(8192U)};
            parsed_block scratch;
            scratch.sequences.reserve(input.size() / 3U);
            for (unsigned frame{}; frame < 6U; ++frame)
            {
                scratch = state.parse(input, 0U, input.size(), std::move(scratch));
                state.reset();
            }
            check(cache_aligned_allocation_count == before + expected,
                "packed double-fast reuses both table buffers across resets");
        }
        check(cache_aligned_deallocation_count == released + expected,
            "packed double-fast releases only its two table buffers");
#endif
    }

    void test_double_fast_reset_indices()
    {
        check_double_fast_reset_indices<10U, 4U>();
        check_double_fast_reset_indices<10U, 5U>();
        check_double_fast_reset_indices<10U, 6U>();
        check_double_fast_reset_indices<30U, 4U>();
        check_double_fast_reset_indices<30U, 5U>();
        check_double_fast_reset_indices<30U, 6U>();
    }

    void test_fast_four_byte_specialization()
    {
        sph::zstd::detail::fast_match_state<14U, 15U, 4U, 0U> specialized{14U, 15U, 4U, 0U};
        sph::zstd::detail::fast_match_state<14U, 15U, 0U, 0U> dynamic{14U, 15U, 4U, 0U};
        for (unsigned frame{}; frame < 6U; ++frame)
        {
            auto input{make_input(16384U)};
            for (std::size_t index{frame}; index < input.size(); index += 97U)
                input[index] ^= static_cast<std::uint8_t>(frame * 41U + 1U);
            for (std::size_t begin{}; begin < input.size(); begin += 4096U)
            {
                auto const expected{dynamic.parse(input, begin, 4096U)};
                auto const actual{specialized.parse(input, begin, 4096U)};
                check(actual.sequences == expected.sequences &&
                    actual.trailing_literal_position == expected.trailing_literal_position &&
                    actual.trailing_literal_length == expected.trailing_literal_length,
                    "four-byte fast specialization matches runtime hash selection");
            }
            specialized.reset();
            dynamic.reset();
        }
    }

    void test_double_fast_four_byte_specialization()
    {
        sph::zstd::detail::double_fast_match_state<18U, 16U, 16U, 4U> specialized{
            18U, 16U, 16U, 4U};
        sph::zstd::detail::double_fast_match_state<18U, 16U, 16U, 0U> dynamic{
            18U, 16U, 16U, 4U};
        for (unsigned frame{}; frame < 6U; ++frame)
        {
            auto input{make_input(16384U)};
            for (std::size_t index{frame}; index < input.size(); index += 97U)
                input[index] ^= static_cast<std::uint8_t>(frame * 41U + 1U);
            for (std::size_t begin{}; begin < input.size(); begin += 4096U)
            {
                auto const expected{dynamic.parse(input, begin, 4096U)};
                auto const actual{specialized.parse(input, begin, 4096U)};
                check(actual.sequences == expected.sequences &&
                    actual.trailing_literal_position == expected.trailing_literal_position &&
                    actual.trailing_literal_length == expected.trailing_literal_length,
                    "four-byte double-fast specialization matches runtime hash selection");
            }
            specialized.reset();
            dynamic.reset();
        }
    }

    void test_reference_interoperability()
    {
        // Adapted from the reference zstreamtest round-trip and tiny-chunk cases.
        auto const input{make_input(257)};
        auto const encoded{compress<checked_content>(input)};
        auto const one_shot_encoded{compress_frame<checked_content>(input)};
        check(one_shot_encoded == encoded, "one-shot and streaming compression produce identical frames");
        check(ZSTD_getFrameContentSize(encoded.data(), encoded.size()) == input.size(),
            "pledged content size is present in the frame header");
        check(reference_decompress(encoded, input.size()) == input,
            "reference zstd round-trips a checksummed frame");

        // Keep the reciprocal interoperability guarantee beside the zstd++ ->
        // reference check above.  The reference encoder's entropy-coded frame is
        // deliberately supplied in small pieces to exercise the streaming decoder.
        auto const reference_encoded{reference_compress(input, 3)};
        check(cpp_decompress(reference_encoded, input.size(), 7U) == input,
            "zstd++ decodes a frame emitted by reference zstd");

        std::vector<std::uint8_t> decoded;
        auto decompressor = sph::zstd::zstd_decompress{
            [&decoded](std::span<std::uint8_t const> output)
            {
                decoded.insert(decoded.end(), output.begin(), output.end());
            }};
        for (auto byte : encoded)
        {
            decompressor.update(byte);
        }
        decompressor.finish();
        check(decoded == input, "byte-at-a-time decompression round-trips");
        check(decompressor.frame_count() == 1, "decompressor counts completed frames");
        check(decompressor.last_frame().checksum, "decompressor exposes frame checksum flag");
        check(decompressor.last_frame().content_size == input.size(), "decompressor exposes content size");

        std::vector<std::uint8_t> direct_decoded;
        auto direct_decompressor = sph::zstd::zstd_decompress{
            [&direct_decoded](std::span<std::uint8_t const> output)
            {
                direct_decoded.insert(direct_decoded.end(), output.begin(), output.end());
            }};
        direct_decompressor.decompress_frame(encoded);
        check(direct_decoded == input, "zero-copy one-shot decompression round-trips");

        std::vector<std::uint8_t> direct_buffer(input.size());
        auto buffer_decompressor = sph::zstd::zstd_decompress{
            [](std::span<std::uint8_t const>) {}};
        buffer_decompressor.decompress_frame(encoded, direct_buffer);
        check(direct_buffer == input, "caller-buffer one-shot decompression round-trips");
    }

    void test_huffman_selection_history()
    {
        sph::zstd::detail::compression_workspace baseline, selected;
        baseline.reserve(8192U, 8192U);
        selected.reserve(8192U, 8192U);
        unsigned random{0x198123U};
        std::vector<std::uint8_t> input, original, candidate;
        for (unsigned trial{}; trial < 4096U; ++trial)
        {
            if (trial % 31U == 0U)
            {
                baseline.reset_entropy_history();
                selected.reset_entropy_history();
            }
            auto const size{16U + (trial * 149U) % 4096U};
            input.resize(size);
            for (auto& byte : input)
            {
                random = random * 1664525U + 1013904223U;
                auto const value{random >> 24U};
                byte = static_cast<std::uint8_t>(trial % 5U == 0U ? value :
                    (value < 160U ? trial % 13U : value % (3U + trial % 61U)));
            }
            auto const old_result{sph::zstd::detail::encode_huffman_literals<false, false>(
                input, baseline, original, trial % 3U == 0U, trial % 7U == 0U ? size : 0U)};
            auto const new_result{sph::zstd::detail::encode_huffman_literals<false, true>(
                input, selected, candidate, trial % 3U == 0U, trial % 7U == 0U ? size : 0U)};
            check(old_result == new_result && original == candidate,
                "Huffman preselection preserves legacy bytes and minimum-gain rejection");
            check(baseline.huffman_repeat_lengths == selected.huffman_repeat_lengths &&
                baseline.huffman_repeat_valid == selected.huffman_repeat_valid &&
                baseline.huffman_repeat_log == selected.huffman_repeat_log &&
                baseline.huffman_repeat_maximum_symbol == selected.huffman_repeat_maximum_symbol,
                "Huffman preselection preserves entropy history across resets and distributions");
        }
    }

    void test_sequence_partition_views()
    {
        sph::zstd::detail::parsed_block decisions;
        decisions.sequences.resize(600U, {0U, 3U, 1U, 0U});
        decisions.trailing_literal_position = 1800U;
        auto const divided{sph::zstd::detail::derive_sequence_partitions(decisions,
            [](sph::zstd::detail::parsed_block_view const& range)
            {
                return range.sequences.size() * range.sequences.size();
            })};
        check(divided.count == 4U && divided.ends[0] == 150U && divided.ends[1] == 300U &&
            divided.ends[2] == 450U && divided.ends[3] == 600U,
            "split recursion uses midpoints and stops below 300 sequences");
        auto const unsplit{sph::zstd::detail::derive_sequence_partitions(decisions,
            [](sph::zstd::detail::parsed_block_view const& range) { return range.sequences.size(); })};
        check(unsplit.count == 1U && unsplit.ends[0] == 600U,
            "equal estimated cost retains the unsplit sequence block");
        sph::zstd::detail::parsed_block parsed{
            {{4U, 10U, 1U, 0U}, {2U, 12U, 1U, 0U}, {3U, 20U, 1U, 0U}},
            0U, 51U, 5U};
        sph::zstd::detail::parsed_block_view const full{parsed};
        auto const first{full.subblock(0U, 1U)};
        auto const last{full.subblock(1U, 3U)};
        check(first.sequences.data() == parsed.sequences.data() &&
            last.sequences.data() == parsed.sequences.data() + 1U,
            "sequence partitions borrow the original storage");
        check(first.input_begin == 0U && first.trailing_literal_position == 14U &&
            first.trailing_literal_length == 0U && last.input_begin == 14U &&
            last.trailing_literal_position == 51U && last.trailing_literal_length == 5U,
            "only the final sequence partition owns the trailing literals");
        auto const middle{last.subblock(0U, 1U)};
        auto const tail{last.subblock(1U, 2U)};
        check(middle.input_begin == 14U && middle.trailing_literal_position == 28U &&
            middle.trailing_literal_length == 0U && tail.input_begin == 28U &&
            tail.trailing_literal_position == 51U && tail.trailing_literal_length == 5U,
            "nested sequence partitions preserve absolute input positions");
        auto const empty{full.subblock(1U, 1U)};
        auto const literals_only{full.subblock(3U, 3U)};
        check(empty.sequences.empty() && empty.input_begin == 14U &&
            empty.trailing_literal_length == 0U && literals_only.sequences.empty() &&
            literals_only.input_begin == 51U && literals_only.trailing_literal_length == 5U,
            "empty interior and final partitions retain correct literal ownership");

        std::vector<std::uint8_t> const input(56U, 42U);
        // Single-segment frame, known 56-byte content size, no checksum.
        std::vector<std::uint8_t> frame{0x28U, 0xB5U, 0x2FU, 0xFDU, 0x20U, 56U};
        sph::zstd::detail::compression_workspace workspace;
        workspace.reserve(input.size(), parsed.sequences.size());
        auto const first_estimate{sph::zstd::detail::estimate_partition_size(input, first, workspace)};
        auto const last_estimate{sph::zstd::detail::estimate_partition_size(input, last, workspace)};
        auto const whole_estimate{sph::zstd::detail::estimate_partition_size(input, full, workspace)};
        check(first_estimate && last_estimate && whole_estimate &&
            first_estimate == sph::zstd::detail::estimate_partition_size(input, first, workspace),
            "partition cost is independent of intervening trial estimates");
        check(std::ranges::none_of(workspace.sequence_histories,
            [](auto const& history) { return history.valid; }) && !workspace.huffman_repeat_valid,
            "complete partition estimates do not commit entropy history");
        std::vector<std::uint8_t> payload;
        for (auto const partition : {first, last})
        {
            sph::zstd::detail::encode_sequences_block<true>(input, partition, workspace, payload);
            check(!payload.empty(), "partition encoder produces a sequence payload");
            auto const header{(static_cast<std::uint32_t>(payload.size()) << 3U) | 4U |
                (partition.trailing_literal_length != 0U ? 1U : 0U)};
            for (unsigned shift : {0U, 8U, 16U})
                frame.push_back(static_cast<std::uint8_t>(header >> shift));
            frame.insert(frame.end(), payload.begin(), payload.end());
        }
        check(reference_decompress(frame, input.size()) == input,
            "reference decodes sequence views emitted as consecutive compressed blocks");
        check(cpp_decompress(frame, input.size(), 7U) == input,
            "zstd++ decodes sequence views emitted as consecutive compressed blocks");
    }

    void test_partition_repeat_reconciliation()
    {
        for (bool const no_literals : {false, true})
        {
            for (std::uint8_t code{1U}; code <= 3U; ++code)
            {
                std::array<std::uint32_t, 3> encoder{10U, 20U, 30U};
                std::array<std::uint32_t, 3> decoder{1U, 4U, 8U};
                std::array<std::uint32_t, 3> const expected_offsets = no_literals ?
                    std::array<std::uint32_t, 3>{20U, 30U, 9U} :
                    std::array<std::uint32_t, 3>{10U, 20U, 30U};
                std::array<sph::zstd::detail::parsed_sequence, 1> sequences{{
                    {no_literals ? 0U : 1U, 10U, 999U, code}}};
                sph::zstd::detail::reconcile_partition_repeats(sequences, encoder, decoder);
                auto const expected{expected_offsets[code - 1U]};
                check(sequences[0].repeat_code == 0U && sequences[0].offset == expected &&
                    decoder == std::array<std::uint32_t, 3>{expected, 1U, 4U},
                    "divergent repeat codes become explicit offsets, including rep0-minus-one");

                encoder = {10U, 20U, 30U};
                decoder = encoder;
                sequences[0] = {no_literals ? 0U : 1U, 10U, expected, code};
                auto const original{sequences[0]};
                sph::zstd::detail::reconcile_partition_repeats(sequences, encoder, decoder);
                check(sequences[0] == original && encoder == decoder,
                    "matching repeat histories preserve the original repeat encoding");
            }
        }

        // The first parsed match would install distance 10, but emitting its
        // partition as raw data leaves the decoder's repeat history unchanged.
        std::array<std::uint32_t, 3> encoder{1U, 4U, 8U};
        auto decoder{encoder};
        auto const saved_decoder{decoder};
        std::array<sph::zstd::detail::parsed_sequence, 1> raw{{{10U, 10U, 10U, 0U}}};
        sph::zstd::detail::reconcile_partition_repeats(raw, encoder, decoder);
        decoder = saved_decoder;
        sph::zstd::detail::parsed_block compressed{{{1U, 10U, 10U, 1U}}, 20U, 31U, 0U};
        sph::zstd::detail::reconcile_partition_repeats(compressed.sequences, encoder, decoder);
        check(compressed.sequences[0].repeat_code == 0U && compressed.sequences[0].offset == 10U &&
            encoder == decoder,
            "raw partition fallback preserves parser history and repairs the next partition");
        constexpr std::string_view text{"abcdefghijabcdefghijKbcdefghijK"};
        std::vector<std::uint8_t> input(text.begin(), text.end());
        std::vector<std::uint8_t> frame{0x28U, 0xB5U, 0x2FU, 0xFDU, 0x20U, 31U,
            160U, 0U, 0U}; // 20-byte raw block, not last.
        frame.insert(frame.end(), input.begin(), input.begin() + 20);
        sph::zstd::detail::compression_workspace workspace;
        workspace.reserve(input.size(), 1U);
        std::vector<std::uint8_t> payload;
        sph::zstd::detail::encode_sequences_block<true>(input, compressed, workspace, payload);
        auto const header{(static_cast<std::uint32_t>(payload.size()) << 3U) | 5U};
        for (unsigned shift : {0U, 8U, 16U})
            frame.push_back(static_cast<std::uint8_t>(header >> shift));
        frame.insert(frame.end(), payload.begin(), payload.end());
        check(reference_decompress(frame, input.size()) == input,
            "reference decodes repaired repeats following a raw partition");
        check(cpp_decompress(frame, input.size(), 7U) == input,
            "zstd++ decodes repaired repeats following a raw partition");
    }

    void test_huffman_explicit_depths()
    {
        std::array<std::uint64_t, 8192U> scratch{};
        std::array<std::size_t, 257U> reference_table{};
        std::array<std::uint8_t, 512U> reference_description{};
        sph::zstd::detail::compression_workspace workspace;
        workspace.reserve(1024U, 1U);
        for (auto const alphabet : {2U, 7U, 32U, 129U, 256U})
        {
            for (unsigned pattern{}; pattern < 3U; ++pattern)
            {
                std::array<std::uint32_t, 256> counts{};
                std::size_t total{};
                unsigned cardinality{};
                for (unsigned symbol{}; symbol < alphabet; ++symbol)
                {
                    counts[symbol] = pattern == 0U ? 8U :
                        pattern == 1U ? (symbol == 0U ? 10000U : 1U + symbol % 5U) :
                        (symbol != 0U && symbol % 3U == 0U && symbol + 1U != alphabet ?
                            0U : 1U + symbol % 17U);
                    total += counts[symbol];
                    cardinality += counts[symbol] != 0U ? 1U : 0U;
                }
                auto const minimum_depth{static_cast<unsigned>(std::bit_width(cardinality - 1U))};
                auto const reference_optimal{HUF_optimalTableLog(11U, total, alphabet - 1U,
                    scratch.data(), sizeof(scratch), reference_table.data(), counts.data(), 1 << 1)};
                check(sph::zstd::detail::optimal_huffman_depth(
                    std::span<std::uint32_t const>{counts}.first(alphabet), total, workspace) == reference_optimal,
                    "optimal Huffman depth matches reference serialized-tree cost search");
                for (auto depth{minimum_depth}; depth <= 11U; ++depth)
                {
                    auto const reference_depth{HUF_buildCTable_wksp(reference_table.data(), counts.data(),
                        alphabet - 1U, depth, scratch.data(), sizeof(scratch))};
                    check(ZSTD_isError(reference_depth) == 0U, "reference builds requested Huffman depth");
                    if (ZSTD_isError(reference_depth) != 0U) continue;
                    auto const candidate{sph::zstd::detail::build_reference_huffman_lengths(
                        std::span<std::uint32_t const>{counts}.first(alphabet), total, depth)};
                    check(candidate.second == reference_depth, "explicit Huffman depth matches reference");
                    for (unsigned symbol{}; symbol < alphabet; ++symbol)
                        check(candidate.first[symbol] == HUF_getNbBitsFromCTable(reference_table.data(), symbol),
                            "every explicit-depth Huffman code length matches reference");
                    auto const reference_size{HUF_writeCTable_wksp(reference_description.data(),
                        reference_description.size(), reference_table.data(), alphabet - 1U,
                        static_cast<unsigned>(reference_depth), scratch.data(), sizeof(scratch))};
                    auto const written{sph::zstd::detail::write_huffman_tree_description(
                        std::span<std::uint8_t const>{candidate.first}.first(alphabet), candidate.second,
                        workspace, workspace.huffman_body)};
                    check(written == (ZSTD_isError(reference_size) == 0U),
                        "Huffman tree serialization support matches reference");
                    if (written && ZSTD_isError(reference_size) == 0U)
                        check(std::ranges::equal(workspace.huffman_body,
                            std::span<std::uint8_t const>{reference_description}.first(reference_size)),
                            "serialized Huffman tree bytes match reference at each depth");
                }
            }
        }
    }

    void test_partition_literal_estimation()
    {
        using namespace sph::zstd::detail;
        compression_workspace workspace;
        workspace.reserve(4096U, 1U);
        std::vector<std::uint8_t> literals(4096U, 0U);
        std::fill(literals.begin() + 3000, literals.end(), std::uint8_t{1U});
        auto const first{estimate_partition_literals(literals, workspace)};
        check(first && first->selection.mode == sequence_table_mode::compressed &&
            first->selection.description_size == 2U && first->section_size == 524U,
            "integrated literal estimator builds and costs a binary Huffman tree");
        check(!workspace.huffman_repeat_valid,
            "tentative literal table does not become committed history");
        workspace.huffman_repeat_valid = true;
        workspace.huffman_repeat_lengths[0] = workspace.huffman_repeat_lengths[1] = 1U;
        workspace.huffman_repeat_log = 1U;
        workspace.huffman_repeat_maximum_symbol = 1U;
        auto const saved_lengths{workspace.huffman_repeat_lengths};
        auto const repeated{estimate_partition_literals(literals, workspace)};
        check(repeated && repeated->selection.mode == sequence_table_mode::repeat &&
            repeated->section_size == 522U,
            "integrated literal estimator prefers the committed tree without a description");
        std::fill(literals.begin() + 3000, literals.end(), std::uint8_t{2U});
        auto const changed{estimate_partition_literals(literals, workspace)};
        check(changed && changed->selection.mode == sequence_table_mode::compressed &&
            workspace.huffman_repeat_lengths == saved_lengths && workspace.huffman_repeat_valid &&
            workspace.huffman_repeat_log == 1U && workspace.huffman_repeat_maximum_symbol == 1U,
            "unsupported repeat alphabet uses a tentative tree without altering committed history");
        auto const disabled{estimate_partition_literals(literals, workspace, false, true)};
        check(disabled && disabled->selection.mode == sequence_table_mode::predefined &&
            disabled->section_size == literals.size(), "disabled literal estimate returns raw size");
        std::fill(literals.begin(), literals.end(), std::uint8_t{42U});
        auto const uniform{estimate_partition_literals(literals, workspace)};
        check(uniform && uniform->selection.mode == sequence_table_mode::run_length &&
            uniform->section_size == 1U, "uniform literal estimate bypasses tree construction");
    }

    void test_split_literal_selection()
    {
        using namespace sph::zstd::detail;
        unsigned builds{};
        literal_table_costs costs{20U, 10U, std::nullopt};
        auto build = [&] { ++builds; return costs; };
        check(select_estimated_literal_table(100U, 60U, false, true, build).mode ==
            sequence_table_mode::predefined, "disabled literal compression bypasses table construction");
        check(select_estimated_literal_table(63U, 63U, false, false, build).mode ==
            sequence_table_mode::predefined, "small literal cutoff precedes the RLE test");
        check(select_estimated_literal_table(6U, 6U, true, false, build).mode ==
            sequence_table_mode::predefined, "valid repeat table retains the six-byte cutoff");
        check(select_estimated_literal_table(7U, 7U, true, false, build).mode ==
            sequence_table_mode::run_length, "valid repeat table lowers the RLE decision cutoff");
        check(select_estimated_literal_table(128U, 5U, false, false, build).mode ==
            sequence_table_mode::predefined && builds == 0U,
            "flat literal histograms skip tentative Huffman construction");
        auto selected{select_estimated_literal_table(100U, 60U, false, false, build)};
        check(selected.mode == sequence_table_mode::compressed && selected.description_size == 10U &&
            builds == 1U, "profitable new literal table includes its description");
        costs.repeat_body_size = 30U;
        selected = select_estimated_literal_table(100U, 60U, false, false, build);
        check(selected.mode == sequence_table_mode::repeat && selected.description_size == 0U,
            "equal new and repeat literal costs prefer the committed table");
        costs.repeat_body_size = 31U;
        check(select_estimated_literal_table(100U, 60U, false, false, build).mode ==
            sequence_table_mode::compressed, "strictly smaller new literal table replaces the old one");
        costs = {0U, 88U, 99U};
        check(select_estimated_literal_table(100U, 60U, false, false, build).mode ==
            sequence_table_mode::repeat, "reference near-source-size tree heuristic prefers repeat");
        costs = {90U, 10U, 100U};
        selected = select_estimated_literal_table(100U, 60U, false, false, build);
        check(selected.mode == sequence_table_mode::predefined && selected.description_size == 0U,
            "nonprofitable new and repeat tables fall back to raw literals");
    }

    void test_split_section_estimates()
    {
        using namespace sph::zstd::detail;
        std::array<huffman_code, 2> const codes{{{0U, 1U}, {1U, 3U}}};
        for (auto const size : {0U, 1U, 255U, 256U, 1023U, 1024U, 16383U, 16384U})
        {
            std::array<std::uint32_t, 2> const counts{size / 2U, size - size / 2U};
            auto const body{(counts[0] + 3U * counts[1]) / 8U};
            auto const header{size < 1024U ? 3U : size < 16384U ? 4U : 5U};
            auto const jumps{size < 256U ? 0U : 6U};
            check(estimate_literal_section_size(sequence_table_mode::compressed, size,
                counts, codes, 11U, true) == body + header + jumps + 11U,
                "split literal estimate includes tree, stream table, and reference header thresholds");
            check(estimate_literal_section_size(sequence_table_mode::repeat, size,
                counts, codes, 11U, false) == body + header + jumps,
                "split literal repeat estimate omits tree description");
            check(estimate_literal_section_size(sequence_table_mode::predefined, size,
                {}, {}, 11U, true) == size &&
                estimate_literal_section_size(sequence_table_mode::run_length, size,
                    {}, {}, 11U, true) == 1U,
                "raw and RLE literal estimates follow the reference header omission");
        }
        for (auto const count : {0U, 127U, 128U, 0x7EFFU, 0x7F00U})
        {
            auto const header{count < 128U ? 2U : count < 0x7F00U ? 3U : 4U};
            check(estimate_sequence_section_size(count, 3U, 5U, 7U, 11U, true) ==
                26U + header && estimate_sequence_section_size(count, 3U, 5U, 7U, 11U, false) ==
                15U + header, "split sequence estimate handles count thresholds and table reuse");
        }
    }

    void test_sequence_symbol_estimates()
    {
        using namespace sph::zstd::detail;
        auto const normalized{make_default_normalized_counts(literal_length_default_norm, 6U)};
        auto const table{build_fse_compression_table(normalized)};
        // FSE_CTABLE_SIZE_U32: header + half the state table + two words/symbol.
        std::array<FSE_CTable, 1U + (1U << 5U) + 36U * 2U> reference_table{};
        std::array<unsigned, 1024U> reference_scratch{};
        auto const built{FSE_buildCTable_wksp(reference_table.data(), literal_length_default_norm.data(),
            35U, 6U, reference_scratch.data(), sizeof(reference_scratch))};
        check(FSE_isError(built) == 0U, "reference builds the estimator comparison table");
        symbol_count_workspace histogram;
        for (auto const size : {1U, 7U, 127U, 300U, 1024U})
        {
            std::vector<std::uint8_t> codes;
            std::array<unsigned, 256> counts{};
            std::size_t extra_bits{};
            unsigned maximum{};
            for (unsigned index{}; index < size; ++index)
            {
                auto const code{static_cast<std::uint8_t>((index * 13U + 7U) % 36U)};
                codes.push_back(code);
                ++counts[code];
                maximum = std::max(maximum, static_cast<unsigned>(code));
                extra_bits += literal_length_bits[code];
            }
            for (auto const mode : {sequence_table_mode::predefined,
                sequence_table_mode::compressed, sequence_table_mode::repeat})
            {
                auto const reference_bits{mode == sequence_table_mode::predefined ?
                    ZSTD_crossEntropyCost(literal_length_default_norm.data(), 6U, counts.data(), maximum) :
                    ZSTD_fseBitCost(reference_table.data(), counts.data(), maximum)};
                check(estimate_sequence_symbol_size(codes, {mode, &table}, normalized,
                    literal_length_default_norm, 6U, literal_length_bits, histogram) ==
                    (reference_bits + extra_bits) / 8U,
                    "symbol estimates match reference fixed-point costs and byte truncation");
            }
        }
        std::array<std::uint8_t, 3> const offsets{5U, 5U, 5U};
        check(estimate_sequence_symbol_size(offsets, {sequence_table_mode::run_length, nullptr},
            normalized, offset_default_norm, 5U, {}, histogram) == 1U,
            "RLE offset estimates retain additional bits and truncate to bytes");
        std::array<std::uint8_t, 1> const unsupported{36U};
        check(estimate_sequence_symbol_size(unsupported, {sequence_table_mode::repeat, &table},
            normalized, literal_length_default_norm, 6U, {}, histogram) == 10U,
            "unsupported repeat symbols use reference conservative fallback size");
    }

    void test_predefined_sequence_table_description()
    {
        // A distribution matching the predefined table should select that table
        // after estimating a dynamic header, without emitting the discarded header.
        for (unsigned const multiplier : {1U, 4U})
        {
            std::vector<std::uint8_t> codes;
            auto const& defaults{sph::zstd::detail::literal_length_default_norm};
            for (std::size_t symbol{}; symbol < defaults.size(); ++symbol)
            {
                auto const count{static_cast<unsigned>(defaults[symbol] < 0 ?
                    -defaults[symbol] : defaults[symbol]) * multiplier};
                codes.insert(codes.end(), count, static_cast<std::uint8_t>(symbol));
            }
            sph::zstd::detail::compression_workspace workspace;
            auto const selected{sph::zstd::detail::select_sequence_table<true>(codes,
                defaults, 6U, 9U, true, workspace.table_descriptions[0],
                workspace.sequence_tables[0], workspace.histogram_workspace,
                workspace.sequence_histories[0])};
            check(selected.mode == sph::zstd::detail::sequence_table_mode::predefined,
                "default-distributed sequence codes select the predefined FSE table");
            check(workspace.table_descriptions[0].empty(),
                "predefined FSE mode emits no discarded dynamic table description");
        }
    }

    void test_symbol_histogram_reuse()
    {
        using namespace sph::zstd::detail;
        symbol_count_workspace workspace;
        for (std::size_t alphabet : {1U, 29U, 36U, 53U, 256U})
            for (std::size_t length : {0U, 1U, 2U, 127U, 128U, 129U, 255U, 256U,
                1499U, 1500U, 2048U, 4999U, 5000U, 5001U, 8191U, 8192U, 8193U})
        {
            std::vector<std::uint8_t> codes(length);
            symbol_counts expected{};
            for (std::size_t index{}; index < length; ++index)
            {
                codes[index] = static_cast<std::uint8_t>(index % alphabet);
                ++expected[codes[index]];
            }
            for (auto& stripe : workspace) stripe.fill(0xdeadU);
            check(count_symbols(codes, workspace) == expected,
                "symbol histogram clears poisoned workspace across alphabets and thresholds");
            for (auto& stripe : workspace) stripe.fill(0xbeefU);
            check(count_symbols<128U>(codes, workspace) == expected,
                "early striped sequence histogram matches scalar oracle");
            for (auto& stripe : workspace) stripe.fill(0xcafeU);
            check(count_symbols<8192U>(codes, workspace) == expected,
                "fast literal histogram changes paths without changing counts");
            for (auto& stripe : workspace) stripe.fill(0xf00dU);
            check(count_symbols_dominant_prefix(codes, workspace) == expected,
                "dominant-prefix histogram falls back for varied symbols");
            if (length >= 128U)
            {
                std::ranges::fill(codes, std::uint8_t{7U});
                codes[17] = 13U;
                codes.back() = 19U;
                expected.fill(0U);
                expected[7] = static_cast<std::uint32_t>(length - 2U);
                expected[13] = 1U;
                expected[19] = 1U;
                for (auto& stripe : workspace) stripe.fill(0xdeadU);
                check(count_symbols_dominant_prefix(codes, workspace) == expected,
                    "dominant-prefix histogram counts rare outliers and clears stale state");
            }
        }
    }

    void test_bounded_sequence_histograms()
    {
        using namespace sph::zstd::detail;
        auto const verify = []<std::size_t AlphabetSize>()
        {
            symbol_count_workspace workspace;
            for (auto const length : {0U, 1U, 127U, 128U, 129U, 1500U, 5000U})
            {
                std::vector<std::uint8_t> codes(length);
                symbol_counts expected{};
                for (std::size_t index{}; index < codes.size(); ++index)
                {
                    codes[index] = static_cast<std::uint8_t>((index * 13U + AlphabetSize - 1U) % AlphabetSize);
                    ++expected[codes[index]];
                }
                for (auto& stripe : workspace) stripe.fill(0xbeefU);
                auto const& actual{count_symbols<128U, AlphabetSize>(codes, workspace)};
                check(std::equal(expected.begin(), expected.begin() + AlphabetSize, actual.begin()),
                    "bounded sequence histogram matches full-alphabet counts");
                check(std::all_of(actual.begin() + AlphabetSize, actual.end(),
                    [](auto count) { return count == 0xbeefU; }),
                    "bounded sequence histogram leaves unused counters untouched");
            }
        };
        verify.template operator()<32U>();
        verify.template operator()<36U>();
        verify.template operator()<53U>();
    }

    void test_rle_blocks()
    {
        std::vector<std::uint8_t> const input(400, 0xA5U);
        auto const encoded{compress<small_blocks>(input)};
        check(encoded.size() < input.size(), "RLE blocks reduce repeated input");
        check(reference_decompress(encoded, input.size()) == input, "reference zstd accepts RLE blocks");
    }

    void test_fse_table_replacement()
    {
        sph::zstd::detail::fse_compression_table reused;
        for (unsigned log : {9U, 5U, 6U})
            for (unsigned maximum : {31U, 1U, 7U})
            for (bool low_probability : {false, true})
            {
                std::array<unsigned, 256> counts{};
                std::vector<std::uint8_t> codes(2048U);
                for (std::size_t index{}; index < codes.size(); ++index)
                {
                    codes[index] = static_cast<std::uint8_t>((index % (maximum + 1U)) & ~1U);
                    ++counts[codes[index]];
                }
                // Include two symbols even in the smallest alphabet.
                codes.back() = static_cast<std::uint8_t>(maximum);
                --counts[(2047U % (maximum + 1U)) & ~1U];
                ++counts[maximum];
                auto const normalized{sph::zstd::detail::normalize_counts(counts,
                    codes.size(), maximum, log, low_probability)};
                auto const fresh{sph::zstd::detail::build_fse_compression_table(normalized)};
                reused.states.fill(65535U);
                reused.transforms.fill({-12345, 12345U});
                reused.run_length_symbol = 255U;
                reused = sph::zstd::detail::build_fse_compression_table(normalized);
                check(reused.states == fresh.states && reused.table_log == fresh.table_log &&
                    reused.run_length_symbol == fresh.run_length_symbol &&
                    std::ranges::equal(reused.transforms, fresh.transforms, [](auto const& a, auto const& b)
                    {
                        return a.delta_find_state == b.delta_find_state &&
                            a.delta_number_bits == b.delta_number_bits;
                    }), "FSE table replacement clears poisoned and unused table entries");
                check(sph::zstd::detail::compress_fse_bytes(codes, reused) ==
                    sph::zstd::detail::compress_fse_bytes(codes, fresh),
                    "reused FSE tables preserve encoded bytes across table sizes");
            }
    }

    void test_normalized_count_round_trips()
    {
        for (unsigned maximum_symbol : {1U, 2U, 7U, 31U, 127U, 255U})
        {
            std::array<unsigned, 256> counts{};
            counts[0] = 1U;
            counts[maximum_symbol] = maximum_symbol == 0U ? 1U : 37U;
            if (maximum_symbol > 3U)
            {
                counts[3] = 11U;
            }
            auto const total{std::accumulate(counts.begin(), counts.begin() + maximum_symbol + 1U, 0U)};
            auto const normalized{sph::zstd::detail::normalize_counts(
                std::span<unsigned const>{counts}.first(maximum_symbol + 1U), total,
                maximum_symbol, 8U, false)};
            auto const encoded{sph::zstd::detail::write_normalized_counts(normalized)};
            auto const decoded{sph::zstd::detail::read_normalized_counts(encoded, maximum_symbol)};
            check(decoded.table_log == normalized.table_log, "normalized count table log round-trips");
            check(decoded.maximum_symbol == normalized.maximum_symbol, "normalized count maximum symbol round-trips");
            check(std::equal(decoded.values.begin(), decoded.values.begin() + decoded.maximum_symbol + 1U,
                normalized.values.begin()), "normalized count values round-trip");
        }
    }

    void test_row_hash_primitives()
    {
        std::array<std::uint8_t, 16> bytes{};
        std::iota(bytes.begin(), bytes.end(), std::uint8_t{1});
        constexpr std::uint64_t salt{0x123456789ABCDEF0ULL};
        auto const mixed{sph::zstd::detail::row_hash_bitmix(salt, 8U)};
        auto expected_mix{salt ^ std::rotr(salt, 49U) ^ std::rotr(salt, 24U)};
        expected_mix *= 0x9FB21C651E98DF25ULL;
        expected_mix ^= (expected_mix >> 35U) + 8U;
        expected_mix *= 0x9FB21C651E98DF25ULL;
        expected_mix ^= expected_mix >> 28U;
        check(mixed == expected_mix, "row hash bit mixer matches reference formula");
        auto value32{std::uint32_t{}};
        auto value64{std::uint64_t{}};
        std::memcpy(&value32, bytes.data(), sizeof(value32));
        std::memcpy(&value64, bytes.data(), sizeof(value64));
        check(sph::zstd::detail::row_hash4(bytes.data(), 20U, 0xA5A5A5A5U) ==
                (((value32 * 2654435761U) ^ 0xA5A5A5A5U) >> 12U),
            "salted row hash 4 matches reference formula");
        check(sph::zstd::detail::row_hash5(bytes.data(), 20U, salt) ==
                static_cast<std::uint32_t>((((value64 << 24U) * 889523592379ULL) ^ salt) >> 44U),
            "salted row hash 5 matches reference formula");
        check(sph::zstd::detail::row_hash6(bytes.data(), 20U, salt) ==
                static_cast<std::uint32_t>((((value64 << 16U) * 227718039650203ULL) ^ salt) >> 44U),
            "salted row hash 6 matches reference formula");
        check(sph::zstd::detail::row_hash7(bytes.data(), 20U, salt) ==
                static_cast<std::uint32_t>((((value64 << 8U) * 58295818150454627ULL) ^ salt) >> 44U),
            "salted row hash 7 matches reference formula");
        check(sph::zstd::detail::row_hash8(bytes.data(), 20U, salt) ==
                static_cast<std::uint32_t>(((value64 * 0xCF1BBCDCB7A56463ULL) ^ salt) >> 44U),
            "salted row hash 8 matches reference formula");
        check(sph::zstd::detail::row_hash(bytes.data(), 20U, 4U, salt) ==
                sph::zstd::detail::row_hash4(bytes.data(), 20U, static_cast<std::uint32_t>(salt)),
            "salted row hash dispatcher selects the minimum-match formula");
        check(sph::zstd::detail::row_hash_fixed<4U>(bytes.data(), 20U, salt) ==
                sph::zstd::detail::row_hash4(bytes.data(), 20U, static_cast<std::uint32_t>(salt)) &&
                sph::zstd::detail::row_hash_fixed<8U>(bytes.data(), 20U, salt) ==
                sph::zstd::detail::row_hash8(bytes.data(), 20U, salt),
            "compile-time salted row hash dispatch selects specialized formulas");
    }

    void test_row_match_table()
    {
        sph::zstd::detail::row_match_table table{10U};
        auto const allocations_after_construction{
            sph::zstd::detail::cache_aligned_allocation_count};
        check((reinterpret_cast<std::uintptr_t>(table.hash_table()) & 63U) == 0U,
            "row table hash storage is cache-line aligned");
        check((reinterpret_cast<std::uintptr_t>(table.tag_table()) & 63U) == 0U,
            "row table tag storage is cache-line aligned");
        auto const hash{(3U << 8U) | 0xA5U};
        check(table.insert(hash, 4U, 11U) == 15U,
            "row table starts insertion at the final slot");
        check(table.insert(hash, 4U, 22U) == 14U,
            "row table insertion cycles backwards");
        auto const row{3U << 4U};
        check(table.hash_table()[row + 15U] == 11U && table.hash_table()[row + 14U] == 22U,
            "row table stores indices in circular slots");
        check(table.tag_table()[row + 15U] == 0xA5U && table.tag_table()[row + 14U] == 0xA5U,
            "row table stores the low hash tag");
        std::array<std::uint32_t, 4> candidates{};
        auto const count{table.collect_candidates(hash, 4U, 0U, 2U, candidates)};
        check(count == 2U && candidates[0] == 22U && candidates[1] == 11U,
            "row table enumerates newest matching candidates first");
        check(table.collect_candidates(hash, 4U, 12U, 2U, candidates) == 1U && candidates[0] == 22U,
            "row table applies the low-limit filter");
        table.reset();
        check(table.tag_table()[row] == 0U && table.tag_table()[row + 14U] == 0U,
            "row table reset clears row heads and tags");
        check(sph::zstd::detail::cache_aligned_allocation_count == allocations_after_construction,
            "row table operations allocate no additional storage");

        for (auto const row_log : {5U, 6U})
        {
            table.reset();
            static_cast<void>(table.insert(hash, row_log, 31U));
            static_cast<void>(table.insert(hash, row_log, 32U));
            static_cast<void>(table.insert(hash, row_log, 33U));
            auto const wide_count{table.collect_candidates(hash, row_log, 0U, 3U, candidates)};
            check(wide_count == 3U && candidates[0] == 33U && candidates[1] == 32U && candidates[2] == 31U,
                "row table preserves newest-first order for wider rows");
        }
    }

    void test_row_hash_cache()
    {
        sph::zstd::detail::row_hash_cache cache;
        cache.fill(4U, 12U, [](std::uint32_t index) { return index * 17U + 3U; });
        check(cache[4U] == 71U && cache[11U] == 190U,
            "row hash cache fills the initial eight positions");
        check(cache.next(4U, [](std::uint32_t index) { return index * 17U + 3U; }) == 71U,
            "row hash cache returns the outgoing entry");
        check(cache[4U] == 207U,
            "row hash cache replaces the slot with the entry eight positions ahead");
        check(cache.next(12U, [](std::uint32_t index) { return index * 17U + 3U; }) == 207U,
            "row hash cache preserves circular slot order");
        cache.reset();
        cache.fill(20U, 22U, [](std::uint32_t index) { return index + 100U; });
        check(cache[20U] == 120U && cache[22U] == 122U,
            "row hash cache fill includes the inclusive end position");
        std::array<std::uint8_t, 32> fixed_bytes{};
        std::iota(fixed_bytes.begin(), fixed_bytes.end(), std::uint8_t{1});
        cache.reset();
        cache.fill_fixed<4U>(0U, 7U, fixed_bytes.data(), 16U, 0U);
        auto const fixed_expected{sph::zstd::detail::row_hash_fixed<4U>(fixed_bytes.data(), 16U, 0U)};
        check(cache[0U] == fixed_expected &&
                cache.next_fixed<4U>(0U, fixed_bytes.data(), 16U, 0U) == fixed_expected,
            "row hash cache compile-time path preserves fill and rollover semantics");
    }

    void test_row_hash_salt_state()
    {
        sph::zstd::detail::row_hash_salt_state state;
        state.observe(0x12345678U);
        state.observe(0x9ABCDEF0U);
        check(state.entropy() == 0xACF13568U,
            "row hash salt state accumulates hash entropy");
        state.reset(false);
        check(state.salt() == sph::zstd::detail::advance_row_hash_salt(0U, 0xACF13568U),
            "row hash salt state advances from cumulative entropy");
        state.observe(7U);
        state.reset(true);
        check(state.salt() == 0U && state.entropy() == 0xACF1356FU,
            "dictionary row hash reset clears salt but preserves entropy");
    }

    void test_row_match_state()
    {
        sph::zstd::detail::row_match_state state{10U, 4U};
        state.update(4U, 6U, [](std::uint32_t index) { static_cast<void>(index); return (3U << 8U) | 0xA5U; });
        std::array<std::uint32_t, 3> candidates{};
        check(state.next_to_update() == 7U && state.search((3U << 8U) | 0xA5U, 0U, 3U, candidates) == 3U,
            "row match state composes update and candidate search");
        check(state.salt() == 0U,
            "row match search records entropy without changing salt until reset");

        state.reset();
        std::uint32_t hash_calls{};
        state.update(0U, 15U, [&hash_calls](std::uint32_t index)
        {
            ++hash_calls;
            static_cast<void>(index);
            return (2U << 8U) | 0x5AU;
        });
        check(state.next_to_update() == 16U && hash_calls == 24U,
            "row match state rolls the eight-entry cache across longer updates");
        // The pointer-based cache primitive replaces hashes eight positions
        // ahead, with up to eight readable bytes at each replacement position.
        std::array<std::uint8_t, 24> fixed_bytes{};
        std::iota(fixed_bytes.begin(), fixed_bytes.end(), std::uint8_t{1});
        state.reset();
        state.update_fixed<4U, 4U>(0U, 7U, fixed_bytes.data(), 12U);
        check(state.search_fixed<4U>(sph::zstd::detail::row_hash_fixed<4U>(
                    fixed_bytes.data(), 12U, state.salt()), 0U, 4U, candidates) != 0U,
            "fixed row match state combines specialized cache and candidate search");
        std::array<std::uint8_t, 32> repeated_bytes{};
        repeated_bytes.fill(0x5AU);
        state.reset();
        auto const fused_count{state.find_fixed<4U, 4U>(8U, 0U, 4U,
            repeated_bytes.data(), 12U, candidates)};
        check(fused_count != 0U && candidates[0] < 8U && state.next_to_update() == 9U,
            "fixed row fused search inserts current position after candidate collection");
    }

    void test_match_counter_boundaries()
    {
        // No trailing padding: exercise every mismatch position and alignment,
        // including short lengths and tails after complete unrolled chunks.
        for (std::size_t alignment{}; alignment < 16U; ++alignment)
        {
            auto const prefix{16U + alignment};
            for (std::size_t length{}; length <= 96U; ++length)
            {
                std::vector<std::uint8_t> left(prefix + length, 0x5AU);
                std::vector<std::uint8_t> right(prefix + length, 0x5AU);
                auto const* const begin{left.data() + prefix};
                auto const* const match{right.data() + prefix};
                for (std::size_t mismatch{}; mismatch <= length; ++mismatch)
                {
                    if (mismatch < length) right[prefix + mismatch] ^= 1U;
                    check(sph::zstd::detail::count_matching_bytes(
                        begin, match, begin + length) == mismatch,
                        "match counter finds the first mismatch at every alignment and tail length");
                    if (mismatch < length) right[prefix + mismatch] ^= 1U;
                }
            }
        }
    }

    void test_row_parser_workspace()
    {
        using namespace sph::zstd::detail;
        row_parser_workspace<true> state{10U};
        std::array<std::uint8_t, 64> input{};
        input.fill(0x5AU);
        std::array<std::uint32_t, 16> candidates{};
        state.fill<4U, 4U>(input, 48U, 14U);
        auto const count{state.find<4U, 4U, true>(input, 8U, 0U, 14U, 8U, false, candidates)};
        check(count == 8U && candidates[0] == 7U && state.next == 9U,
            "production row search updates predecessors and searches before insertion");
        auto const expected_hash{row_hash_fixed<4U>(input.data() + 8U, 14U, state.salt.salt())};
        check(state.salt.entropy() == expected_hash,
            "only current-position searches contribute row salt entropy");
        auto const allocations{cache_aligned_allocation_count};
        state.reset();
        state.fill<4U, 4U>(input, 48U, 14U);
        check(state.find<4U, 4U, true>(input, 0U, 0U, 14U, 8U, false, candidates) == 0U,
            "row frame epoch rejects every previous-frame candidate");
        state.frame_base = std::numeric_limits<std::uint32_t>::max() / 2U;
        state.next = 64U;
        state.reset();
        check(state.frame_base == 0U && state.next == 0U &&
            std::all_of(state.table.hash_table(), state.table.hash_table() + 1024U,
                [](std::uint32_t value) { return value == 0U; }),
            "row index rollover clears stale positions without allocation");
        check(cache_aligned_allocation_count == allocations,
            "row reset and index rollover reuse allocated tables");
        for (std::size_t size{1U}; size <= 64U; ++size)
        {
            auto bytes{make_input(size)};
            greedy_match_state<0U, false, 15U, 10U, 10U, 3U, 4U, true> matcher{
                15U, 10U, 10U, 3U, 4U};
            for (bool repeated : {false, true})
            {
                if (repeated) std::ranges::fill(bytes, std::uint8_t{0x5A});
                matcher.reset();
                auto const parsed{matcher.parse(bytes, 0U, bytes.size())};
                auto consumed{parsed.trailing_literal_length};
                for (auto const& sequence : parsed.sequences)
                    consumed += sequence.literal_length + sequence.match_length;
                check(consumed == size, "row lookahead handles exact-size buffers around its 16-byte cutoff");
            }
        }
    }

    template <unsigned MinimumMatch>
    void check_row_batch_boundaries()
    {
        using namespace sph::zstd::detail;
        auto input{make_input(4096U)};
        row_parser_workspace<true> batched{10U}, scalar{10U};
        std::array<std::uint32_t, 16> actual{}, expected{};
        for (unsigned frame{}; frame < 4U; ++frame)
        {
            if (frame == 2U) std::ranges::fill(input, std::uint8_t{0x5AU});
            if (frame == 3U)
            {
                batched.frame_base = scalar.frame_base =
                    std::numeric_limits<std::uint32_t>::max() / 2U;
            }
            batched.reset();
            scalar.reset();
            batched.fill<MinimumMatch, 4U>(input, 4080U, 14U);
            scalar.fill<MinimumMatch, 4U>(input, 4080U, 14U);
            auto const allocations{cache_aligned_allocation_count};
            for (auto const gap : {0U, 1U, 2U, 3U, 7U, 8U, 9U, 15U, 16U, 17U,
                     95U, 96U, 97U, 383U, 384U, 385U})
            {
                auto const current{batched.next + gap};
                auto const count{batched.find<MinimumMatch, 4U, true>(
                    input, current, 0U, 14U, 15U, false, actual)};
                auto const expected_count{scalar.find<MinimumMatch, 4U, false>(
                    input, current, 0U, 14U, 15U, false, expected)};
                check(count == expected_count && std::equal(actual.begin(),
                    actual.begin() + static_cast<std::ptrdiff_t>(count), expected.begin()),
                    "batch row insertion preserves scalar candidate order at odd/even and skip thresholds");
                check(batched.next == scalar.next && batched.salt.entropy() == scalar.salt.entropy() &&
                    std::equal(batched.table.hash_table(), batched.table.hash_table() + 1024U,
                        scalar.table.hash_table()) &&
                    std::equal(batched.table.tag_table(), batched.table.tag_table() + 1024U,
                        scalar.table.tag_table()),
                    "batch row insertion preserves all table entries across cache wrap and frame rollover");
                for (std::uint32_t slot{}; slot < 8U; ++slot)
                    check(batched.cache[slot] == scalar.cache[slot],
                        "batch row insertion preserves every lookahead cache slot");
            }
            check(cache_aligned_allocation_count == allocations,
                "batch row insertion reuses its tables without allocation");
        }
    }

    void test_one_shot_compression_state()
    {
        auto const input{make_input(257U)};
        std::vector<std::uint8_t> encoded;
        auto compressor = sph::zstd::make_zstd_compress(
            [&encoded](std::span<std::uint8_t const> output)
            {
                encoded.insert(encoded.end(), output.begin(), output.end());
            });
        compressor.update(input.front());
        check_error([&compressor, &input] { compressor.compress_frame(input); },
            sph::zstd::error_code::invalid_state,
            "one-shot compression rejects an active streaming frame");
        compressor.reset();
        compressor.compress_frame(input);
        check_error([&compressor, &input] { compressor.compress_frame(input); },
            sph::zstd::error_code::invalid_state,
            "one-shot compression rejects a finished frame until reset");

        auto checked = sph::zstd::make_zstd_compress<checked_content>(
            [](std::span<std::uint8_t const>) {});
        auto const short_input{std::span<std::uint8_t const>{input}.first(256U)};
        check_error([&checked, short_input] { checked.compress_frame(short_input); },
            sph::zstd::error_code::source_size_mismatch,
            "one-shot compression validates the pledged source size");
    }

    void test_reference_rle_golden_frame()
    {
        // This unmodified reference corpus frame expands eight maximum-size RLE blocks to 1 MiB.
        auto const encoded{read_file(std::filesystem::path{SPH_ZSTDPP_REFERENCE_TEST_DIR} /
            "golden-decompression" / "rle-first-block.zst")};
        std::vector<std::uint8_t> decoded;
        auto decompressor = sph::zstd::zstd_decompress{
            [&decoded](std::span<std::uint8_t const> output)
            {
                decoded.insert(decoded.end(), output.begin(), output.end());
            }};
        for (auto chunk : std::span<std::uint8_t const>{encoded} | std::views::chunk(7))
        {
            decompressor.update(chunk);
        }
        decompressor.finish();
        check(decoded.size() == 1024U * 1024U, "reference RLE golden frame expands to 1 MiB");
        check(std::ranges::all_of(decoded, [](std::uint8_t byte) { return byte == 0; }),
            "reference RLE golden frame expands to zero bytes");
        check(decompressor.last_frame().checksum, "reference RLE golden frame checksum is validated");
    }

    auto decode_reference_golden(std::string_view file_name) -> std::vector<std::uint8_t>
    {
        auto const encoded{read_file(std::filesystem::path{SPH_ZSTDPP_REFERENCE_TEST_DIR} /
            "golden-decompression" / file_name)};
        std::vector<std::uint8_t> decoded;
        auto decompressor = sph::zstd::zstd_decompress{
            [&decoded](std::span<std::uint8_t const> output)
            {
                decoded.insert(decoded.end(), output.begin(), output.end());
            }};
        decompressor.update(encoded);
        decompressor.finish();
        return decoded;
    }

    void test_reference_zero_sequence_golden_frames()
    {
        // These reference files use compressed-block framing but no FSE match sequences.
        check(decode_reference_golden("empty-block.zst").empty(),
            "reference empty compressed block decodes");
        auto const decoded{decode_reference_golden("zeroSeq_2B.zst")};
        constexpr std::string_view expected{"Hello World!\n"};
        check(std::ranges::equal(decoded, expected, {},
            [](std::uint8_t byte) { return static_cast<char>(byte); }, std::identity{}),
            "reference two-byte zero-sequence block decodes raw literals");
    }

    void test_reference_entropy_frames()
    {
        auto const input{make_entropy_input()};
        for (auto const level : {1, 3, 9})
        {
            auto const encoded{reference_compress(input, level)};
            check(encoded.size() < input.size() / 4U, "reference entropy frame is genuinely compressed");

            std::vector<std::uint8_t> decoded;
            auto decompressor = sph::zstd::zstd_decompress{
                [&decoded](std::span<std::uint8_t const> output)
                {
                    decoded.insert(decoded.end(), output.begin(), output.end());
                }};
            for (auto chunk : std::span<std::uint8_t const>{encoded} | std::views::chunk(13))
            {
                decompressor.update(chunk);
            }
            decompressor.finish();
            check(decoded == input, "Huffman/FSE reference frame decodes at each tested level");
            check(decompressor.decoded_size() == input.size(), "entropy decoder reports decoded size");
        }

        auto const mixed_input{make_mixed_entropy_input()};
        auto const mixed_encoded{reference_compress(mixed_input, 1)};
        std::vector<std::uint8_t> mixed_decoded;
        auto mixed_decompressor = sph::zstd::zstd_decompress{
            [&mixed_decoded](std::span<std::uint8_t const> output)
            {
                mixed_decoded.insert(mixed_decoded.end(), output.begin(), output.end());
            }};
        mixed_decompressor.update(mixed_encoded);
        mixed_decompressor.finish();
        check(mixed_decoded == mixed_input,
            "low-probability FSE normalized counts decode in the mixed evaluation corpus");
    }

    void test_huffman_minimum_gain()
    {
        // Literals from the optimal pre-split regression: 117 raw bytes,
        // 114 Huffman payload bytes. Ultra accepts this even though the
        // complete literal section saves only two bytes after headers.
        constexpr std::array<std::uint8_t, 117> literals{
            27,144,26,223,25,64,24,166,16,214,31,110,30,225,29,168,28,75,27,154,
            26,125,25,11,24,7,16,242,31,153,30,209,29,8,28,180,27,71,26,204,
            25,89,24,174,16,192,31,55,30,136,29,15,28,31,27,167,26,16,25,189,
            24,212,16,148,31,138,30,112,29,4,28,145,27,52,26,123,25,238,24,162,
            16,207,31,98,30,68,29,224,28,107,27,169,26,47,25,71,24,24,16,153,
            30,201,29,140,28,65,27,30,17,25,24,27,26,29,28,31,30};
        std::vector<std::uint8_t> input(literals.begin(), literals.end());
        input.insert(input.end(), literals.begin(), literals.begin() + 24U);
        sph::zstd::detail::parsed_block parsed{{{117U, 24U, 117U, 0U}}, 0U, 141U, 0U};
        sph::zstd::detail::compression_workspace workspace;
        workspace.reserve(input.size(), 1U);
        std::vector<std::uint8_t> payload;
        sph::zstd::detail::encode_sequences_block<true, false, false, true, 7U>(
            input, sph::zstd::detail::parsed_block_view{parsed}, workspace, payload);
        check(payload.size() >= 3U && payload[0] == 82U && payload[1] == 135U && payload[2] == 28U,
            "ultra minimum gain compares the Huffman payload without section headers");
        std::vector<std::uint8_t> frame{0x28U, 0xB5U, 0x2FU, 0xFDU, 0x20U, 141U};
        auto const header{(static_cast<std::uint32_t>(payload.size()) << 3U) | 5U};
        for (unsigned shift : {0U, 8U, 16U}) frame.push_back(static_cast<std::uint8_t>(header >> shift));
        frame.insert(frame.end(), payload.begin(), payload.end());
        check(reference_decompress(frame, input.size()) == input,
            "reference decodes the minimum-gain Huffman literal section");

        auto const saved_lengths{workspace.huffman_repeat_lengths};
        auto const saved_frequencies{workspace.huffman_repeat_frequencies};
        std::vector<std::uint8_t> rejected_literals(1024U, 1U);
        for (std::size_t i{}; i < rejected_literals.size(); i += 7U)
            rejected_literals[i] = 2U;
        check(!sph::zstd::detail::encode_huffman_literals(rejected_literals, workspace,
            payload, true, rejected_literals.size()),
            "minimum-gain rejection occurs before committing a new Huffman table");
        check(workspace.huffman_repeat_valid &&
            workspace.huffman_repeat_lengths == saved_lengths &&
            workspace.huffman_repeat_frequencies == saved_frequencies,
            "rejected Huffman candidate preserves the previous emitted table");

        std::vector<std::uint8_t> raw_input(280U);
        for (std::size_t i{}; i < 256U; ++i) raw_input[i] = static_cast<std::uint8_t>(i);
        std::copy_n(raw_input.begin(), 24U, raw_input.begin() + 256U);
        sph::zstd::detail::parsed_block raw_parsed{{{256U, 24U, 256U, 0U}}, 0U, 280U, 0U};
        workspace.reserve(raw_input.size(), 1U);
        sph::zstd::detail::encode_sequences_block<true, false, false, true, 7U>(
            raw_input, sph::zstd::detail::parsed_block_view{raw_parsed}, workspace, payload);
        check((payload.front() & 3U) == 0U && workspace.huffman_repeat_valid &&
            workspace.huffman_repeat_lengths == saved_lengths,
            "raw literal blocks preserve the previously emitted Huffman table");
    }

    void test_cpp_match_compression()
    {
        constexpr std::string_view pattern{"fast-match-finder entropy sequence "};
        std::vector<std::uint8_t> input;
        input.reserve(300U * 1024U);
        while (input.size() < 300U * 1024U)
        {
            input.insert(input.end(), pattern.begin(), pattern.end());
        }
        input.resize(300U * 1024U);
        auto const encoded{compress(input)};
        check(encoded.size() < input.size() / 3U, "fast match finder materially reduces repetitive input");
        check(reference_decompress(encoded, input.size()) == input,
            "reference zstd accepts sph::zstd sequence entropy blocks");

        std::vector<std::uint8_t> decoded;
        auto decompressor = sph::zstd::zstd_decompress{
            [&decoded](std::span<std::uint8_t const> output)
            {
                decoded.insert(decoded.end(), output.begin(), output.end());
            }};
        decompressor.update(encoded);
        decompressor.finish();
        check(decoded == input, "sph::zstd decodes its own sequence entropy blocks");
    }

    void test_huffman_writer_depth_policy()
    {
        constexpr std::string_view phrase{
            "The quick brown fox jumps over the lazy dog. Zstandard C++ evaluation.\n"};
        std::vector<std::uint8_t> literals(phrase.begin(), phrase.end());
        sph::zstd::detail::compression_workspace workspace;
        workspace.reserve(4096U, 1U);
        std::vector<std::uint8_t> fast, optimal, fast_again;
        check(sph::zstd::detail::encode_huffman_literals(literals, workspace, fast),
            "default Huffman depth encodes text literals");
        workspace.reset_entropy_history();
        check(sph::zstd::detail::encode_huffman_literals(literals, workspace, optimal, true),
            "optimal Huffman depth encodes text literals");
        check(optimal.size() + 1U == fast.size() && workspace.huffman_tree_optimal_depth,
            "ultra depth search removes the reference text-first literal section's one-byte gap");
        workspace.reset_entropy_history();
        check(sph::zstd::detail::encode_huffman_literals(literals, workspace, fast_again) &&
            fast_again == fast && !workspace.huffman_tree_optimal_depth,
            "Huffman tree cache distinguishes default and optimal depth policies");
    }

    void test_cpp_huffman_literal_compression()
    {
        std::vector<std::uint8_t> input{1, 2, 3, 4};
        input.reserve(6000U);
        std::uint32_t random{0x12345678U};
        while (input.size() < 5000U)
        {
            random = random * 1664525U + 1013904223U;
            input.push_back(static_cast<std::uint8_t>('a' + (random >> 29U)));
        }
        input.insert(input.end(), input.begin(), input.begin() + 1000);

        auto const encoded{compress(input)};
        check(encoded.size() < 4000U, "Huffman literals reduce a skewed direct-weight alphabet");
        check(reference_decompress(encoded, input.size()) == input,
            "reference zstd accepts sph::zstd Huffman literal output");

        std::vector<std::uint8_t> decoded;
        auto decompressor = sph::zstd::zstd_decompress{
            [&decoded](std::span<std::uint8_t const> output)
            {
                decoded.insert(decoded.end(), output.begin(), output.end());
            }};
        decompressor.update(encoded);
        decompressor.finish();
        check(decoded == input, "sph::zstd decodes its own Huffman literal output");
    }

    void test_full_byte_huffman_literals()
    {
        for (std::size_t size : {255U, 256U, 1023U, 1024U, 4096U})
        {
            std::vector<std::uint8_t> input(size);
            std::uint32_t random{0x12345678U};
            for (auto& byte : input)
            {
                random = random * 1664525U + 1013904223U;
                byte = static_cast<std::uint8_t>(248U + (random >> 29U));
            }
            sph::zstd::detail::compression_workspace workspace;
            workspace.reserve(size, 1U);
            std::vector<std::uint8_t> literals;
            std::vector<std::uint8_t> frame{0x28, 0xB5, 0x2F, 0xFD, 0x00, 0x30};
            auto expected{input};
            for (bool repeat : {false, true})
            {
                auto const encoded{sph::zstd::detail::encode_huffman_literals(
                    input, workspace, literals, false)};
                check(encoded, "Huffman writer supports symbols above 128 using FSE weights");
                if (!encoded) continue;
                check((literals.front() & 3U) == (repeat ? 3U : 2U),
                    "full-byte Huffman tables can be reused");
                if (size < 1024U)
                    check((literals.front() & 12U) == (size < 256U ? 0U : 4U),
                        "four Huffman streams start at 256 literals with a three-byte header");
                if (repeat)
                {
                    frame[6] &= 0xFEU; // The previous block is no longer last.
                    expected.insert(expected.end(), input.begin(), input.end());
                }
                auto const header{static_cast<std::uint32_t>((literals.size() + 1U) << 3U) | 5U};
                frame.push_back(static_cast<std::uint8_t>(header));
                frame.push_back(static_cast<std::uint8_t>(header >> 8U));
                frame.push_back(static_cast<std::uint8_t>(header >> 16U));
                frame.insert(frame.end(), literals.begin(), literals.end());
                frame.push_back(0U); // No sequences: the block consists entirely of literals.
                check(reference_decompress(frame, expected.size()) == expected,
                    "reference decodes full-byte Huffman literals at stream thresholds");
                check(cpp_decompress(frame, expected.size(), 7U) == expected,
                    "C++ decodes full-byte Huffman literals at stream thresholds");
            }
        }
    }

    template <bool RankCanonical, bool DeferredCodes = false>
    void test_canonical_huffman_ranks()
    {
        using namespace sph::zstd::detail;
        for (unsigned depth{1U}; depth <= 11U; ++depth)
        {
            for (bool uniform : {false, true})
            {
                if (uniform && depth > 8U) continue;
                std::array<std::uint8_t, 256> lengths{};
                auto const symbols{uniform ? (1U << depth) : depth + 1U};
                for (unsigned index{}; index < symbols; ++index)
                {
                    // Permutation exercises holes and high-valued symbols.
                    auto const symbol{(index * 73U + 19U) & 255U};
                    lengths[symbol] = static_cast<std::uint8_t>(
                        uniform ? depth : std::min(index + 1U, depth));
                }
                std::vector<std::uint8_t> input;
                std::array<std::uint32_t, 256> frequencies{};
                for (std::size_t symbol{}; symbol < lengths.size(); ++symbol)
                {
                    if (lengths[symbol] == 0U) continue;
                    auto const count{16U << (depth - lengths[symbol])};
                    frequencies[symbol] = count;
                    input.insert(input.end(), count, static_cast<std::uint8_t>(symbol));
                }
                // Pass the low-compressibility gate even for a full alphabet.
                input.insert(input.end(), 1024U, std::uint8_t{19U});
                frequencies[19U] += 1024U;
                auto maximum{255U};
                while (lengths[maximum] == 0U) --maximum;
                compression_workspace workspace;
                workspace.reserve(input.size(), 1U);
                // Seed a valid cached tree to exercise every supported depth,
                // independently of which depth the histogram builder selects.
                workspace.huffman_tree_valid = true;
                workspace.huffman_tree_optimal_depth = false;
                workspace.huffman_tree_lengths = lengths;
                workspace.huffman_tree_log = depth;
                workspace.huffman_tree_frequencies = frequencies;
                workspace.huffman_tree_maximum_symbol = static_cast<std::uint8_t>(maximum);
                workspace.huffman_tree_literal_size = input.size();
                if (uniform && depth == 8U)
                {
                    // A flat 256-symbol tree needs no tree description when
                    // repeated; use that path to isolate canonical assignment.
                    workspace.huffman_repeat_valid = true;
                    workspace.huffman_repeat_lengths = lengths;
                    workspace.huffman_repeat_log = depth;
                    workspace.huffman_repeat_frequencies = frequencies;
                    workspace.huffman_repeat_maximum_symbol = static_cast<std::uint8_t>(maximum);
                    workspace.huffman_repeat_literal_size = input.size();
                }
                std::vector<std::uint8_t> encoded;
                auto const success{encode_huffman_literals<false, DeferredCodes, RankCanonical, DeferredCodes>(
                    input, workspace, encoded, false)};
                check(success, "canonical rank test encodes a valid cached tree");
                if (!success) continue;

                // Independent old algorithm: scan symbols separately for each rank.
                std::array<huffman_code, 256> codes{};
                std::size_t position{};
                for (unsigned weight{1U}; weight <= depth; ++weight)
                {
                    auto const bits{depth + 1U - weight};
                    for (std::size_t symbol{}; symbol < lengths.size(); ++symbol)
                    {
                        if (lengths[symbol] != bits) continue;
                        codes[symbol] = {static_cast<std::uint16_t>(position >> (depth - bits)),
                            static_cast<std::uint8_t>(bits)};
                        position += std::size_t{1} << (weight - 1U);
                    }
                }
                check(position == (std::size_t{1} << depth), "oracle tree fills its code space");
                auto const segment{(input.size() + 3U) / 4U};
                for (std::size_t stream{}; stream < 4U; ++stream)
                {
                    auto const begin{stream * segment};
                    auto const end{std::min(input.size(), begin + segment)};
                    forward_bit_writer scalar;
                    for (auto index{end}; index > begin; --index)
                    {
                        auto const code{codes[input[index - 1U]]};
                        scalar.append_unchecked(code.value, code.number_bits);
                    }
                    check(workspace.huffman_streams[stream] == scalar.finish(),
                        "canonical ranks match scalar construction for sparse/full alphabets and depths 1..11");
                }
            }
        }
    }

    void test_canonical_huffman_history()
    {
        using namespace sph::zstd::detail;
        compression_workspace baseline, selected;
        baseline.reserve(8192U, 8192U);
        selected.reserve(8192U, 8192U);
        unsigned random{0x198123U};
        std::vector<std::uint8_t> input, original, candidate;
        for (unsigned trial{}; trial < 4096U; ++trial)
        {
            if (trial % 31U == 0U)
            {
                baseline.reset_entropy_history();
                selected.reset_entropy_history();
            }
            auto const size{16U + (trial * 149U) % 4096U};
            input.resize(size);
            for (auto& byte : input)
            {
                random = random * 1664525U + 1013904223U;
                auto const value{random >> 24U};
                byte = static_cast<std::uint8_t>(trial % 5U == 0U ? value :
                    (value < 160U ? trial % 13U : value % (3U + trial % 61U)));
            }
            auto const old_result{encode_huffman_literals<false, true, false>(
                input, baseline, original, trial % 3U == 0U, trial % 7U == 0U ? size : 0U)};
            auto const new_result{encode_huffman_literals<false, true, true>(
                input, selected, candidate, trial % 3U == 0U, trial % 7U == 0U ? size : 0U)};
            check(old_result == new_result && original == candidate &&
                baseline.huffman_repeat_lengths == selected.huffman_repeat_lengths &&
                baseline.huffman_repeat_valid == selected.huffman_repeat_valid &&
                baseline.huffman_repeat_log == selected.huffman_repeat_log &&
                baseline.huffman_repeat_maximum_symbol == selected.huffman_repeat_maximum_symbol,
                "rank-based Huffman assignment preserves bytes and repeat history");
        }
    }

    void test_direct_huffman_stream()
    {
        // Packing oracle deliberately uses the old scalar writer. These codes
        // test bit packing, not tree validity (frame tests cover valid trees).
        std::array<sph::zstd::detail::huffman_code, 256> codes{};
        for (std::size_t symbol{}; symbol < codes.size(); ++symbol)
        {
            auto const bits{static_cast<unsigned>(symbol % 11U + 1U)};
            codes[symbol] = {static_cast<std::uint16_t>((symbol * 37U) & ((1U << bits) - 1U)),
                static_cast<std::uint8_t>(bits)};
        }
        sph::zstd::detail::compression_workspace workspace;
        workspace.reserve(128U * 1024U, 1U);
        auto& actual{workspace.huffman_streams[0]};
        auto const capacity{actual.capacity()};
        for (std::size_t length{}; length <= 512U; ++length)
        {
            for (std::size_t phase{}; phase < 11U; ++phase)
            {
                std::vector<std::uint8_t> input(length);
                for (std::size_t index{}; index < length; ++index)
                    input[index] = static_cast<std::uint8_t>(index + phase);
                std::vector<std::uint8_t> expected;
                sph::zstd::detail::forward_bit_writer scalar;
                scalar.reset(expected);
                for (auto position{length}; position > 0U; --position)
                {
                    auto const code{codes[input[position - 1U]]};
                    scalar.append_unchecked(code.value, code.number_bits);
                }
                scalar.finish_into(expected);
                actual.assign((length * 11U + 7U) / 8U + 8U, 0xA5U);
                actual.clear();
                sph::zstd::detail::encode_huffman_stream(input, codes, actual);
                check(actual == expected, "direct Huffman stream matches scalar packing and end marker");
                check(actual.capacity() == capacity, "direct Huffman stream reuses reserved storage");
            }
        }
        for (unsigned const width : {1U, 11U})
        {
            auto const value{static_cast<std::uint16_t>((1U << width) - 1U)};
            codes[0] = {value, static_cast<std::uint8_t>(width)};
            for (auto const length : {4U, 7U, 8U, 255U, 32768U})
            {
                std::vector<std::uint8_t> input(length, 0U);
                std::vector<std::uint8_t> expected;
                sph::zstd::detail::forward_bit_writer scalar;
                scalar.reset(expected);
                for (auto count{length}; count > 0U; --count)
                    scalar.append_unchecked(value, width);
                scalar.finish_into(expected);
                actual.assign((length * 11U + 7U) / 8U + 8U, 0xA5U);
                actual.clear();
                sph::zstd::detail::encode_huffman_stream(input, codes, actual);
                check(actual == expected, "minimum/maximum-length Huffman codes preserve full-stream packing");
                check(actual.capacity() == capacity, "worst-case Huffman stream needs no growth");
            }
        }
    }

    void test_invalid_match_history()
    {
        // One sequence requests a three-byte match at offset four before any output exists.
        std::array<std::uint8_t, 16> const encoded{
            0x28, 0xB5, 0x2F, 0xFD, 0x00, 0x00, // frame and window header
            0x3D, 0x00, 0x00,                   // final compressed block, seven-byte payload
            0x00,                               // zero raw literals
            0x01, 0x54, 0x00, 0x02, 0x00,       // one sequence; RLE LL/OF/ML codes
            0x07                                // offset extra bits 11 and end marker
        };
        auto sink = [](std::span<std::uint8_t const>) {};
        auto decompressor = sph::zstd::zstd_decompress{sink};
        check_error([&decompressor, &encoded] { decompressor.update(encoded); },
            sph::zstd::error_code::invalid_frame,
            "match offsets outside the bounded history window are rejected");
    }

    void test_concatenated_and_skippable_frames()
    {
        auto const first_input{make_input(49)};
        std::vector<std::uint8_t> const second_input(31, 0x7CU);
        auto stream{compress<small_blocks>(first_input)};

        std::array<std::uint8_t, 11> const skippable{
            0x50, 0x2A, 0x4D, 0x18, // skippable-frame magic
            0x03, 0x00, 0x00, 0x00, // payload size
            0x11, 0x22, 0x33
        };
        stream.insert(stream.end(), skippable.begin(), skippable.end());
        auto const second_frame{compress<small_blocks>(second_input)};
        stream.insert(stream.end(), second_frame.begin(), second_frame.end());

        std::vector<std::uint8_t> decoded;
        auto decompressor = sph::zstd::zstd_decompress{
            [&decoded](std::span<std::uint8_t const> output)
            {
                decoded.insert(decoded.end(), output.begin(), output.end());
            }};
        decompressor.update(stream);
        decompressor.finish();

        auto expected{first_input};
        expected.insert(expected.end(), second_input.begin(), second_input.end());
        check(decoded == expected, "concatenated frames decode around a skippable frame");
        check(decompressor.frame_count() == 2, "only data frames contribute to frame count");
    }

    void test_fast_parser_literal_gathering()
    {
        // The reference fast loop starts at 1, probes the repeat at 1+step,
        // then catches back one byte. The uniform shortcut must agree even
        // when minMatch changes, and nondefault skip distances use the loop.
        std::vector<std::uint8_t> const uniform_input(1024U, 42U);
        for (unsigned minimum_match{4U}; minimum_match <= 7U; ++minimum_match)
        {
            for (unsigned target_length : {0U, 1U, 2U, 7U})
            {
                sph::zstd::detail::fast_match_state<> parser{
                    19U, 14U, minimum_match, target_length};
                auto const parsed{parser.parse(uniform_input, 0U, uniform_input.size())};
                auto const literals{target_length == 0U ? 2U : target_length + 1U};
                check(parsed.sequences.size() == 1U &&
                    parsed.sequences.front().literal_length == literals &&
                    parsed.sequences.front().match_length == uniform_input.size() - literals &&
                    parsed.sequences.front().offset == 1U &&
                    parsed.sequences.front().repeat_code == 1U,
                    "uniform fast parsing follows the reference probe/catch-back distance");
            }
        }
        for (auto const size : {8U, 9U, 16U, 31U, 127U, 1024U, 4096U})
        {
            for (bool const uniform : {false, true})
            {
                auto input{make_input(size)};
                if (uniform) std::fill(input.begin(), input.end(), std::uint8_t{42});
                sph::zstd::detail::fast_match_state<19U, 14U, 7U, 0U> parser{19U, 14U, 7U, 0U};
                std::vector<std::uint8_t> storage(size + 16U, 0xA5U);
                for (unsigned iteration{}; iteration < 2U; ++iteration)
                {
                    parser.reset();
                    auto const parsed{parser.parse<true>(input, 0U, input.size(), {}, std::nullopt,
                        std::span<std::uint8_t>{storage}.first(size))};
                    std::vector<std::uint8_t> expected;
                    auto position{parsed.input_begin};
                    for (auto const& sequence : parsed.sequences)
                    {
                        auto const literals{std::span<std::uint8_t const>{input}.subspan(
                            position, sequence.literal_length)};
                        expected.insert(expected.end(), literals.begin(), literals.end());
                        position += sequence.literal_length + sequence.match_length;
                    }
                    auto const tail{std::span<std::uint8_t const>{input}.subspan(
                        parsed.trailing_literal_position, parsed.trailing_literal_length)};
                    expected.insert(expected.end(), tail.begin(), tail.end());
                    check(parsed.sequences.empty() ||
                        std::equal(expected.begin(), expected.end(), storage.begin()),
                        "fast parser gathers literal runs and tail in sequence order");
                    check(std::all_of(storage.begin() + size, storage.end(),
                        [](std::uint8_t value) { return value == 0xA5U; }),
                        "fast parser gathering stays inside supplied storage");
                }
            }
        }
    }

    void test_double_fast_parser_literal_gathering()
    {
        using state_type = sph::zstd::detail::double_fast_match_state<18U, 14U, 14U, 5U>;
        for (auto const size : {7U, 8U, 16U, 127U, 1024U, 4096U, 131072U})
        {
            for (bool const uniform : {false, true})
            {
                auto input{make_input(size)};
                if (uniform) std::fill(input.begin(), input.end(), std::uint8_t{42});
                state_type ordinary{18U, 14U, 14U, 5U};
                state_type collecting{18U, 14U, 14U, 5U};
                std::vector<std::uint8_t> storage(size + 16U, 0xA5U);
                for (unsigned iteration{}; iteration < 2U; ++iteration)
                {
                    auto const expected_block{ordinary.parse(input, 0U, input.size())};
                    auto const collected{collecting.parse_collecting(input, 0U, input.size(), {},
                        std::nullopt, std::span<std::uint8_t>{storage}.first(size))};
                    check(collected.sequences == expected_block.sequences &&
                        collected.trailing_literal_position == expected_block.trailing_literal_position &&
                        collected.trailing_literal_length == expected_block.trailing_literal_length,
                        "collected double-fast parser preserves ordinary match decisions");
                    std::vector<std::uint8_t> expected_literals;
                    auto position{collected.input_begin};
                    for (auto const& sequence : collected.sequences)
                    {
                        auto const literals{std::span<std::uint8_t const>{input}.subspan(
                            position, sequence.literal_length)};
                        expected_literals.insert(expected_literals.end(), literals.begin(), literals.end());
                        position += sequence.literal_length + sequence.match_length;
                    }
                    auto const tail{std::span<std::uint8_t const>{input}.subspan(
                        collected.trailing_literal_position, collected.trailing_literal_length)};
                    expected_literals.insert(expected_literals.end(), tail.begin(), tail.end());
                    check(collected.sequences.empty() ||
                        std::equal(expected_literals.begin(), expected_literals.end(), storage.begin()),
                        "double-fast parser gathers literal runs and trailing bytes in order");
                    check(std::all_of(storage.begin() + size, storage.end(),
                        [](std::uint8_t value) { return value == 0xA5U; }),
                        "double-fast parser gathering respects the supplied storage boundary");
                    ordinary.reset();
                    collecting.reset();
                }
            }
        }
        auto const input{make_input(262144U)};
        state_type ordinary{18U, 14U, 14U, 5U};
        state_type collecting{18U, 14U, 14U, 5U};
        std::vector<std::uint8_t> storage(131072U);
        for (std::size_t begin{}; begin < input.size(); begin += 131072U)
        {
            auto const expected{ordinary.parse(input, begin, 131072U)};
            auto const actual{collecting.parse_collecting(input, begin, 131072U, {},
                std::nullopt, storage)};
            check(actual.sequences == expected.sequences &&
                actual.trailing_literal_position == expected.trailing_literal_position &&
                actual.trailing_literal_length == expected.trailing_literal_length,
                "double-fast collected parser preserves cross-block match history");
        }
#if defined(_MSC_VER)
        using packed_state_type = sph::zstd::detail::double_fast_match_state<20U, 17U, 16U, 5U>;
        packed_state_type packed_ordinary{20U, 17U, 16U, 5U};
        packed_state_type packed_collecting{20U, 17U, 16U, 5U};
        for (std::size_t begin{}; begin < input.size(); begin += 131072U)
        {
            auto const expected{packed_ordinary.parse(input, begin, 131072U)};
            auto const actual{packed_collecting.parse_collecting(input, begin, 131072U, {},
                std::nullopt, storage)};
            check(actual.sequences == expected.sequences &&
                actual.trailing_literal_position == expected.trailing_literal_position &&
                actual.trailing_literal_length == expected.trailing_literal_length,
                "packed double-fast collected parser shares cross-block match history");
        }
#endif
    }

    void test_fast_parser_direct_candidates()
    {
        // A small hash table forces collisions. Compare the minMatch-6
        // specialization with the tagged runtime parser through block/window
        // boundaries and reused histories, not just a fresh single block.
        auto const allocations_before{sph::zstd::detail::cache_aligned_allocation_count};
        sph::zstd::detail::fast_match_state<15U, 10U, 6U, 0U> direct{15U, 10U, 6U, 0U};
        auto const direct_allocations{sph::zstd::detail::cache_aligned_allocation_count -
            allocations_before};
        auto const tagged_allocations_before{sph::zstd::detail::cache_aligned_allocation_count};
        sph::zstd::detail::fast_match_state<> tagged{15U, 10U, 6U, 0U};
        auto const tagged_allocations{sph::zstd::detail::cache_aligned_allocation_count -
            tagged_allocations_before};
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
        check(direct_allocations < tagged_allocations,
            "GCC 13 minMatch-6 fast parser omits tag storage");
#else
        check(direct_allocations == tagged_allocations,
            "other fast parser specializations retain tag storage");
#endif
        auto input{make_input(65536U)};
        for (unsigned iteration{}; iteration < 3U; ++iteration)
        {
            direct.reset();
            tagged.reset();
            for (std::size_t begin{}; begin < input.size(); begin += 8192U)
            {
                auto const actual{direct.parse(input, begin, 8192U)};
                auto const expected{tagged.parse(input, begin, 8192U)};
                check(actual.input_begin == expected.input_begin &&
                    actual.trailing_literal_position == expected.trailing_literal_position &&
                    actual.trailing_literal_length == expected.trailing_literal_length &&
                    actual.sequences.size() == expected.sequences.size(),
                    "direct fast candidates preserve block and tail boundaries");
                if (actual.sequences.size() == expected.sequences.size())
                    for (std::size_t index{}; index < actual.sequences.size(); ++index)
                    {
                        auto const& left{actual.sequences[index]};
                        auto const& right{expected.sequences[index]};
                        check(left.literal_length == right.literal_length &&
                            left.match_length == right.match_length &&
                            left.offset == right.offset && left.repeat_code == right.repeat_code,
                            "direct fast candidates preserve every parsed sequence");
                    }
            }
            for (std::size_t index{iteration}; index < input.size(); index += 97U)
                input[index] ^= 0x5AU;
        }
    }

    void test_optimal_frontier_initialization()
    {
        auto const input{make_input(8192U)};
        sph::zstd::detail::greedy_match_state<2U, true, 15U, 10U, 10U, 5U, 5U> matcher{
            15U, 10U, 10U, 5U, 5U};
        sph::zstd::detail::compression_workspace workspace;
        workspace.reserve(input.size(), input.size() / 3U + 1U);
        workspace.optimal.reserve();
        for (bool const accurate : {false, true})
        {
            matcher.reset();
            workspace.optimal.initialized = false;
            workspace.optimal.repeat_offsets = {1U, 4U, 8U};
            auto const expected{sph::zstd::detail::parse_optimal_block(matcher, input,
                0U, input.size(), workspace, {}, accurate, 48U)};
            check(!expected.sequences.empty(), "frontier regression exercises matched input");
            matcher.reset();
            workspace.optimal.initialized = false;
            workspace.optimal.repeat_offsets = {1U, 4U, 8U};
            for (auto& path : workspace.optimal.paths)
                path = {-1000, 99U, 99U, 99U, {999U, 998U, 997U}};
            auto const actual{sph::zstd::detail::parse_optimal_block(matcher, input,
                0U, input.size(), workspace, {}, accurate, 48U)};
            check(actual.sequences == expected.sequences &&
                actual.trailing_literal_position == expected.trailing_literal_position &&
                actual.trailing_literal_length == expected.trailing_literal_length,
                "frontier initialization prevents stale path data from changing parse results");
        }
    }

    void test_optimal_longest_sufficient_match()
    {
        struct candidate_matcher
        {
            unsigned minimum;
            bool end_match;
            auto optimal_minimum_match() const -> unsigned { return minimum; }
            void begin_optimal_block(std::size_t) const {}
            auto collect_optimal_candidates(std::span<std::uint8_t const>,
                std::size_t position, std::size_t,
                sph::zstd::detail::compression_workspace& workspace,
                std::array<std::uint32_t, 3> const&, bool, std::size_t) const
                -> std::span<sph::zstd::detail::binary_tree_match_candidate const>
            {
                if (position != 16U) return {};
                workspace.optimal.matches[0] = {7U, end_match ? 12U : 49U};
                workspace.optimal.matches[1] = {11U, end_match ? 16U : 60U};
                return std::span<sph::zstd::detail::binary_tree_match_candidate const>{
                    workspace.optimal.matches}.first(2U);
            }
        };
        std::array<std::uint8_t, 96> input{};
        for (unsigned minimum : {3U, 4U})
            for (bool accurate : {false, true})
            for (bool end_match : {false, true})
            {
                candidate_matcher matcher{minimum, end_match};
                auto const source{std::span<std::uint8_t const>{input}.first(end_match ? 32U : 96U)};
                sph::zstd::detail::compression_workspace workspace;
                workspace.reserve(input.size(), input.size() / 3U + 1U);
                workspace.optimal.reserve();
                workspace.optimal.repeat_offsets = {1000U, 1001U, 1002U};
                auto const parsed{sph::zstd::detail::parse_optimal_block(matcher,
                    source, 0U, source.size(), workspace, {}, accurate, 48U)};
                check(!parsed.sequences.empty() &&
                    parsed.sequences.front().literal_length == 16U &&
                    parsed.sequences.front().match_length == (end_match ? 16U : 60U) &&
                    parsed.sequences.front().offset == 8U,
                    end_match ? "optimal parser preserves matches ending beyond hash lookup limit" :
                        "optimal parser selects longest candidate above sufficient length");
                if (end_match)
                    check(parsed.trailing_literal_length == 0U,
                        "block-ending match leaves no artificial literal tail");
            }
    }

    void test_optimal_parser_workspace()
    {
        sph::zstd::detail::optimal_parse_workspace workspace;
        workspace.reserve();
        check(workspace.matches.size() == sph::zstd::detail::optimal_parse_workspace::table_size &&
            workspace.paths.size() == sph::zstd::detail::optimal_parse_workspace::table_size,
            "optimal parser workspace matches the reference 4 KiB path-table contract");
        auto const* const matches{workspace.matches.data()};
        auto const* const paths{workspace.paths.data()};
        workspace.matches.front() = {7U, 11U};
        workspace.paths.front() = {123, 7U, 11U, 3U, {1U, 4U, 8U}};
        workspace.reserve();
        check(workspace.matches.data() == matches && workspace.paths.data() == paths &&
            workspace.matches.front().length == 11U && workspace.paths.front().price == 123,
            "optimal parser workspace reuses stable storage without resetting its path state");
    }

    void test_optimal_price_model()
    {
        check(sph::zstd::detail::optimal_bit_weight(0U) == 0U &&
            sph::zstd::detail::optimal_bit_weight(1U) == 256U &&
            sph::zstd::detail::optimal_fractional_weight(0U) == 256U,
            "optimal price weights match the reference fixed-point definitions");
        sph::zstd::detail::optimal_parse_workspace workspace;
        std::array<std::uint8_t, 16> input{};
        input.fill(42U);
        sph::zstd::detail::initialize_optimal_prices(workspace, input, true);
        check(workspace.initialized && !workspace.predefined_prices &&
            workspace.literal_frequencies[42U] == 1U && workspace.literal_sum == 1U &&
            workspace.literal_length_sum == 40U && workspace.match_length_sum == 53U &&
            workspace.offset_sum == 53U,
            "optimal price initialization uses reference baseline sequence frequencies");
        check(workspace.literal_base_price == sph::zstd::detail::optimal_fractional_weight(1U) &&
            workspace.offset_base_price == sph::zstd::detail::optimal_fractional_weight(53U),
            "optimal price initialization derives base costs from the frequency sums");
        check(sph::zstd::detail::optimal_literal_cost(input, workspace, true) == 4096U &&
            sph::zstd::detail::optimal_literal_length_price(4U, workspace, true) == 1096U &&
            sph::zstd::detail::optimal_match_price(4U, 3U, workspace, true) == 2963U,
            "optimal sequence costs combine literal, length, and offset prices deterministically");
    }

    void test_optimal_cached_match_prices()
    {
        using namespace sph::zstd::detail;
        auto const input{make_input(512U)};
        optimal_parse_workspace workspace;
        for (bool accurate : {false, true})
        {
            workspace.initialized = false;
            initialize_optimal_prices(workspace, input, accurate);
            auto verify = [&]
            {
                for (unsigned code{}; code < 32U; ++code)
                    for (std::size_t symbol{}; symbol < match_length_base.size(); ++symbol)
                    for (std::size_t length : {
                        static_cast<std::size_t>(match_length_base[symbol]),
                        static_cast<std::size_t>(match_length_base[symbol]) +
                            (std::size_t{1} << match_length_bits[symbol]) - 1U})
                    {
                        auto const match_code{match_length_symbol(length)};
                        auto const expected{code * optimal_bitcost_multiplier + workspace.offset_base_price -
                            optimal_weight(workspace.offset_frequencies[code], accurate) +
                            match_length_bits[match_code] * optimal_bitcost_multiplier +
                            workspace.match_length_base_price -
                            optimal_weight(workspace.match_length_frequencies[match_code], accurate) +
                            optimal_bitcost_multiplier / 5U};
                        check(optimal_match_price(std::uint32_t{1} << code, length, workspace, accurate) == expected,
                            "cached match prices equal frequency-derived prices across the symbol alphabet");
                    }
            };
            verify();
            std::array<parsed_sequence, 1> const sequences{{{17U, 60U, 8U, 0U}}};
            update_optimal_statistics(workspace, input, 0U, sequences, accurate);
            verify();
            initialize_optimal_prices(workspace, input, accurate);
            verify();
        }
    }

    void test_optimal_path_relaxation()
    {
        sph::zstd::detail::optimal_parse_workspace workspace;
        workspace.reserve();
        sph::zstd::detail::reset_optimal_paths(workspace);
        workspace.paths[0] = {0, 0U, 0U, 0U, {1U, 4U, 8U}};
        check(sph::zstd::detail::relax_optimal_literal(workspace, 0U, 11U) &&
            workspace.paths[1].price == 11 && workspace.paths[1].literal_length == 1U,
            "optimal path relaxation carries a literal run forward");
        check(sph::zstd::detail::relax_optimal_match(workspace, 1U, 4U, 17U, 23U) &&
            workspace.paths[5].price == 34 && workspace.paths[5].match_length == 4U &&
            workspace.paths[5].offset_base == 17U && workspace.paths[5].literal_length == 0U,
            "optimal path relaxation stores a cheaper match predecessor");
        check(!sph::zstd::detail::relax_optimal_match(workspace, 1U, 4U, 19U, 24U) &&
            workspace.paths[5].offset_base == 17U,
            "optimal path relaxation retains the lower-cost predecessor");
        check(sph::zstd::detail::relax_optimal_literal(workspace, 5U, 7U),
            "optimal path tracks literals after a selected match");
        std::vector<sph::zstd::detail::parsed_sequence> sequences;
        auto const trailing{sph::zstd::detail::reconstruct_optimal_sequences(workspace, 6U, sequences)};
        check(sequences.size() == 1U && sequences.front().literal_length == 1U &&
            sequences.front().match_length == 4U && sequences.front().offset == 14U && trailing == 1U,
            "optimal path reconstruction restores sequence and trailing literals");
    }

    void test_optimal_repeat_offsets()
    {
        std::array<std::uint32_t, 3> const initial{10U, 20U, 30U};
        check(sph::zstd::detail::resolve_optimal_offset(initial, 1U, false) == 10U &&
            sph::zstd::detail::resolve_optimal_offset(initial, 2U, false) == 20U &&
            sph::zstd::detail::resolve_optimal_offset(initial, 3U, false) == 30U,
            "optimal repeat codes map rep0/rep1/rep2 when literals precede a match");
        check(sph::zstd::detail::resolve_optimal_offset(initial, 1U, true) == 20U &&
            sph::zstd::detail::resolve_optimal_offset(initial, 2U, true) == 30U &&
            sph::zstd::detail::resolve_optimal_offset(initial, 3U, true) == 9U,
            "zero-literal repeat codes apply the reference rep1/rep2/rep0-minus-one remapping");

        auto repeats{initial};
        sph::zstd::detail::update_optimal_repeat_offsets(repeats, 1U, false);
        check(repeats == initial, "rep0 leaves repeat history unchanged after literals");
        repeats = initial;
        sph::zstd::detail::update_optimal_repeat_offsets(repeats, 2U, false);
        check(repeats == std::array<std::uint32_t, 3>{20U, 10U, 30U},
            "rep1 moves to the front after literals");
        repeats = initial;
        sph::zstd::detail::update_optimal_repeat_offsets(repeats, 3U, true);
        check(repeats == std::array<std::uint32_t, 3>{9U, 10U, 20U},
            "zero-literal rep0-minus-one updates the complete history");
        repeats = initial;
        sph::zstd::detail::update_optimal_repeat_offsets(repeats, 17U, false);
        check(repeats == std::array<std::uint32_t, 3>{14U, 10U, 20U},
            "normal offsets enter repeat history using offset-base encoding");
    }

    void test_prevalidated_match_relaxation()
    {
        using namespace sph::zstd::detail;
        optimal_parse_workspace checked;
        checked.reserve();
        for (auto const position : {std::size_t{0U}, std::size_t{1U},
            optimal_parse_workspace::path_window - 1U})
        {
            for (auto const length : {std::size_t{1U}, checked.paths.size() - position - 1U})
            {
                for (std::uint32_t offset : {1U, 2U, 3U, 17U})
                {
                    for (std::uint32_t literals : {0U, 1U})
                    {
                        for (std::int32_t price : {19, 20, 21})
                        {
                            reset_optimal_paths(checked);
                            checked.paths[position] = {10, 0U, 0U, literals, {10U, 20U, 30U}};
                            checked.paths[position + length].price = price;
                            auto prevalidated{checked};
                            auto const expected{relax_optimal_match(checked, position, length, offset, 10)};
                            auto const actual{relax_optimal_match<false>(prevalidated, position, length, offset, 10)};
                            auto const& left{checked.paths[position + length]};
                            auto const& right{prevalidated.paths[position + length]};
                            check(actual == expected && left.price == right.price &&
                                left.offset_base == right.offset_base && left.match_length == right.match_length &&
                                left.literal_length == right.literal_length && left.repeat_offsets == right.repeat_offsets,
                                "prevalidated relaxation preserves checked boundary, tie and repeat behavior");
                        }
                    }
                }
            }
        }
    }

    void test_optimal_path_solver()
    {
        std::array<std::uint8_t, 64> input{};
        input.fill(42U);
        sph::zstd::detail::optimal_parse_workspace workspace;
        workspace.reserve();
        sph::zstd::detail::initialize_optimal_prices(workspace, input, true);
        std::array<sph::zstd::detail::binary_tree_match_candidate, 1> match{{{17U, 60U}}};
        std::array<std::span<sph::zstd::detail::binary_tree_match_candidate const>, 64> candidates{};
        candidates[1] = match;
        std::vector<sph::zstd::detail::parsed_sequence> sequences;
        auto const trailing{sph::zstd::detail::solve_optimal_path(workspace, input, candidates,
            sequences, true)};
        check(sequences.size() == 1U && sequences.front().literal_length == 1U &&
            sequences.front().match_length == 60U && sequences.front().offset == 14U && trailing == 3U,
            "optimal path solver selects a lower-cost candidate and reconstructs its tail");
    }

    void test_match_state_table_allocations()
    {
        using namespace sph::zstd::detail;
        auto before{cache_aligned_allocation_count};
        std::size_t container_overhead{};
        {
            // Debug standard libraries may allocate iterator proxies even for
            // empty vectors. Measure the four member containers separately.
            match_table hash, hash3, children;
            row_tag_table tags;
            container_overhead = cache_aligned_allocation_count - before;
        }
        before = cache_aligned_allocation_count;
        {
            greedy_match_state<2U, true, 15U, 10U, 10U, 5U, 5U> tree{15U, 10U, 10U, 5U, 5U};
            check(cache_aligned_allocation_count == before + container_overhead + 2U,
                "binary tree allocates only its hash and child tables, not hash-chain tags");
            tree.reset();
            check(cache_aligned_allocation_count == before + container_overhead + 2U,
                "binary-tree reset does not allocate a tag table");
        }
        before = cache_aligned_allocation_count;
        {
            greedy_match_state<2U, true, 15U, 10U, 10U, 5U, 3U> tree{15U, 10U, 10U, 5U, 3U};
            check(cache_aligned_allocation_count == before + container_overhead + 3U,
                "minimum-match-three tree adds only its dedicated HC3 table");
            tree.reset();
        }
        before = cache_aligned_allocation_count;
        {
            greedy_match_state<2U, false, 15U, 10U, 10U, 5U, 5U> chain{15U, 10U, 10U, 5U, 5U};
            check(cache_aligned_allocation_count == before + container_overhead + 3U,
                "hash-chain parser retains its required byte-tag allocation");
            chain.reset();
        }
    }

    void test_optimal_block_boundary_history()
    {
        constexpr std::size_t size{1024U * 1024U};
        constexpr auto parameters = [] {
            sph::zstd::compression_parameters p{};
            p.compression_level = 16;
            p.pledged_source_size = size;
            return p;
        }();
        std::vector<std::uint8_t> input(size), encoded;
        auto compressor = sph::zstd::make_zstd_compress<parameters>(
            [&](std::span<std::uint8_t const> bytes) { encoded.insert(encoded.end(), bytes.begin(), bytes.end()); });
        // Respectively exercise block-start catch-up, the >4KiB traversal
        // cutoff, and tree history maintained while emitting an RLE block.
        for (std::size_t mutation : {131072U, 139263U, 262144U})
        {
            std::fill(input.begin(), input.end(), std::uint8_t{});
            input[mutation] = 1U;
            auto const expected{reference_compress(input, 16)};
            for (bool streaming : {false, true})
            {
                encoded.clear();
                compressor.reset();
                if (streaming)
                {
                    for (auto chunk : std::span<std::uint8_t const>{input} | std::views::chunk(8191U))
                        compressor.update(chunk);
                    compressor.finish();
                }
                else compressor.compress_frame(input);
                check(encoded == expected, "optimal block-boundary tree history matches reference bytes");
                check(reference_decompress(encoded, size) == input,
                    "reference decodes the optimal block-boundary mutation");
            }
        }
    }

    void test_binary_tree_candidate_collection()
    {
        using uncached_tree = sph::zstd::detail::greedy_match_state<2U, true, 15U, 10U, 10U, 5U, 5U>;
        using cached_tree = sph::zstd::detail::greedy_match_state<2U, true, 15U, 10U, 10U, 5U, 5U, false, true>;
        static_assert(sizeof(sph::zstd::detail::adjacent_insertion_cache<true>) == 1536U);
        static_assert(sizeof(cached_tree) == sizeof(uncached_tree) + 1536U);
        std::vector<std::uint8_t> input(4096U);
        for (std::size_t index{}; index < input.size(); ++index)
            input[index] = static_cast<std::uint8_t>(index % 23U);
        uncached_tree uncached{15U, 10U, 10U, 5U, 5U};
        cached_tree cached{15U, 10U, 10U, 5U, 5U};
        for (unsigned frame{}; frame < 2U; ++frame)
        {
            uncached.reset();
            cached.reset();
            for (std::size_t begin{}; begin < input.size(); begin += 2048U)
            {
                uncached.begin_optimal_block(begin);
                cached.begin_optimal_block(begin);
                for (auto position{begin}; position + 8U < begin + 2048U; position += 3U)
                {
                    std::array<sph::zstd::detail::binary_tree_match_candidate, 64> expected{}, actual{};
                    auto const expected_count{uncached.collect_binary_tree_matches(
                        input, position, begin + 2048U, expected)};
                    auto const actual_count{cached.collect_binary_tree_matches(
                        input, position, begin + 2048U, actual)};
                    check(actual_count == expected_count,
                        "adjacent cache preserves candidate count across blocks and resets");
                    for (std::size_t i{}; i < std::min({actual_count, expected_count, actual.size()}); ++i)
                        check(actual[i].length == expected[i].length &&
                            actual[i].offset_base == expected[i].offset_base,
                            "adjacent cache preserves exact binary-tree candidates");
                }
            }
        }
        sph::zstd::detail::greedy_match_state<2U, true, 15U, 10U, 10U, 5U, 5U> matcher{
            15U, 10U, 10U, 5U, 5U};
        std::array<sph::zstd::detail::binary_tree_match_candidate, 64> candidates{};
        std::size_t count{};
        for (std::size_t position{8U}; position + 8U < input.size() && count == 0U; ++position)
            count = matcher.collect_binary_tree_matches(input, position, input.size(), candidates);
        check(count != 0U, "binary tree collector finds candidates in repeated input");
        for (std::size_t index{1U}; index < std::min(count, candidates.size()); ++index)
            check(candidates[index - 1U].length < candidates[index].length &&
                candidates[index].offset_base >= 4U,
                "binary tree collector emits increasing lengths with offset-base encoding");
        sph::zstd::detail::greedy_match_state<2U, true, 15U, 10U, 10U, 5U, 5U> scratch_matcher{
            15U, 10U, 10U, 5U, 5U};
        sph::zstd::detail::compression_workspace workspace;
        workspace.optimal.reserve();
        std::span<sph::zstd::detail::binary_tree_match_candidate const> scratch_candidates;
        for (std::size_t position{8U}; position + 8U < input.size() && scratch_candidates.empty(); ++position)
            scratch_candidates = scratch_matcher.collect_optimal_candidates(input, position, input.size(), workspace);
        check(!scratch_candidates.empty() && scratch_candidates.data() == workspace.optimal.matches.data(),
            "optimal tree collection writes directly into persistent workspace scratch");

        // A sufficient repeat returns before inserting the current tree node.
        // A tree-only long match, on the other hand, suppresses all candidate
        // searches (including repeats) inside its skipped region.
        for (unsigned reset_trial{}; reset_trial < 2U; ++reset_trial)
        {
            scratch_matcher.reset();
            scratch_matcher.begin_optimal_block();
            for (std::size_t position : {23U, 24U})
            {
                auto const repeat{scratch_matcher.collect_optimal_candidates(input, position, input.size(),
                    workspace, std::array<std::uint32_t, 3>{23U, 4U, 8U}, false, 64U)};
                check(repeat.size() == 1U && repeat[0].offset_base == 1U &&
                    repeat[0].length == input.size() - position,
                    "four-byte sufficient repeat is returned without advancing the tree skip frontier");
            }
            scratch_matcher.reset();
            scratch_matcher.begin_optimal_block();
            auto const explicit_match{scratch_matcher.collect_optimal_candidates(input, 23U, input.size(),
                workspace, std::array<std::uint32_t, 3>{1U, 4U, 8U}, false, 64U)};
            check(!explicit_match.empty() && explicit_match.back().offset_base == 26U,
                "four-byte tree candidate encodes the periodic distance explicitly");
            auto const skipped{scratch_matcher.collect_optimal_candidates(input, 24U, input.size(),
                workspace, std::array<std::uint32_t, 3>{23U, 4U, 8U}, false, 64U)};
            check(skipped.empty(), "tree skipped-area guard also suppresses repeat candidates");
        }
    }

    void test_optimal_three_byte_matches()
    {
        constexpr std::string_view text{
            "The quick brown fox jumps over the lazy dog. Zstandard C++ evaluation.\nThe q"};
        std::vector<std::uint8_t> input(text.begin(), text.end());
        sph::zstd::detail::greedy_match_state<2U, true, 15U, 10U, 10U, 5U, 3U> matcher{
            15U, 10U, 10U, 5U, 3U};
        sph::zstd::detail::compression_workspace workspace;
        workspace.optimal.reserve();
        auto const* const scratch{workspace.optimal.matches.data()};
        constexpr std::array<std::uint32_t, 3> repeats{1U, 4U, 8U};
        auto find_text_match = [&]
        {
            matcher.begin_optimal_block();
            for (std::size_t position{}; position < 32U; ++position)
                (void)matcher.collect_optimal_candidates(input, position, input.size(),
                    workspace, repeats, false, 64U);
            auto const matches{matcher.collect_optimal_candidates(input, 32U, input.size(),
                workspace, repeats, false, 64U)};
            check(matches.size() == 1U && matches[0].offset_base == 34U && matches[0].length == 3U,
                "HC3 finds reference's three-byte he-space match at distance31");
        };
        find_text_match();
        matcher.reset();
        find_text_match();
        check(workspace.optimal.matches.data() == scratch,
            "three-byte collection reuses the reserved candidate scratch after reset");

        matcher.reset();
        matcher.begin_optimal_block();
        auto const repeated{matcher.collect_optimal_candidates(input, 32U, input.size(),
            workspace, std::array<std::uint32_t, 3>{31U, 4U, 8U}, false, 64U)};
        check(repeated.size() == 1U && repeated[0].offset_base == 1U && repeated[0].length == 3U,
            "three-byte repeat candidates precede HC3's explicit-offset candidate");

        matcher.reset();
        matcher.begin_optimal_block();
        auto const no_literals{matcher.collect_optimal_candidates(input, 32U, input.size(),
            workspace, std::array<std::uint32_t, 3>{4U, 31U, 8U}, true, 64U)};
        check(no_literals.size() == 1U && no_literals[0].offset_base == 1U && no_literals[0].length == 3U,
            "three-byte repeats honor the zero-literal repcode mapping");

        matcher.reset();
        matcher.begin_optimal_block();
        std::vector<std::uint8_t> unrelated(input.size());
        for (std::size_t index{}; index < unrelated.size(); ++index)
            unrelated[index] = static_cast<std::uint8_t>(128U + index);
        auto const unrelated_matches{matcher.collect_optimal_candidates(unrelated, 32U,
            unrelated.size(), workspace, repeats, false, 64U)};
        check(unrelated_matches.empty(), "HC3 reset excludes stale positions from earlier frames");

        auto check_distance = [&](unsigned window_log, std::size_t distance, bool expected)
        {
            std::vector<std::uint8_t> distant(distance + 16U, 0x99U);
            distant[0] = distant[distance] = 'A';
            distant[1] = distant[distance + 1U] = 'B';
            distant[2] = distant[distance + 2U] = 'C';
            distant[3] = 'q';
            distant[distance + 3U] = 'r';
            sph::zstd::detail::greedy_match_state<2U, true> bounded{
                window_log, 10U, 10U, 5U, 3U};
            bounded.begin_optimal_block();
            auto const matches{bounded.collect_optimal_candidates(distant, distance, distant.size(),
                workspace, repeats, false, 64U)};
            check(expected ? (matches.size() == 1U && matches[0].length == 3U &&
                    matches[0].offset_base == distance + 3U) : matches.empty(),
                "HC3 applies the window bound and exclusive 256-KiB distance cutoff");
        };
        check_distance(5U, 32U, true);
        check_distance(5U, 33U, false);
        check_distance(19U, (1U << 18U) - 1U, true);
        check_distance(19U, 1U << 18U, false);
    }

    void test_direct_output_bounds()
    {
        std::vector<std::uint8_t> input(8192U);
        unsigned random{1234567U};
        for (std::size_t index{}; index < input.size(); ++index)
        {
            random = random * 1664525U + 1013904223U;
            input[index] = static_cast<std::uint8_t>(
                index % 149U == 0U ? random >> 24U : index % 29U);
        }
        auto const frame{reference_compress(input, 3)};
        auto decoder = sph::zstd::zstd_decompress{[](std::span<std::uint8_t const>) {}};
        auto verify = [&](std::span<std::uint8_t const> encoded, std::size_t capacity, bool intact)
        {
            constexpr std::size_t guard{64U};
            std::vector<std::uint8_t> storage(capacity + 2U * guard, 0xA5U);
            auto destination{std::span{storage}.subspan(guard, capacity)};
            decoder.reset();
            bool accepted{};
            try
            {
                decoder.decompress_frame(encoded, destination);
                accepted = true;
            }
            catch (std::exception const&) {}
            check(std::ranges::all_of(std::span{storage}.first(guard),
                [](auto byte) { return byte == 0xA5U; }) &&
                std::ranges::all_of(std::span{storage}.last(guard),
                [](auto byte) { return byte == 0xA5U; }),
                "direct decoder never writes outside the caller-owned output span");
            if (intact)
            {
                check(accepted == (capacity == input.size()),
                    "direct decoder rejects an undersized destination");
                if (accepted) check(std::ranges::equal(destination, input),
                    "direct decoder preserves exact output after reset from an error");
            }
        };
        for (std::size_t capacity : {0U, 1U, 31U, 4096U, 8191U, 8192U})
            verify(frame, capacity, true);
        auto corrupt{frame};
        for (std::size_t position{}; position < corrupt.size(); ++position)
        {
            for (unsigned bit{}; bit < 8U; ++bit)
            {
                corrupt[position] ^= static_cast<std::uint8_t>(1U << bit);
                verify(corrupt, input.size(), false);
                corrupt[position] ^= static_cast<std::uint8_t>(1U << bit);
            }
        }
        verify(frame, input.size(), true);
    }

    void test_error_reporting()
    {
        auto const input{make_input(257)};
        auto encoded{compress<checked_content>(input)};

        auto sink = [](std::span<std::uint8_t const>) {};
        auto truncated = sph::zstd::zstd_decompress{sink};
        truncated.update(std::span<std::uint8_t const>{encoded}.first(encoded.size() - 1U));
        check_error([&truncated] { truncated.finish(); }, sph::zstd::error_code::truncated_input,
            "finish reports truncated content checksum");

        encoded.back() ^= 0x80U;
        auto corrupt = sph::zstd::zstd_decompress{sink};
        check_error([&corrupt, &encoded] { corrupt.update(encoded); }, sph::zstd::error_code::checksum_mismatch,
            "corrupt content checksum is rejected");
    }

    void test_flush_and_reset()
    {
        std::vector<std::uint8_t> encoded;
        auto compressor = sph::zstd::make_zstd_compress<small_blocks>(
            [&encoded](std::span<std::uint8_t const> output)
            {
                encoded.insert(encoded.end(), output.begin(), output.end());
            });
        compressor.update(std::uint8_t{42});
        compressor.flush();
        compressor.update(std::uint8_t{43});
        compressor.finish();
        std::array<std::uint8_t, 2> const expected{42, 43};
        check(reference_decompress(encoded, expected.size()) == std::vector<std::uint8_t>(expected.begin(), expected.end()),
            "flush preserves a valid open frame");

        compressor.reset();
        check(compressor.status() == sph::zstd::stream_status::ready, "reset restores ready state");
    }

    void test_chunked_bit_writer()
    {
        sph::zstd::detail::forward_bit_writer external_writer;
        sph::zstd::detail::forward_bit_writer internal_writer;
        for (std::size_t prefix_size : {0U, 1U, 7U, 255U})
        {
            for (unsigned word_count : {0U, 1U, 63U, 64U, 65U, 127U, 128U})
            {
                std::vector<std::uint8_t> external(prefix_size, 0xA5U);
                external_writer.reset(external);
                internal_writer.reset();
                for (unsigned index{}; index < word_count; ++index)
                {
                    auto const value{0x9E3779B9U ^ (index * 0x85EBCA6BU)};
                    external_writer.append(value, 32U);
                    internal_writer.append(value, 32U);
                    if ((index & 7U) == 0U)
                    {
                        external_writer.append_unchecked(1U, 1U);
                        internal_writer.append_unchecked(1U, 1U);
                    }
                }
                auto const expected{internal_writer.finish()};
                external_writer.finish_into(external);
                check(external.size() == prefix_size + expected.size() &&
                    std::ranges::all_of(std::span<std::uint8_t const>{external}.first(prefix_size),
                        [](std::uint8_t value) { return value == 0xA5U; }) &&
                    std::ranges::equal(std::span<std::uint8_t const>{external}.subspan(prefix_size),
                        expected), "chunked external bit writer preserves prefix and exact payload");
            }
        }
    }

    void test_wide_sequence_bit_writer()
    {
        constexpr std::array<unsigned, 11> widths{0U, 1U, 7U, 8U, 9U, 16U,
            24U, 28U, 31U, 32U, 3U};
        for (std::size_t prefix_size : {0U, 1U, 7U, 255U})
        {
            for (unsigned field_count : {0U, 1U, 31U, 64U, 1024U})
            {
                sph::zstd::detail::forward_bit_writer expected_writer;
                sph::zstd::detail::wide_sequence_bit_writer wide_writer;
                std::vector<std::uint8_t> actual(prefix_size, 0xA5U);
                wide_writer.reset(actual);
                for (unsigned index{}; index < field_count; ++index)
                {
                    auto const width{widths[index % widths.size()]};
                    auto const value{0x9E3779B9U ^ (index * 0x85EBCA6BU)};
                    if ((index & 1U) == 0U)
                    {
                        expected_writer.append(value, width);
                        wide_writer.append(value, width);
                    }
                    else
                    {
                        auto const mask{width == 32U ? std::numeric_limits<std::uint32_t>::max() :
                            (std::uint32_t{1} << width) - 1U};
                        expected_writer.append_unchecked(value & mask, width);
                        wide_writer.append_unchecked(value & mask, width);
                    }
                }
                auto const expected{expected_writer.finish()};
                wide_writer.finish_into(actual);
                check(actual.size() == prefix_size + expected.size() &&
                    std::ranges::all_of(std::span<std::uint8_t const>{actual}.first(prefix_size),
                        [](std::uint8_t value) { return value == 0xA5U; }) &&
                    std::ranges::equal(std::span<std::uint8_t const>{actual}.subspan(prefix_size),
                        expected), "wide sequence bit writer preserves prefix and exact payload");
            }
        }
    }

    void test_packed_length_extra_bits()
    {
        for (unsigned ll_bits{}; ll_bits <= 16U; ++ll_bits)
            for (unsigned ml_bits{}; ml_bits <= 16U; ++ml_bits)
                for (auto const value : {0U, 0xFFFFFFFFU, 0x9E3779B9U})
                {
                    sph::zstd::detail::forward_bit_writer expected;
                    sph::zstd::detail::forward_bit_writer packed;
                    expected.append(5U, 3U);
                    packed.append(5U, 3U);
                    expected.append(value, ll_bits);
                    expected.append(value ^ 0x85EBCA6BU, ml_bits);
                    auto const count{ll_bits + ml_bits};
                    if (count != 0U)
                    {
                        auto const ll_mask{(1U << ll_bits) - 1U};
                        auto const ml_mask{(1U << ml_bits) - 1U};
                        auto const combined{(value & ll_mask) |
                            (((value ^ 0x85EBCA6BU) & ml_mask) << ll_bits)};
                        packed.append_unchecked(combined, count);
                    }
                    expected.append(0x1F3DU, 15U);
                    packed.append(0x1F3DU, 15U);
                    check(expected.finish() == packed.finish(),
                        "packed length extra bits preserve field order and boundaries");
                }
    }
}

int main()
{
    try
    {
        test_literal_workspace_alignment();
        test_double_fast_reset_indices();
        check_double_fast_tag_storage<0U>();
        check_double_fast_tag_storage<4U>();
        check_double_fast_tag_storage<5U>();
        test_packed_double_fast_storage();
        test_double_fast_tag_transitions();
        test_reference_interoperability();
        check_custom_double_fast_frames<4U, false>();
        check_custom_double_fast_frames<4U, true>();
        check_custom_double_fast_frames<6U, false>();
        check_custom_double_fast_frames<6U, true>();
        test_huffman_selection_history();
        test_double_fast_four_byte_specialization();
        test_fast_four_byte_specialization();
        test_sequence_partition_views();
        test_partition_repeat_reconciliation();
        test_sequence_symbol_estimates();
        test_split_section_estimates();
        test_split_literal_selection();
        test_partition_literal_estimation();
        test_huffman_explicit_depths();
        test_predefined_sequence_table_description();
        test_symbol_histogram_reuse();
        test_bounded_sequence_histograms();
        test_rle_blocks();
        test_normalized_count_round_trips();
        test_fse_table_replacement();
        test_row_hash_primitives();
        test_row_match_table();
        test_row_hash_cache();
        test_row_hash_salt_state();
        test_row_match_state();
        test_row_parser_workspace();
        check_row_batch_boundaries<4U>();
        check_row_batch_boundaries<5U>();
        test_match_counter_boundaries();
        test_one_shot_compression_state();
        test_reference_rle_golden_frame();
        test_reference_zero_sequence_golden_frames();
        test_reference_entropy_frames();
        test_huffman_minimum_gain();
        test_cpp_match_compression();
        test_cpp_huffman_literal_compression();
        test_full_byte_huffman_literals();
        test_canonical_huffman_ranks<false>();
        test_canonical_huffman_ranks<true>();
        test_canonical_huffman_ranks<false, true>();
        test_canonical_huffman_ranks<true, true>();
        test_canonical_huffman_history();
        test_direct_huffman_stream();
        test_huffman_writer_depth_policy();
        test_invalid_match_history();
        test_concatenated_and_skippable_frames();
        test_error_reporting();
        test_direct_output_bounds();
        test_fast_parser_literal_gathering();
        test_double_fast_parser_literal_gathering();
        test_fast_parser_direct_candidates();
        test_optimal_parser_workspace();
        test_optimal_frontier_initialization();
        test_optimal_longest_sufficient_match();
        test_optimal_price_model();
        test_optimal_cached_match_prices();
        test_optimal_path_relaxation();
        test_prevalidated_match_relaxation();
        test_optimal_repeat_offsets();
        test_optimal_path_solver();
        test_optimal_block_boundary_history();
        test_binary_tree_candidate_collection();
        test_match_state_table_allocations();
        test_optimal_three_byte_matches();
        test_flush_and_reset();
        test_chunked_bit_writer();
        test_wide_sequence_bit_writer();
        test_packed_length_extra_bits();
    }
    catch (std::exception const& error)
    {
        ++failures;
        std::cerr << "UNEXPECTED EXCEPTION: " << error.what() << '\n';
    }

    if (failures == 0)
    {
        std::cout << "All sph-zstd++ tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
