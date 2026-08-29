#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace sph::zstd::detail
{
    class entropy_error : public std::runtime_error
    {
    public:
        explicit entropy_error(std::string_view message) : std::runtime_error{std::string{message}} {}
    };

    /** Zstandard entropy streams are written forward but decoded backward. */
    class reverse_bit_reader
    {
    public:
        explicit reverse_bit_reader(std::span<std::uint8_t const> bytes) : bytes_{bytes}
        {
            if (bytes_.empty() || bytes_.back() == 0)
            {
                throw entropy_error{"Zstandard bitstream has no end marker"};
            }
            auto const marker{static_cast<unsigned>(std::bit_width(bytes_.back())) - 1U};
            remaining_bits_ = (bytes_.size() - 1U) * 8U + marker;
        }

        [[nodiscard]] auto read(unsigned count, bool permit_overread = false) -> std::uint32_t
        {
            if (count > 32U)
            {
                throw entropy_error{"Zstandard bit read exceeds 32 bits"};
            }
            if (count > remaining_bits_ && !permit_overread)
            {
                throw entropy_error{"truncated Zstandard entropy bitstream"};
            }

            if (count == 0U)
            {
                return 0U;
            }
            auto const available{static_cast<unsigned>(
                std::min<std::size_t>(count, remaining_bits_))};
            auto const missing{count - available};
            if (missing != 0U)
            {
                overflow_ = true;
            }
            if (available == 0U)
            {
                return 0U;
            }

            auto const first_bit{remaining_bits_ - available};
            auto const first_byte{first_bit / 8U};
            auto const bit_offset{static_cast<unsigned>(first_bit & 7U)};
            auto const byte_count{(bit_offset + available + 7U) / 8U};
            std::uint64_t packed{};
            for (unsigned index{}; index < byte_count; ++index)
            {
                packed |= static_cast<std::uint64_t>(bytes_[first_byte + index]) << (index * 8U);
            }
            auto const mask{available == 32U ? std::numeric_limits<std::uint32_t>::max() :
                (std::uint32_t{1} << available) - 1U};
            remaining_bits_ = first_bit;
            return static_cast<std::uint32_t>((packed >> bit_offset) & mask) << missing;
        }

        [[nodiscard]] auto peek(unsigned count, bool permit_overread = false) const -> std::uint32_t
        {
            auto copy{*this};
            return copy.read(count, permit_overread);
        }

        [[nodiscard]] auto remaining() const noexcept -> std::size_t { return remaining_bits_; }
        [[nodiscard]] auto at_end() const noexcept -> bool { return remaining_bits_ == 0 && !overflow_; }
        [[nodiscard]] auto overflowed() const noexcept -> bool { return overflow_; }

    private:
        std::span<std::uint8_t const> bytes_;
        std::size_t remaining_bits_{};
        bool overflow_{};
    };

    class forward_lsb_reader
    {
    public:
        explicit forward_lsb_reader(std::span<std::uint8_t const> bytes, std::size_t bit_offset = 0)
            : bytes_{bytes}, bit_offset_{bit_offset}
        {
        }

        [[nodiscard]] auto peek(unsigned count) const -> std::uint32_t
        {
            if (count > 32U || bit_offset_ + count > bytes_.size() * 8U)
            {
                throw entropy_error{"truncated Zstandard FSE table description"};
            }
            if (count == 0U)
            {
                return 0U;
            }
            auto const first_byte{bit_offset_ / 8U};
            auto const within_byte{static_cast<unsigned>(bit_offset_ & 7U)};
            auto const byte_count{(within_byte + count + 7U) / 8U};
            std::uint64_t packed{};
            for (unsigned index{}; index < byte_count; ++index)
            {
                packed |= static_cast<std::uint64_t>(bytes_[first_byte + index]) << (index * 8U);
            }
            auto const mask{count == 32U ? std::numeric_limits<std::uint32_t>::max() :
                (std::uint32_t{1} << count) - 1U};
            return static_cast<std::uint32_t>((packed >> within_byte) & mask);
        }

        [[nodiscard]] auto read(unsigned count) -> std::uint32_t
        {
            auto const value{peek(count)};
            bit_offset_ += count;
            return value;
        }

        [[nodiscard]] auto bytes_consumed() const noexcept -> std::size_t
        {
            return (bit_offset_ + 7U) / 8U;
        }

    private:
        std::span<std::uint8_t const> bytes_;
        std::size_t bit_offset_{};
    };

    struct normalized_counts
    {
        std::array<std::int16_t, 256> values{};
        unsigned maximum_symbol{};
        unsigned table_log{};
        std::size_t bytes_consumed{};
    };

    /** Decodes the compact normalized-count header shared by FSE tables. */
    [[nodiscard]] inline auto read_normalized_counts(
        std::span<std::uint8_t const> source, unsigned maximum_symbol) -> normalized_counts
    {
        if (source.empty() || maximum_symbol >= 256U)
        {
            throw entropy_error{"invalid Zstandard FSE table description"};
        }

        forward_lsb_reader bits{source, 4};
        normalized_counts result{};
        result.table_log = static_cast<unsigned>(source[0] & 0x0FU) + 5U;
        if (result.table_log > 12U)
        {
            throw entropy_error{"Zstandard FSE table log is too large"};
        }

        auto remaining{static_cast<int>((1U << result.table_log) + 1U)};
        auto threshold{static_cast<int>(1U << result.table_log)};
        auto number_bits{static_cast<int>(result.table_log + 1U)};
        unsigned symbol{};
        bool previous_zero{};

        while (remaining > 1 && symbol <= maximum_symbol)
        {
            if (previous_zero)
            {
                while (bits.peek(2) == 3U)
                {
                    static_cast<void>(bits.read(2));
                    symbol += 3U;
                    if (symbol > maximum_symbol)
                    {
                        throw entropy_error{"too many zero counts in Zstandard FSE table"};
                    }
                }
                symbol += static_cast<unsigned>(bits.read(2));
                if (symbol > maximum_symbol)
                {
                    throw entropy_error{"too many zero counts in Zstandard FSE table"};
                }
            }

            auto const maximum{(2 * threshold - 1) - remaining};
            int count{};
            if (static_cast<int>(bits.peek(static_cast<unsigned>(number_bits - 1))) < maximum)
            {
                count = static_cast<int>(bits.read(static_cast<unsigned>(number_bits - 1)));
            }
            else
            {
                count = static_cast<int>(bits.read(static_cast<unsigned>(number_bits)));
                if (count >= threshold)
                {
                    count -= maximum;
                }
            }
            --count;
            if (count >= 0)
            {
                remaining -= count;
            }
            else
            {
                // A -1 normalized count represents one low-probability state.
                remaining += count;
            }
            result.values[symbol++] = static_cast<std::int16_t>(count);
            previous_zero = count == 0;

            if (remaining < threshold)
            {
                if (remaining <= 1)
                {
                    break;
                }
                number_bits = static_cast<int>(std::bit_width(static_cast<unsigned>(remaining)));
                threshold = 1 << (number_bits - 1);
            }
        }

        if (remaining != 1 || symbol == 0)
        {
            throw entropy_error{"invalid normalized counts in Zstandard FSE table"};
        }
        result.maximum_symbol = symbol - 1U;
        result.bytes_consumed = bits.bytes_consumed();
        if (result.bytes_consumed > source.size())
        {
            throw entropy_error{"truncated Zstandard FSE table description"};
        }
        return result;
    }

    struct fse_entry
    {
        std::uint16_t next_state{};
        std::uint8_t number_bits{};
        std::uint8_t symbol{};
    };

    struct fse_table
    {
        std::vector<fse_entry> entries;
        unsigned table_log{};
    };

    [[nodiscard]] inline auto build_fse_table(normalized_counts const& counts) -> fse_table
    {
        auto const table_size{std::size_t{1} << counts.table_log};
        fse_table result{std::vector<fse_entry>(table_size), counts.table_log};
        std::array<std::uint16_t, 256> symbol_next{};
        auto high_threshold{table_size - 1U};

        std::size_t total{};
        for (unsigned symbol{}; symbol <= counts.maximum_symbol; ++symbol)
        {
            auto const count{counts.values[symbol]};
            if (count == -1)
            {
                result.entries[high_threshold--].symbol = static_cast<std::uint8_t>(symbol);
                symbol_next[symbol] = 1;
                ++total;
            }
            else if (count >= 0)
            {
                symbol_next[symbol] = static_cast<std::uint16_t>(count);
                total += static_cast<std::size_t>(count);
            }
            else
            {
                throw entropy_error{"invalid negative Zstandard FSE count"};
            }
        }
        if (total != table_size)
        {
            throw entropy_error{"Zstandard FSE counts do not fill the decoding table"};
        }

        auto const table_mask{table_size - 1U};
        auto const step{(table_size >> 1U) + (table_size >> 3U) + 3U};
        std::size_t position{};
        for (unsigned symbol{}; symbol <= counts.maximum_symbol; ++symbol)
        {
            auto const count{counts.values[symbol]};
            for (int occurrence{}; occurrence < count; ++occurrence)
            {
                result.entries[position].symbol = static_cast<std::uint8_t>(symbol);
                position = (position + step) & table_mask;
                while (position > high_threshold)
                {
                    position = (position + step) & table_mask;
                }
            }
        }
        if (position != 0)
        {
            throw entropy_error{"invalid Zstandard FSE symbol spread"};
        }

        for (std::size_t index{}; index < table_size; ++index)
        {
            auto& entry{result.entries[index]};
            auto const next{static_cast<unsigned>(symbol_next[entry.symbol]++)};
            if (next == 0)
            {
                throw entropy_error{"invalid Zstandard FSE decoding state"};
            }
            entry.number_bits = static_cast<std::uint8_t>(counts.table_log -
                (static_cast<unsigned>(std::bit_width(next)) - 1U));
            entry.next_state = static_cast<std::uint16_t>((next << entry.number_bits) - table_size);
        }
        return result;
    }

    struct sequence_entry
    {
        std::uint16_t next_state{};
        std::uint8_t additional_bits{};
        std::uint8_t state_bits{};
        std::uint32_t base_value{};
    };

    struct sequence_table
    {
        std::vector<sequence_entry> entries;
        unsigned table_log{};
        bool valid{};
    };

    inline constexpr std::array<std::uint32_t, 36> literal_length_base{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 18, 20, 22, 24, 28, 32, 40, 48, 64, 0x80, 0x100, 0x200, 0x400,
        0x800, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000
    };
    inline constexpr std::array<std::uint8_t, 36> literal_length_bits{
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
    };
    inline constexpr std::array<std::int16_t, 36> literal_length_default_norm{
        4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1
    };

    inline constexpr std::array<std::uint32_t, 32> offset_base{
        0, 1, 1, 5, 0xD, 0x1D, 0x3D, 0x7D, 0xFD, 0x1FD, 0x3FD, 0x7FD,
        0xFFD, 0x1FFD, 0x3FFD, 0x7FFD, 0xFFFD, 0x1FFFD, 0x3FFFD, 0x7FFFD,
        0xFFFFD, 0x1FFFFD, 0x3FFFFD, 0x7FFFFD, 0xFFFFFD, 0x1FFFFFD,
        0x3FFFFFD, 0x7FFFFFD, 0xFFFFFFD, 0x1FFFFFFD, 0x3FFFFFFD, 0x7FFFFFFD
    };
    inline constexpr std::array<std::uint8_t, 32> offset_bits{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
    };
    inline constexpr std::array<std::int16_t, 29> offset_default_norm{
        1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1
    };

    inline constexpr std::array<std::uint32_t, 53> match_length_base{
        3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
        35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99, 0x83, 0x103, 0x203,
        0x403, 0x803, 0x1003, 0x2003, 0x4003, 0x8003, 0x10003
    };
    inline constexpr std::array<std::uint8_t, 53> match_length_bits{
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11,
        12, 13, 14, 15, 16
    };
    inline constexpr std::array<std::int16_t, 53> match_length_default_norm{
        1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1,
        -1, -1, -1, -1, -1
    };

    template <std::size_t Count>
    [[nodiscard]] inline auto make_normalized_counts(
        std::array<std::int16_t, Count> const& values, unsigned table_log) -> normalized_counts
    {
        normalized_counts result{};
        std::ranges::copy(values, result.values.begin());
        result.maximum_symbol = static_cast<unsigned>(Count - 1U);
        result.table_log = table_log;
        return result;
    }

    template <std::size_t Count>
    [[nodiscard]] inline auto build_sequence_table(
        normalized_counts const& counts,
        std::array<std::uint32_t, Count> const& bases,
        std::array<std::uint8_t, Count> const& additional_bits) -> sequence_table
    {
        if (counts.maximum_symbol >= Count)
        {
            throw entropy_error{"Zstandard FSE sequence symbol is out of range"};
        }
        auto const symbols{build_fse_table(counts)};
        sequence_table result{};
        result.table_log = symbols.table_log;
        result.valid = true;
        result.entries.reserve(symbols.entries.size());
        for (auto const& symbol : symbols.entries)
        {
            result.entries.push_back(sequence_entry{
                symbol.next_state,
                additional_bits[symbol.symbol],
                symbol.number_bits,
                bases[symbol.symbol]
            });
        }
        return result;
    }

    template <std::size_t Count>
    [[nodiscard]] inline auto make_rle_sequence_table(
        std::uint8_t symbol,
        std::array<std::uint32_t, Count> const& bases,
        std::array<std::uint8_t, Count> const& additional_bits) -> sequence_table
    {
        if (symbol >= Count)
        {
            throw entropy_error{"Zstandard RLE sequence symbol is out of range"};
        }
        return sequence_table{
            std::vector<sequence_entry>{sequence_entry{0, additional_bits[symbol], 0, bases[symbol]}},
            0,
            true
        };
    }

    [[nodiscard]] inline auto fse_decompress_bytes(
        std::span<std::uint8_t const> source, std::size_t capacity, unsigned maximum_table_log) -> std::vector<std::uint8_t>
    {
        auto const counts{read_normalized_counts(source, 255)};
        if (counts.table_log > maximum_table_log || counts.bytes_consumed >= source.size())
        {
            throw entropy_error{"invalid Zstandard FSE-compressed weights"};
        }
        auto const table{build_fse_table(counts)};
        reverse_bit_reader bits{source.subspan(counts.bytes_consumed)};
        auto state_one{static_cast<std::size_t>(bits.read(table.table_log))};
        auto state_two{static_cast<std::size_t>(bits.read(table.table_log))};
        std::vector<std::uint8_t> output;
        output.reserve(capacity);

        auto decode = [&bits, &table, &output, capacity](std::size_t& state)
        {
            if (state >= table.entries.size() || output.size() >= capacity)
            {
                throw entropy_error{"invalid Zstandard FSE weight stream"};
            }
            auto const entry{table.entries[state]};
            output.push_back(entry.symbol);
            state = static_cast<std::size_t>(entry.next_state) + bits.read(entry.number_bits, true);
        };

        for (;;)
        {
            decode(state_one);
            if (bits.overflowed())
            {
                decode(state_two);
                break;
            }
            decode(state_two);
            if (bits.overflowed())
            {
                decode(state_one);
                break;
            }
        }
        return output;
    }

    struct huffman_entry
    {
        std::uint8_t symbol{};
        std::uint8_t number_bits{};
    };

    struct huffman_table
    {
        std::vector<huffman_entry> entries;
        unsigned table_log{};
        bool valid{};
    };

    struct huffman_description
    {
        huffman_table table;
        std::size_t bytes_consumed{};
    };

    [[nodiscard]] inline auto read_huffman_table(std::span<std::uint8_t const> source) -> huffman_description
    {
        if (source.empty())
        {
            throw entropy_error{"truncated Zstandard Huffman table"};
        }

        auto const encoded_size{static_cast<std::size_t>(source[0])};
        std::vector<std::uint8_t> weights;
        std::size_t bytes_consumed{};
        if (encoded_size >= 128U)
        {
            auto const weight_count{encoded_size - 127U};
            auto const weight_bytes{(weight_count + 1U) / 2U};
            if (weight_bytes + 1U > source.size() || weight_count >= 256U)
            {
                throw entropy_error{"invalid direct Zstandard Huffman weights"};
            }
            weights.resize(weight_count);
            for (std::size_t index{}; index < weight_count; ++index)
            {
                auto const packed{source[1U + index / 2U]};
                weights[index] = index % 2U == 0U ? static_cast<std::uint8_t>(packed >> 4U) :
                    static_cast<std::uint8_t>(packed & 0x0FU);
            }
            bytes_consumed = weight_bytes + 1U;
        }
        else
        {
            if (encoded_size == 0 || encoded_size + 1U > source.size())
            {
                throw entropy_error{"invalid FSE-compressed Zstandard Huffman weights"};
            }
            weights = fse_decompress_bytes(source.subspan(1U, encoded_size), 255U, 6U);
            bytes_consumed = encoded_size + 1U;
        }

        std::array<std::uint32_t, 13> rank_counts{};
        std::uint32_t weight_total{};
        for (auto const weight : weights)
        {
            if (weight > 12U)
            {
                throw entropy_error{"Zstandard Huffman weight is too large"};
            }
            ++rank_counts[weight];
            weight_total += (std::uint32_t{1} << weight) >> 1U;
        }
        if (weight_total == 0)
        {
            throw entropy_error{"empty Zstandard Huffman tree"};
        }

        auto const table_log{static_cast<unsigned>(std::bit_width(weight_total))};
        if (table_log > 12U)
        {
            throw entropy_error{"Zstandard Huffman table log is too large"};
        }
        auto const total{std::uint32_t{1} << table_log};
        auto const rest{total - weight_total};
        if (rest == 0 || !std::has_single_bit(rest))
        {
            throw entropy_error{"invalid implied Zstandard Huffman weight"};
        }
        auto const last_weight{static_cast<std::uint8_t>(std::bit_width(rest))};
        weights.push_back(last_weight);
        ++rank_counts[last_weight];
        if (rank_counts[1] < 2U || (rank_counts[1] & 1U) != 0U)
        {
            throw entropy_error{"invalid Zstandard Huffman tree"};
        }

        std::array<std::vector<std::uint8_t>, 13> symbols_by_weight;
        for (std::size_t symbol{}; symbol < weights.size(); ++symbol)
        {
            symbols_by_weight[weights[symbol]].push_back(static_cast<std::uint8_t>(symbol));
        }

        huffman_table table{std::vector<huffman_entry>(std::size_t{1} << table_log), table_log, true};
        std::size_t table_index{};
        for (unsigned weight{1}; weight <= table_log; ++weight)
        {
            auto const run_length{(std::size_t{1} << weight) >> 1U};
            auto const number_bits{static_cast<std::uint8_t>(table_log + 1U - weight)};
            for (auto const symbol : symbols_by_weight[weight])
            {
                if (table_index + run_length > table.entries.size())
                {
                    throw entropy_error{"Zstandard Huffman tree overfills its decoding table"};
                }
                std::fill_n(table.entries.begin() + static_cast<std::ptrdiff_t>(table_index), run_length,
                    huffman_entry{symbol, number_bits});
                table_index += run_length;
            }
        }
        if (table_index != table.entries.size())
        {
            throw entropy_error{"Zstandard Huffman tree does not fill its decoding table"};
        }
        return {std::move(table), bytes_consumed};
    }

    inline void decode_huffman_stream(
        std::span<std::uint8_t> destination,
        std::span<std::uint8_t const> source,
        huffman_table const& table)
    {
        if (!table.valid || table.entries.empty())
        {
            throw entropy_error{"Zstandard Huffman repeat mode has no previous table"};
        }
        reverse_bit_reader bits{source};
        for (auto& byte : destination)
        {
            auto const index{bits.peek(table.table_log, true)};
            if (index >= table.entries.size())
            {
                throw entropy_error{"invalid Zstandard Huffman code"};
            }
            auto const entry{table.entries[index]};
            static_cast<void>(bits.read(entry.number_bits));
            byte = entry.symbol;
        }
        if (!bits.at_end())
        {
            throw entropy_error{"Zstandard Huffman stream has trailing bits"};
        }
    }

    [[nodiscard]] inline auto decode_huffman_literals(
        std::span<std::uint8_t const> source,
        std::size_t regenerated_size,
        bool four_streams,
        huffman_table const& table) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> output(regenerated_size);
        if (!four_streams)
        {
            decode_huffman_stream(output, source, table);
            return output;
        }
        if (source.size() < 10U || regenerated_size < 6U)
        {
            throw entropy_error{"invalid four-stream Zstandard Huffman section"};
        }
        auto const read_u16 = [source](std::size_t offset)
        {
            return static_cast<std::size_t>(source[offset]) |
                (static_cast<std::size_t>(source[offset + 1U]) << 8U);
        };
        std::array<std::size_t, 4> lengths{read_u16(0), read_u16(2), read_u16(4), 0};
        auto const first_three{lengths[0] + lengths[1] + lengths[2]};
        if (first_three + 6U >= source.size())
        {
            throw entropy_error{"invalid Zstandard Huffman jump table"};
        }
        lengths[3] = source.size() - 6U - first_three;

        auto const segment_size{(regenerated_size + 3U) / 4U};
        std::array<std::size_t, 5> output_offsets{
            0, segment_size, segment_size * 2U, segment_size * 3U, regenerated_size};
        if (output_offsets[3] > regenerated_size)
        {
            throw entropy_error{"invalid Zstandard Huffman output segments"};
        }
        std::size_t source_offset{6};
        for (std::size_t stream{}; stream < 4U; ++stream)
        {
            decode_huffman_stream(
                std::span<std::uint8_t>{output}.subspan(output_offsets[stream],
                    output_offsets[stream + 1U] - output_offsets[stream]),
                source.subspan(source_offset, lengths[stream]), table);
            source_offset += lengths[stream];
        }
        return output;
    }
}
