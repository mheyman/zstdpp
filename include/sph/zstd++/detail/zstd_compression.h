#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <span>
#include <vector>

#include <sph/zstd++/detail/zstd_entropy.h>

namespace sph::zstd::detail
{
    struct match
    {
        std::size_t position{};
        std::size_t length{};
        std::size_t offset{};
    };

    /**
     * A single-candidate hash-chain match finder. This is the baseline for the
     * `fast` strategy: it is linear, bounded by the input block, and deliberately
     * keeps only the most recent position for each hash. More expensive template
     * strategies can replace it without changing the frame or streaming layers.
     */
    [[nodiscard]] inline auto find_best_fast_match(
        std::span<std::uint8_t const> input, unsigned hash_log) -> std::optional<match>
    {
        if (input.size() < 8U)
        {
            return std::nullopt;
        }
        hash_log = std::clamp(hash_log, 10U, 20U);
        auto const table_size{std::size_t{1} << hash_log};
        auto constexpr missing{std::numeric_limits<std::size_t>::max()};
        std::vector<std::size_t> latest(table_size, missing);
        std::optional<match> best;

        auto const hash = [hash_log](std::span<std::uint8_t const> bytes)
        {
            auto const value{static_cast<std::uint32_t>(bytes[0]) |
                (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                (static_cast<std::uint32_t>(bytes[2]) << 16U) |
                (static_cast<std::uint32_t>(bytes[3]) << 24U)};
            return static_cast<std::size_t>((value * 0x9E3779B1U) >> (32U - hash_log));
        };

        for (std::size_t position{}; position + 4U <= input.size(); ++position)
        {
            auto const slot{hash(input.subspan(position, 4U))};
            auto const candidate{latest[slot]};
            latest[slot] = position;
            if (candidate == missing || candidate >= position ||
                !std::ranges::equal(input.subspan(candidate, 4U), input.subspan(position, 4U)))
            {
                continue;
            }
            if (best && input.size() - position <= best->length)
            {
                continue;
            }

            std::size_t length{4};
            while (position + length < input.size() && input[candidate + length] == input[position + length])
            {
                ++length;
            }
            if (!best || length > best->length)
            {
                best = match{position, length, position - candidate};
            }
        }
        return best;
    }

    class reverse_bit_writer
    {
    public:
        void append(std::uint32_t value, unsigned count)
        {
            for (unsigned index{}; index < count; ++index)
            {
                auto const shift{count - index - 1U};
                bits_.push_back(static_cast<std::uint8_t>((value >> shift) & 1U));
            }
        }

        [[nodiscard]] auto finish() const -> std::vector<std::uint8_t>
        {
            auto const data_bits{bits_.size()};
            std::vector<std::uint8_t> output(data_bits / 8U + 1U);
            output[data_bits / 8U] |= static_cast<std::uint8_t>(1U << (data_bits % 8U));
            for (std::size_t index{}; index < data_bits; ++index)
            {
                if (bits_[index] != 0U)
                {
                    auto const destination{data_bits - index - 1U};
                    output[destination / 8U] |= static_cast<std::uint8_t>(1U << (destination % 8U));
                }
            }
            return output;
        }

    private:
        std::vector<std::uint8_t> bits_;
    };

    template <std::size_t Count>
    [[nodiscard]] inline auto find_length_symbol(
        std::size_t value,
        std::array<std::uint32_t, Count> const& bases,
        std::array<std::uint8_t, Count> const& additional_bits) -> std::uint8_t
    {
        for (std::size_t index{Count}; index-- > 0U;)
        {
            auto const base{static_cast<std::size_t>(bases[index])};
            auto const range{std::size_t{1} << additional_bits[index]};
            if (value >= base && value - base < range)
            {
                return static_cast<std::uint8_t>(index);
            }
        }
        throw entropy_error{"value cannot be represented by a Zstandard sequence code"};
    }

    [[nodiscard]] inline auto find_offset_symbol(std::size_t value) -> std::uint8_t
    {
        for (std::size_t index{2}; index < offset_base.size(); ++index)
        {
            auto const base{static_cast<std::size_t>(offset_base[index])};
            auto const range{std::size_t{1} << offset_bits[index]};
            if (value >= base && value - base < range)
            {
                return static_cast<std::uint8_t>(index);
            }
        }
        throw entropy_error{"match offset cannot be represented by Zstandard"};
    }

    inline void append_raw_literals_header(std::vector<std::uint8_t>& output, std::size_t size)
    {
        if (size < 32U)
        {
            output.push_back(static_cast<std::uint8_t>(size << 3U));
        }
        else if (size < 4096U)
        {
            auto const header{static_cast<std::uint32_t>((size << 4U) | 4U)};
            output.push_back(static_cast<std::uint8_t>(header));
            output.push_back(static_cast<std::uint8_t>(header >> 8U));
        }
        else
        {
            auto const header{static_cast<std::uint32_t>((size << 4U) | 12U)};
            output.push_back(static_cast<std::uint8_t>(header));
            output.push_back(static_cast<std::uint8_t>(header >> 8U));
            output.push_back(static_cast<std::uint8_t>(header >> 16U));
        }
    }

    struct huffman_code
    {
        std::uint16_t value{};
        std::uint8_t number_bits{};
    };

    struct huffman_node
    {
        std::size_t frequency{};
        std::size_t parent{std::numeric_limits<std::size_t>::max()};
    };

    [[nodiscard]] inline auto encode_huffman_stream(
        std::span<std::uint8_t const> input,
        std::array<huffman_code, 256> const& codes) -> std::vector<std::uint8_t>
    {
        reverse_bit_writer bits;
        for (auto const symbol : input)
        {
            auto const code{codes[symbol]};
            if (code.number_bits == 0U)
            {
                throw entropy_error{"missing Zstandard Huffman encoding code"};
            }
            bits.append(code.value, code.number_bits);
        }
        return bits.finish();
    }

    /**
     * Builds a length-limited canonical Huffman tree and writes a direct-weight
     * table. Direct tables cover alphabets whose highest used byte is at most
     * 128; larger or excessively skewed alphabets cleanly fall back to raw
     * literals until the FSE weight encoder is available.
     */
    [[nodiscard]] inline auto encode_huffman_literals(
        std::span<std::uint8_t const> literals) -> std::optional<std::vector<std::uint8_t>>
    {
        if (literals.size() < 16U)
        {
            return std::nullopt;
        }
        std::array<std::size_t, 256> frequencies{};
        std::uint8_t maximum_symbol{};
        std::size_t distinct_symbols{};
        for (auto const symbol : literals)
        {
            if (frequencies[symbol]++ == 0U)
            {
                ++distinct_symbols;
            }
            maximum_symbol = std::max(maximum_symbol, symbol);
        }
        if (distinct_symbols < 2U || maximum_symbol > 128U)
        {
            return std::nullopt;
        }

        using queue_entry = std::pair<std::size_t, std::size_t>;
        std::priority_queue<queue_entry, std::vector<queue_entry>, std::greater<>> pending;
        std::vector<huffman_node> nodes;
        std::array<std::size_t, 256> leaf_nodes{};
        leaf_nodes.fill(std::numeric_limits<std::size_t>::max());
        for (std::size_t symbol{}; symbol <= maximum_symbol; ++symbol)
        {
            if (frequencies[symbol] != 0U)
            {
                leaf_nodes[symbol] = nodes.size();
                nodes.push_back(huffman_node{frequencies[symbol]});
                pending.emplace(frequencies[symbol], nodes.size() - 1U);
            }
        }
        while (pending.size() > 1U)
        {
            auto const first{pending.top()};
            pending.pop();
            auto const second{pending.top()};
            pending.pop();
            auto const parent{nodes.size()};
            nodes[first.second].parent = parent;
            nodes[second.second].parent = parent;
            nodes.push_back(huffman_node{first.first + second.first});
            pending.emplace(first.first + second.first, parent);
        }

        std::array<std::uint8_t, 256> lengths{};
        unsigned table_log{};
        for (std::size_t symbol{}; symbol <= maximum_symbol; ++symbol)
        {
            if (leaf_nodes[symbol] == std::numeric_limits<std::size_t>::max())
            {
                continue;
            }
            unsigned length{};
            for (auto node{leaf_nodes[symbol]}; nodes[node].parent != std::numeric_limits<std::size_t>::max();
                node = nodes[node].parent)
            {
                ++length;
            }
            if (length == 0U || length > 11U)
            {
                return std::nullopt;
            }
            lengths[symbol] = static_cast<std::uint8_t>(length);
            table_log = std::max(table_log, length);
        }

        std::array<std::uint8_t, 256> weights{};
        std::array<huffman_code, 256> codes{};
        std::size_t table_position{};
        for (unsigned weight{1}; weight <= table_log; ++weight)
        {
            auto const code_length{table_log + 1U - weight};
            auto const run_length{std::size_t{1} << (weight - 1U)};
            for (std::size_t symbol{}; symbol <= maximum_symbol; ++symbol)
            {
                if (lengths[symbol] != code_length)
                {
                    continue;
                }
                weights[symbol] = static_cast<std::uint8_t>(weight);
                codes[symbol] = huffman_code{
                    static_cast<std::uint16_t>(table_position >> (table_log - code_length)),
                    static_cast<std::uint8_t>(code_length)
                };
                table_position += run_length;
            }
        }
        if (table_position != (std::size_t{1} << table_log))
        {
            return std::nullopt;
        }

        auto const explicit_weights{static_cast<std::size_t>(maximum_symbol)};
        std::vector<std::uint8_t> body;
        body.push_back(static_cast<std::uint8_t>(127U + explicit_weights));
        for (std::size_t symbol{}; symbol < explicit_weights; symbol += 2U)
        {
            auto const high{static_cast<std::uint8_t>(weights[symbol] << 4U)};
            auto const low{symbol + 1U < explicit_weights ? weights[symbol + 1U] : std::uint8_t{0}};
            body.push_back(static_cast<std::uint8_t>(high | low));
        }

        auto const four_streams{literals.size() >= 1024U};
        if (!four_streams)
        {
            auto const stream{encode_huffman_stream(literals, codes)};
            body.insert(body.end(), stream.begin(), stream.end());
        }
        else
        {
            auto const segment_size{(literals.size() + 3U) / 4U};
            std::array<std::vector<std::uint8_t>, 4> streams;
            for (std::size_t stream{}; stream < 4U; ++stream)
            {
                auto const begin{std::min(literals.size(), stream * segment_size)};
                auto const end{std::min(literals.size(), (stream + 1U) * segment_size)};
                streams[stream] = encode_huffman_stream(literals.subspan(begin, end - begin), codes);
            }
            if (streams[0].size() > std::numeric_limits<std::uint16_t>::max() ||
                streams[1].size() > std::numeric_limits<std::uint16_t>::max() ||
                streams[2].size() > std::numeric_limits<std::uint16_t>::max())
            {
                return std::nullopt;
            }
            for (std::size_t stream{}; stream < 3U; ++stream)
            {
                body.push_back(static_cast<std::uint8_t>(streams[stream].size()));
                body.push_back(static_cast<std::uint8_t>(streams[stream].size() >> 8U));
            }
            for (auto const& stream : streams)
            {
                body.insert(body.end(), stream.begin(), stream.end());
            }
        }

        auto const regenerated_size{literals.size()};
        auto const compressed_size{body.size()};
        std::vector<std::uint8_t> output;
        if (!four_streams && regenerated_size < 1024U && compressed_size < 1024U)
        {
            auto const header{static_cast<std::uint32_t>((regenerated_size << 4U) |
                (compressed_size << 14U) | 2U)};
            output = {static_cast<std::uint8_t>(header), static_cast<std::uint8_t>(header >> 8U),
                static_cast<std::uint8_t>(header >> 16U)};
        }
        else if (regenerated_size < 16'384U && compressed_size < 16'384U)
        {
            auto const header{static_cast<std::uint32_t>((regenerated_size << 4U) |
                (compressed_size << 18U) | 10U)};
            output = {static_cast<std::uint8_t>(header), static_cast<std::uint8_t>(header >> 8U),
                static_cast<std::uint8_t>(header >> 16U), static_cast<std::uint8_t>(header >> 24U)};
        }
        else if (regenerated_size < 262'144U && compressed_size < 262'144U)
        {
            auto const header{static_cast<std::uint32_t>((regenerated_size << 4U) |
                ((compressed_size & 0x3FFU) << 22U) | 14U)};
            output = {static_cast<std::uint8_t>(header), static_cast<std::uint8_t>(header >> 8U),
                static_cast<std::uint8_t>(header >> 16U), static_cast<std::uint8_t>(header >> 24U),
                static_cast<std::uint8_t>(compressed_size >> 10U)};
        }
        else
        {
            return std::nullopt;
        }
        output.insert(output.end(), body.begin(), body.end());
        return output;
    }

    /**
     * Encodes one profitable match as a compressed block. Each sequence alphabet
     * uses Zstandard's one-symbol FSE/RLE mode, so there are no ANS state bits;
     * only the literal-length, offset, and match-length extra bits are emitted.
     * This is a complete interoperable entropy path and a stepping stone to
     * multi-sequence normalized FSE tables.
     */
    [[nodiscard]] inline auto encode_single_match_block(
        std::span<std::uint8_t const> input, match const& selected) -> std::vector<std::uint8_t>
    {
        if (selected.position + selected.length > input.size() || selected.length < 3U)
        {
            throw entropy_error{"invalid match selected by Zstandard compressor"};
        }
        auto const trailing_size{input.size() - selected.position - selected.length};
        auto const literal_size{selected.position + trailing_size};
        auto const literal_symbol{find_length_symbol(selected.position,
            literal_length_base, literal_length_bits)};
        auto const match_symbol{find_length_symbol(selected.length,
            match_length_base, match_length_bits)};
        auto const selected_offset_symbol{find_offset_symbol(selected.offset)};

        std::vector<std::uint8_t> literals;
        literals.reserve(literal_size);
        literals.insert(literals.end(), input.begin(), input.begin() + static_cast<std::ptrdiff_t>(selected.position));
        literals.insert(literals.end(), input.begin() + static_cast<std::ptrdiff_t>(selected.position + selected.length),
            input.end());

        std::vector<std::uint8_t> raw_literals;
        raw_literals.reserve(literals.size() + 3U);
        append_raw_literals_header(raw_literals, literals.size());
        raw_literals.insert(raw_literals.end(), literals.begin(), literals.end());
        auto huffman_literals{encode_huffman_literals(literals)};

        std::vector<std::uint8_t> output;
        output.reserve(input.size());
        if (huffman_literals && huffman_literals->size() < raw_literals.size())
        {
            output.insert(output.end(), huffman_literals->begin(), huffman_literals->end());
        }
        else
        {
            output.insert(output.end(), raw_literals.begin(), raw_literals.end());
        }

        output.push_back(1U); // one sequence
        output.push_back(0x54U); // LL, OF, and ML all use one-symbol FSE/RLE tables
        output.push_back(literal_symbol);
        output.push_back(selected_offset_symbol);
        output.push_back(match_symbol);

        reverse_bit_writer bits;
        bits.append(static_cast<std::uint32_t>(selected.offset - offset_base[selected_offset_symbol]),
            offset_bits[selected_offset_symbol]);
        bits.append(static_cast<std::uint32_t>(selected.length - match_length_base[match_symbol]),
            match_length_bits[match_symbol]);
        bits.append(static_cast<std::uint32_t>(selected.position - literal_length_base[literal_symbol]),
            literal_length_bits[literal_symbol]);
        auto const bitstream{bits.finish()};
        output.insert(output.end(), bitstream.begin(), bitstream.end());
        return output;
    }
}
