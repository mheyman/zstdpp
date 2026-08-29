#define ZSTD_STATIC_LINKING_ONLY
#define ZSTD_DISABLE_DEPRECATE_WARNINGS

#include <sph/zstd++/zstd_compress.h>

#include "eval_corpus.h"

#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace
{
    inline constexpr std::size_t corpus_size{1024U * 1024U};

    template <int Level>
    inline constexpr auto parameters = []
    {
        auto value{sph::zstd::compression_parameters{}};
        value.compression_level = Level;
        value.pledged_source_size = corpus_size;
        return value;
    }();

    template <int Level>
    auto compress_cpp(std::span<std::uint8_t const> input) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> encoded;
        auto compressor = sph::zstd::make_zstd_compress<parameters<Level>>(
            [&encoded](std::span<std::uint8_t const> output)
            {
                encoded.insert(encoded.end(), output.begin(), output.end());
            });
        compressor.update(input);
        compressor.finish();
        return encoded;
    }

    auto compress_reference(std::span<std::uint8_t const> input, int level) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> encoded(ZSTD_compressBound(input.size()));
        auto const result{ZSTD_compress(encoded.data(), encoded.size(), input.data(), input.size(), level)};
        if (ZSTD_isError(result) != 0U)
        {
            std::cerr << "reference zstd level " << level << " failed: " << ZSTD_getErrorName(result) << '\n';
            return {};
        }
        encoded.resize(result);
        return encoded;
    }

    auto reference_sequence_blocks(std::span<std::uint8_t const> input, int level)
        -> std::vector<std::vector<ZSTD_Sequence>>
    {
        auto* context{ZSTD_createCCtx()};
        if (context == nullptr)
        {
            throw std::runtime_error{"cannot allocate reference compression context"};
        }
        auto const parameter_result{ZSTD_CCtx_setParameter(context, ZSTD_c_compressionLevel, level)};
        if (ZSTD_isError(parameter_result) != 0U)
        {
            ZSTD_freeCCtx(context);
            throw std::runtime_error{ZSTD_getErrorName(parameter_result)};
        }
        std::vector<ZSTD_Sequence> sequences(ZSTD_sequenceBound(input.size()));
        auto const result{ZSTD_generateSequences(context, sequences.data(), sequences.size(),
            input.data(), input.size())};
        ZSTD_freeCCtx(context);
        if (ZSTD_isError(result) != 0U)
        {
            throw std::runtime_error{ZSTD_getErrorName(result)};
        }
        sequences.resize(result);

        std::vector<std::vector<ZSTD_Sequence>> blocks(1U);
        for (auto const& sequence : sequences)
        {
            if (sequence.offset == 0U && sequence.matchLength == 0U)
            {
                blocks.emplace_back();
            }
            else
            {
                blocks.back().push_back(sequence);
            }
        }
        if (!blocks.empty() && blocks.back().empty())
        {
            blocks.pop_back();
        }
        return blocks;
    }

    auto same_parameters(sph::zstd::compression_parameters const& cpp,
        ZSTD_compressionParameters const& reference) -> bool
    {
        return cpp.window_log == reference.windowLog && cpp.chain_log == reference.chainLog &&
            cpp.hash_log == reference.hashLog && cpp.search_log == reference.searchLog &&
            cpp.minimum_match == reference.minMatch && cpp.target_length == reference.targetLength &&
            static_cast<unsigned>(cpp.strategy) == static_cast<unsigned>(reference.strategy);
    }

    auto cpp_fast_sequence_blocks(std::span<std::uint8_t const> input,
        sph::zstd::compression_parameters const& parameters) -> std::vector<sph::zstd::detail::parsed_block>
    {
        sph::zstd::detail::fast_match_state state{parameters.window_log, parameters.hash_log,
            parameters.minimum_match, parameters.target_length};
        std::vector<sph::zstd::detail::parsed_block> blocks;
        for (std::size_t begin{}; begin < input.size(); begin += sph::zstd::maximum_block_size)
        {
            auto const size{std::min(sph::zstd::maximum_block_size, input.size() - begin)};
            blocks.push_back(state.parse(input, begin, size));
        }
        return blocks;
    }

    auto cpp_double_fast_sequence_blocks(std::span<std::uint8_t const> input,
        sph::zstd::compression_parameters const& parameters) -> std::vector<sph::zstd::detail::parsed_block>
    {
        sph::zstd::detail::double_fast_match_state state{parameters.window_log, parameters.hash_log,
            parameters.chain_log, parameters.minimum_match};
        std::vector<sph::zstd::detail::parsed_block> blocks;
        for (std::size_t begin{}; begin < input.size(); begin += sph::zstd::maximum_block_size)
        {
            auto const size{std::min(sph::zstd::maximum_block_size, input.size() - begin)};
            blocks.push_back(state.parse(input, begin, size));
        }
        return blocks;
    }

    template <unsigned LazyDepth = 0U, bool BinaryTree = false>
    auto cpp_greedy_sequence_blocks(std::span<std::uint8_t const> input,
        sph::zstd::compression_parameters const& parameters)
        -> std::vector<sph::zstd::detail::parsed_block>
    {
        sph::zstd::detail::greedy_match_state<LazyDepth, BinaryTree> state{parameters.window_log,
            parameters.hash_log, parameters.chain_log, parameters.search_log, parameters.minimum_match};
        std::vector<sph::zstd::detail::parsed_block> blocks;
        for (std::size_t begin{}; begin < input.size(); begin += sph::zstd::maximum_block_size)
        {
            auto const size{std::min(sph::zstd::maximum_block_size, input.size() - begin)};
            blocks.push_back(state.parse(input, begin, size));
        }
        return blocks;
    }

    auto same_fast_sequences(std::vector<sph::zstd::detail::parsed_block> const& cpp,
        std::vector<std::vector<ZSTD_Sequence>> const& reference) -> bool
    {
        if (cpp.size() != reference.size())
        {
            return false;
        }
        for (std::size_t block{}; block < cpp.size(); ++block)
        {
            if (cpp[block].sequences.size() != reference[block].size())
            {
                return false;
            }
            for (std::size_t index{}; index < reference[block].size(); ++index)
            {
                auto const& left{cpp[block].sequences[index]};
                auto const& right{reference[block][index]};
                if (left.literal_length != right.litLength || left.match_length != right.matchLength ||
                    left.offset != right.offset || left.repeat_code != right.rep)
                {
                    std::cout << "  first sequence mismatch: block=" << block << " sequence=" << index
                              << " cpp=(ll=" << left.literal_length << ", ml=" << left.match_length
                              << ", off=" << left.offset << ", rep=" << static_cast<unsigned>(left.repeat_code)
                              << ") reference=(ll=" << right.litLength << ", ml=" << right.matchLength
                              << ", off=" << right.offset << ", rep=" << right.rep << ")\n";
                    return false;
                }
            }
        }
        return true;
    }

    template <int Level>
    auto check_level(std::span<std::uint8_t const> input) -> bool
    {
        constexpr auto cpp_parameters{sph::zstd::detail::resolve_parameters(parameters<Level>)};
        auto const reference_parameters{ZSTD_getCParams(Level, input.size(), 0)};
        auto const cpp{compress_cpp<Level>(input)};
        auto const reference{compress_reference(input, Level)};
        auto const reference_sequence_blocks_value{reference_sequence_blocks(input, Level)};
        auto const cpp_sequence_blocks_value{[&]
        {
            if constexpr (Level == 1)
            {
                return cpp_fast_sequence_blocks(input, cpp_parameters);
            }
            else if constexpr (Level == 3)
            {
                return cpp_double_fast_sequence_blocks(input, cpp_parameters);
            }
            else if constexpr (Level == 5)
            {
                return cpp_greedy_sequence_blocks(input, cpp_parameters);
            }
            else if constexpr (Level == 9)
            {
                return cpp_greedy_sequence_blocks<2U>(input, cpp_parameters);
            }
            else if constexpr (Level == 15)
            {
                return cpp_greedy_sequence_blocks<2U, true>(input, cpp_parameters);
            }
            else
            {
                return std::vector<sph::zstd::detail::parsed_block>{};
            }
        }()};
        auto const sequence_equal{(Level != 1 && Level != 3 && Level != 5 && Level != 9 && Level != 15) ||
            same_fast_sequences(cpp_sequence_blocks_value, reference_sequence_blocks_value)};
        auto const parameters_equal{same_parameters(cpp_parameters, reference_parameters)};
        auto const output_equal{!reference.empty() && cpp == reference};
        auto const common_size{std::min(cpp.size(), reference.size())};
        auto const mismatch{std::mismatch(cpp.begin(), cpp.begin() + static_cast<std::ptrdiff_t>(common_size),
            reference.begin())};
        auto const mismatch_offset{static_cast<std::size_t>(std::distance(cpp.begin(), mismatch.first))};
        std::cout << "level " << Level << ":\n"
                  << "  zstd++ params:   window=" << static_cast<unsigned>(cpp_parameters.window_log)
                  << " chain=" << static_cast<unsigned>(cpp_parameters.chain_log)
                  << " hash=" << static_cast<unsigned>(cpp_parameters.hash_log)
                  << " search=" << static_cast<unsigned>(cpp_parameters.search_log)
                  << " min=" << static_cast<unsigned>(cpp_parameters.minimum_match)
                  << " target=" << cpp_parameters.target_length
                  << " strategy=" << static_cast<unsigned>(cpp_parameters.strategy) << '\n'
                  << "  reference params: window=" << reference_parameters.windowLog
                  << " chain=" << reference_parameters.chainLog
                  << " hash=" << reference_parameters.hashLog
                  << " search=" << reference_parameters.searchLog
                  << " min=" << reference_parameters.minMatch
                  << " target=" << reference_parameters.targetLength
                  << " strategy=" << static_cast<unsigned>(reference_parameters.strategy) << '\n'
                  << "  parameters: " << (parameters_equal ? "[PASS]" : "[FAIL]") << '\n'
                  << "  reference sequences per block:";
        for (auto const& block : reference_sequence_blocks_value)
        {
            std::cout << ' ' << block.size();
        }
        std::cout << '\n'
                  << "  zstd++ fast sequences per block:";
        for (auto const& block : cpp_sequence_blocks_value)
        {
            std::cout << ' ' << block.sequences.size();
        }
        std::cout << '\n'
                  << "  sequence fields: " << (sequence_equal ? "[PASS]" : "[FAIL]") << '\n'
                  << "  output: zstd++=" << cpp.size()
                  << " bytes, reference=" << reference.size() << " bytes"
                  << (output_equal ? " [PASS]" : " [FAIL]") << '\n';
        if (!output_equal)
        {
            std::cout << "  first output mismatch: byte " << mismatch_offset;
            if (mismatch_offset < common_size)
            {
                std::cout << " (zstd++=" << static_cast<unsigned>(cpp[mismatch_offset])
                          << ", reference=" << static_cast<unsigned>(reference[mismatch_offset]) << ')';
            }
            std::cout << '\n';
            auto const diagnostic_end{std::min(common_size, mismatch_offset + 12U)};
            std::cout << "  zstd++ bytes:";
            for (auto index{mismatch_offset}; index < diagnostic_end; ++index)
            {
                std::cout << ' ' << static_cast<unsigned>(cpp[index]);
            }
            std::cout << '\n' << "  reference bytes:";
            for (auto index{mismatch_offset}; index < diagnostic_end; ++index)
            {
                std::cout << ' ' << static_cast<unsigned>(reference[index]);
            }
            std::cout << '\n';
        }
        return parameters_equal && output_equal;
    }
}

int main()
{
    auto const corpus{sph::zstd::eval::make_mixed_corpus(corpus_size)};
    auto const input{std::span<std::uint8_t const>{corpus}};
    auto const results{std::array{
        check_level<1>(input),
        check_level<3>(input),
        check_level<5>(input),
        check_level<9>(input),
        check_level<15>(input)
    }};
    if (!std::ranges::all_of(results, std::identity{}))
    {
        std::cerr << "compression parameter and byte-for-byte output parity is required at every acceptance level\n";
        return 1;
    }
    return 0;
}
