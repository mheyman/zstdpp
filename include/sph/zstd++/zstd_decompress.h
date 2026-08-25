#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <sph/zstd++/zstd_common.h>
#include <sph/zstd++/detail/zstd_entropy.h>

namespace sph::zstd
{
    struct frame_information
    {
        std::optional<std::uint64_t> content_size;
        std::uint64_t window_size{};
        std::uint32_t dictionary_id{};
        bool checksum{};
    };

    /**
     * Incrementally decodes concatenated Zstandard frames and skips skippable frames.
     * Output spans are valid only during the callback. finish() verifies that the final
     * frame was complete and reports truncated input explicitly.
     *
     * Raw, RLE, and entropy-coded blocks are accepted. Match copies may refer to
     * earlier blocks, but retained history is always bounded by the frame window.
     */
    template <decompression_parameters Parameters = {}, typename Callback = std::function<void(std::span<std::uint8_t const>)>>
        requires output_callback<Callback>
    class zstd_decompress
    {
        static_assert(Parameters.maximum_window_log >= 10 && Parameters.maximum_window_log <= 63,
            "zstd_decompress maximum_window_log must be in [10, 63]");
        static_assert(Parameters.maximum_decoded_block_size > 0 &&
            Parameters.maximum_decoded_block_size <= maximum_block_size,
            "zstd_decompress maximum_decoded_block_size must be in [1, 128 KiB]");

    public:
        explicit zstd_decompress(Callback callback) : callback_{std::move(callback)} {}

        zstd_decompress(zstd_decompress const&) = delete;
        auto operator=(zstd_decompress const&) -> zstd_decompress& = delete;
        zstd_decompress(zstd_decompress&&) = default;
        auto operator=(zstd_decompress&&) -> zstd_decompress& = default;

        void update(std::span<std::uint8_t const> input)
        {
            require_writable();
            if (!input.empty())
            {
                status_ = stream_status::active;
                encoded_size_ += input.size();
                pending_.insert(pending_.end(), input.begin(), input.end());
                parse();
                compact_input();
            }
        }

        void update(std::uint8_t byte)
        {
            update(std::span<std::uint8_t const>{&byte, 1});
        }

        void finish()
        {
            if (status_ == stream_status::finished)
            {
                return;
            }
            require_writable();
            parse();
            compact_input();
            if (state_ != parse_state::frame_header || !pending_.empty() || frame_count_ == 0)
            {
                fail(error_code::truncated_input, "incomplete Zstandard frame at end of input");
            }
            status_ = stream_status::finished;
        }

        void reset()
        {
            pending_.clear();
            cursor_ = 0;
            state_ = parse_state::frame_header;
            status_ = stream_status::ready;
            frame_count_ = 0;
            encoded_size_ = 0;
            decoded_size_ = 0;
            frame_decoded_size_ = 0;
            last_block_ = false;
            checksum_.reset();
            information_ = {};
            reset_frame_state();
        }

        [[nodiscard]] constexpr auto status() const noexcept -> stream_status { return status_; }
        [[nodiscard]] constexpr auto frame_count() const noexcept -> std::size_t { return frame_count_; }
        [[nodiscard]] constexpr auto encoded_size() const noexcept -> std::uint64_t { return encoded_size_; }
        [[nodiscard]] constexpr auto decoded_size() const noexcept -> std::uint64_t { return decoded_size_; }
        [[nodiscard]] constexpr auto last_frame() const noexcept -> frame_information const& { return information_; }
        [[nodiscard]] static consteval auto parameters() noexcept -> decompression_parameters { return Parameters; }

    private:
        enum class parse_state : std::uint8_t
        {
            frame_header,
            block_header,
            block_payload,
            frame_checksum
        };

        enum class block_type : std::uint8_t
        {
            raw,
            run_length,
            compressed,
            reserved
        };

        void require_writable()
        {
            if (status_ == stream_status::failed)
            {
                throw zstd_error{error_code::invalid_state, "zstd_decompress is in a failed state; call reset()"};
            }
            if (status_ == stream_status::finished)
            {
                throw zstd_error{error_code::invalid_state, "zstd_decompress stream is finished; call reset()"};
            }
        }

        [[noreturn]] void fail(error_code code, std::string_view message)
        {
            status_ = stream_status::failed;
            throw zstd_error{code, message};
        }

        [[nodiscard]] auto available() const noexcept -> std::size_t { return pending_.size() - cursor_; }

        [[nodiscard]] auto input() const noexcept -> std::span<std::uint8_t const>
        {
            return std::span<std::uint8_t const>{pending_}.subspan(cursor_);
        }

        void consume(std::size_t count) noexcept { cursor_ += count; }

        void compact_input()
        {
            if (cursor_ == pending_.size())
            {
                pending_.clear();
                cursor_ = 0;
            }
            else if (cursor_ >= maximum_block_size)
            {
                pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(cursor_));
                cursor_ = 0;
            }
        }

        void parse()
        {
            for (;;)
            {
                bool progressed{};
                switch (state_)
                {
                case parse_state::frame_header:
                    progressed = parse_frame_header();
                    break;
                case parse_state::block_header:
                    progressed = parse_block_header();
                    break;
                case parse_state::block_payload:
                    progressed = parse_block_payload();
                    break;
                case parse_state::frame_checksum:
                    progressed = parse_frame_checksum();
                    break;
                }
                if (!progressed)
                {
                    return;
                }
            }
        }

        auto parse_frame_header() -> bool
        {
            auto bytes{input()};
            std::size_t prefix_size{};
            if constexpr (Parameters.format == frame_format::standard)
            {
                if (bytes.size() < 4)
                {
                    return false;
                }
                auto const magic{detail::read_u32(bytes.first(4))};
                if (magic >= detail::skippable_magic_start && magic <= detail::skippable_magic_end)
                {
                    if (bytes.size() < 8)
                    {
                        return false;
                    }
                    auto const skip_size{detail::read_u32(bytes.subspan(4, 4))};
                    auto const total_size{8U + static_cast<std::uint64_t>(skip_size)};
                    if (total_size > std::numeric_limits<std::size_t>::max())
                    {
                        fail(error_code::invalid_frame, "skippable Zstandard frame is too large");
                    }
                    if (bytes.size() < static_cast<std::size_t>(total_size))
                    {
                        return false;
                    }
                    consume(static_cast<std::size_t>(total_size));
                    return true;
                }
                if (magic != detail::frame_magic)
                {
                    fail(error_code::invalid_frame, "invalid Zstandard frame magic");
                }
                prefix_size = 4;
            }

            if (bytes.size() < prefix_size + 1)
            {
                return false;
            }
            auto const descriptor{bytes[prefix_size]};
            if ((descriptor & 0x18U) != 0)
            {
                fail(error_code::invalid_frame, "reserved Zstandard frame-header bits are set");
            }
            auto const content_size_flag{static_cast<std::uint8_t>(descriptor >> 6U)};
            auto const single_segment{(descriptor & 0x20U) != 0};
            auto const dictionary_flag{static_cast<std::uint8_t>(descriptor & 0x03U)};
            std::array<std::size_t, 4> constexpr dictionary_sizes{0, 1, 2, 4};
            std::array<std::size_t, 4> constexpr content_sizes{0, 2, 4, 8};
            auto content_size_bytes{content_sizes[content_size_flag]};
            if (single_segment && content_size_flag == 0)
            {
                content_size_bytes = 1;
            }
            auto const window_bytes{single_segment ? 0U : 1U};
            auto const dictionary_bytes{dictionary_sizes[dictionary_flag]};
            auto const header_size{prefix_size + 1U + window_bytes + dictionary_bytes + content_size_bytes};
            if (bytes.size() < header_size)
            {
                return false;
            }

            std::size_t offset{prefix_size + 1U};
            std::uint64_t window_size{};
            if (!single_segment)
            {
                auto const descriptor_byte{bytes[offset++]};
                auto const exponent{static_cast<std::uint8_t>(descriptor_byte >> 3U)};
                auto const mantissa{static_cast<std::uint8_t>(descriptor_byte & 0x07U)};
                auto const window_log{static_cast<std::uint8_t>(10U + exponent)};
                if (window_log > Parameters.maximum_window_log)
                {
                    fail(error_code::unsupported_frame, "Zstandard frame window exceeds maximum_window_log");
                }
                auto const window_base{std::uint64_t{1} << window_log};
                window_size = window_base + (window_base >> 3U) * mantissa;
            }

            std::uint32_t dictionary_id{};
            for (std::size_t index{}; index < dictionary_bytes; ++index)
            {
                dictionary_id |= static_cast<std::uint32_t>(bytes[offset++]) << (index * 8U);
            }
            if (dictionary_id != 0)
            {
                fail(error_code::unsupported_frame, "dictionary-compressed Zstandard frames are not implemented yet");
            }

            std::optional<std::uint64_t> content_size;
            if (content_size_bytes != 0)
            {
                std::uint64_t value{};
                for (std::size_t index{}; index < content_size_bytes; ++index)
                {
                    value |= static_cast<std::uint64_t>(bytes[offset++]) << (index * 8U);
                }
                if (content_size_bytes == 2)
                {
                    value += 256U;
                }
                content_size = value;
                if (single_segment)
                {
                    window_size = value;
                    auto const maximum_window{std::uint64_t{1} << Parameters.maximum_window_log};
                    if (window_size > maximum_window)
                    {
                        fail(error_code::unsupported_frame, "single-segment Zstandard frame exceeds maximum_window_log");
                    }
                }
            }

            information_ = frame_information{
                .content_size = content_size,
                .window_size = window_size,
                .dictionary_id = dictionary_id,
                .checksum = (descriptor & 0x04U) != 0
            };
            checksum_.reset();
            frame_decoded_size_ = 0;
            reset_frame_state();
            consume(header_size);
            state_ = parse_state::block_header;
            return true;
        }

        auto parse_block_header() -> bool
        {
            if (available() < 3)
            {
                return false;
            }
            auto const bytes{input()};
            auto const header{static_cast<std::uint32_t>(bytes[0]) |
                (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                (static_cast<std::uint32_t>(bytes[2]) << 16U)};
            last_block_ = (header & 1U) != 0;
            block_type_ = static_cast<block_type>((header >> 1U) & 0x03U);
            block_size_ = header >> 3U;
            if (block_size_ > Parameters.maximum_decoded_block_size)
            {
                fail(error_code::invalid_frame, "Zstandard block exceeds maximum_decoded_block_size");
            }
            if (block_type_ == block_type::reserved)
            {
                fail(error_code::invalid_frame, "reserved Zstandard block type");
            }
            consume(3);
            state_ = parse_state::block_payload;
            return true;
        }

        auto parse_block_payload() -> bool
        {
            switch (block_type_)
            {
            case block_type::raw:
                if (available() < block_size_)
                {
                    return false;
                }
                emit(input().first(block_size_));
                consume(block_size_);
                break;
            case block_type::run_length:
                if (available() < 1)
                {
                    return false;
                }
                emit_run(input().front(), block_size_);
                consume(1);
                break;
            case block_type::compressed:
                if (available() < block_size_)
                {
                    return false;
                }
                decode_compressed_block(input().first(block_size_));
                consume(block_size_);
                break;
            case block_type::reserved:
                fail(error_code::invalid_frame, "reserved Zstandard block type");
            }

            if (last_block_)
            {
                if (information_.checksum)
                {
                    state_ = parse_state::frame_checksum;
                }
                else
                {
                    complete_frame();
                }
            }
            else
            {
                state_ = parse_state::block_header;
            }
            return true;
        }

        struct literals_section
        {
            std::vector<std::uint8_t> bytes;
            std::size_t sequence_offset{};
        };

        void decode_compressed_block(std::span<std::uint8_t const> block)
        {
            try
            {
                auto literals{decode_literals(block)};
                emit(decode_sequences(block.subspan(literals.sequence_offset), literals.bytes));
            }
            catch (detail::entropy_error const& error)
            {
                fail(error_code::invalid_frame, error.what());
            }
        }

        [[nodiscard]] auto decode_literals(std::span<std::uint8_t const> block) -> literals_section
        {
            if (block.empty())
            {
                throw detail::entropy_error{"compressed Zstandard block has no literals section"};
            }

            auto const literals_type{static_cast<std::uint8_t>(block[0] & 0x03U)};
            auto const size_format{static_cast<std::uint8_t>((block[0] >> 2U) & 0x03U)};
            std::size_t header_size{};
            std::size_t regenerated_size{};
            std::size_t compressed_size{};
            bool four_streams{};

            if (literals_type <= 1U)
            {
                if (size_format == 0U || size_format == 2U)
                {
                    header_size = 1;
                    regenerated_size = block[0] >> 3U;
                }
                else if (size_format == 1U)
                {
                    if (block.size() < 2U)
                    {
                        throw detail::entropy_error{"truncated Zstandard literals header"};
                    }
                    header_size = 2;
                    regenerated_size = (static_cast<std::size_t>(block[0]) |
                        (static_cast<std::size_t>(block[1]) << 8U)) >> 4U;
                }
                else
                {
                    if (block.size() < 3U)
                    {
                        throw detail::entropy_error{"truncated Zstandard literals header"};
                    }
                    header_size = 3;
                    regenerated_size = (static_cast<std::size_t>(block[0]) |
                        (static_cast<std::size_t>(block[1]) << 8U) |
                        (static_cast<std::size_t>(block[2]) << 16U)) >> 4U;
                }
            }
            else if (size_format <= 1U)
            {
                if (block.size() < 3U)
                {
                    throw detail::entropy_error{"truncated Zstandard Huffman literals header"};
                }
                auto const header{static_cast<std::uint32_t>(block[0]) |
                    (static_cast<std::uint32_t>(block[1]) << 8U) |
                    (static_cast<std::uint32_t>(block[2]) << 16U)};
                header_size = 3;
                regenerated_size = (header >> 4U) & 0x3FFU;
                compressed_size = (header >> 14U) & 0x3FFU;
                four_streams = size_format == 1U;
            }
            else if (size_format == 2U)
            {
                if (block.size() < 4U)
                {
                    throw detail::entropy_error{"truncated Zstandard Huffman literals header"};
                }
                auto const header{detail::read_u32(block.first(4))};
                header_size = 4;
                regenerated_size = (header >> 4U) & 0x3FFFU;
                compressed_size = header >> 18U;
                four_streams = true;
            }
            else
            {
                if (block.size() < 5U)
                {
                    throw detail::entropy_error{"truncated Zstandard Huffman literals header"};
                }
                auto const header{detail::read_u32(block.first(4))};
                header_size = 5;
                regenerated_size = (header >> 4U) & 0x3FFFFU;
                compressed_size = (header >> 22U) + (static_cast<std::size_t>(block[4]) << 10U);
                four_streams = true;
            }

            if (regenerated_size > Parameters.maximum_decoded_block_size)
            {
                throw detail::entropy_error{"Zstandard literals exceed maximum_decoded_block_size"};
            }
            if (literals_type <= 1U)
            {
                auto const stored_size{literals_type == 0U ? regenerated_size : std::size_t{1}};
                if (header_size + stored_size > block.size())
                {
                    throw detail::entropy_error{"Zstandard literals section exceeds compressed block size"};
                }
                std::vector<std::uint8_t> literals;
                if (literals_type == 0U)
                {
                    auto const stored{block.subspan(header_size, regenerated_size)};
                    literals.assign(stored.begin(), stored.end());
                }
                else
                {
                    literals.assign(regenerated_size, block[header_size]);
                }
                return {std::move(literals), header_size + stored_size};
            }

            if (compressed_size == 0U || header_size + compressed_size > block.size())
            {
                throw detail::entropy_error{"Zstandard Huffman literals exceed compressed block size"};
            }
            auto compressed{block.subspan(header_size, compressed_size)};
            if (literals_type == 2U)
            {
                auto description{detail::read_huffman_table(compressed)};
                huffman_table_ = std::move(description.table);
                if (description.bytes_consumed >= compressed.size())
                {
                    throw detail::entropy_error{"Zstandard Huffman literals contain no bitstream"};
                }
                compressed = compressed.subspan(description.bytes_consumed);
            }
            else if (!huffman_table_.valid)
            {
                throw detail::entropy_error{"Zstandard Huffman repeat mode has no previous table"};
            }

            return {
                detail::decode_huffman_literals(compressed, regenerated_size, four_streams, huffman_table_),
                header_size + compressed_size
            };
        }

        template <std::size_t Count, std::size_t NormCount>
        void load_sequence_table(
            detail::sequence_table& table,
            std::uint8_t mode,
            std::span<std::uint8_t const> source,
            std::size_t& offset,
            unsigned maximum_table_log,
            std::array<std::uint32_t, Count> const& bases,
            std::array<std::uint8_t, Count> const& additional_bits,
            std::array<std::int16_t, NormCount> const& default_norm,
            unsigned default_log)
        {
            switch (mode)
            {
            case 0:
                table = detail::build_sequence_table(
                    detail::make_normalized_counts(default_norm, default_log), bases, additional_bits);
                return;
            case 1:
                if (offset >= source.size())
                {
                    throw detail::entropy_error{"truncated Zstandard RLE sequence table"};
                }
                table = detail::make_rle_sequence_table(source[offset++], bases, additional_bits);
                return;
            case 2:
            {
                auto const counts{detail::read_normalized_counts(source.subspan(offset),
                    static_cast<unsigned>(Count - 1U))};
                if (counts.table_log > maximum_table_log)
                {
                    throw detail::entropy_error{"Zstandard sequence FSE table log is too large"};
                }
                offset += counts.bytes_consumed;
                table = detail::build_sequence_table(counts, bases, additional_bits);
                return;
            }
            case 3:
                if (!fse_repeat_allowed_ || !table.valid)
                {
                    throw detail::entropy_error{"Zstandard FSE repeat mode has no previous table"};
                }
                return;
            default:
                throw detail::entropy_error{"invalid Zstandard sequence table mode"};
            }
        }

        [[nodiscard]] auto read_sequence_count(
            std::span<std::uint8_t const> source, std::size_t& offset) const -> std::size_t
        {
            if (source.empty())
            {
                throw detail::entropy_error{"compressed Zstandard block has no sequence section"};
            }
            offset = 1;
            if (source[0] < 128U)
            {
                return source[0];
            }
            if (source[0] < 255U)
            {
                if (source.size() < 2U)
                {
                    throw detail::entropy_error{"truncated Zstandard sequence count"};
                }
                offset = 2;
                return (static_cast<std::size_t>(source[0]) - 128U) * 256U + source[1];
            }
            if (source.size() < 3U)
            {
                throw detail::entropy_error{"truncated Zstandard sequence count"};
            }
            offset = 3;
            return 0x7F00U + source[1] + (static_cast<std::size_t>(source[2]) << 8U);
        }

        void append_match(std::vector<std::uint8_t>& output, std::size_t match_offset, std::size_t match_length)
        {
            if (match_length > Parameters.maximum_decoded_block_size - output.size())
            {
                throw detail::entropy_error{"decoded Zstandard block is too large"};
            }
            for (std::size_t index{}; index < match_length; ++index)
            {
                auto const available{history_.size() + output.size()};
                if (match_offset == 0U || match_offset > available)
                {
                    throw detail::entropy_error{"Zstandard match offset exceeds the retained history window"};
                }
                auto const source_position{available - match_offset};
                output.push_back(source_position < history_.size() ? history_[source_position] :
                    output[source_position - history_.size()]);
            }
        }

        [[nodiscard]] auto decode_sequences(
            std::span<std::uint8_t const> source,
            std::span<std::uint8_t const> literals) -> std::vector<std::uint8_t>
        {
            std::size_t offset{};
            auto const sequence_count{read_sequence_count(source, offset)};
            if (sequence_count == 0U)
            {
                if (offset != source.size())
                {
                    throw detail::entropy_error{"zero-sequence Zstandard block has trailing data"};
                }
                return {literals.begin(), literals.end()};
            }
            if (sequence_count > Parameters.maximum_decoded_block_size || offset >= source.size())
            {
                throw detail::entropy_error{"invalid Zstandard sequence count"};
            }

            auto const modes{source[offset++]};
            if ((modes & 3U) != 0U)
            {
                throw detail::entropy_error{"reserved Zstandard sequence-header bits are set"};
            }
            load_sequence_table(literal_length_table_, static_cast<std::uint8_t>(modes >> 6U), source, offset,
                9U, detail::literal_length_base, detail::literal_length_bits,
                detail::literal_length_default_norm, 6U);
            load_sequence_table(offset_table_, static_cast<std::uint8_t>((modes >> 4U) & 3U), source, offset,
                8U, detail::offset_base, detail::offset_bits, detail::offset_default_norm, 5U);
            load_sequence_table(match_length_table_, static_cast<std::uint8_t>((modes >> 2U) & 3U), source, offset,
                9U, detail::match_length_base, detail::match_length_bits,
                detail::match_length_default_norm, 6U);
            if (offset >= source.size())
            {
                throw detail::entropy_error{"Zstandard sequence section has no bitstream"};
            }

            detail::reverse_bit_reader bits{source.subspan(offset)};
            auto literal_state{static_cast<std::size_t>(bits.read(literal_length_table_.table_log))};
            auto offset_state{static_cast<std::size_t>(bits.read(offset_table_.table_log))};
            auto match_state{static_cast<std::size_t>(bits.read(match_length_table_.table_log))};
            std::vector<std::uint8_t> output;
            output.reserve(std::min(Parameters.maximum_decoded_block_size,
                literals.size() + sequence_count * 3U));
            std::size_t literal_offset{};

            for (std::size_t sequence_index{}; sequence_index < sequence_count; ++sequence_index)
            {
                if (literal_state >= literal_length_table_.entries.size() ||
                    offset_state >= offset_table_.entries.size() ||
                    match_state >= match_length_table_.entries.size())
                {
                    throw detail::entropy_error{"invalid Zstandard FSE sequence state"};
                }
                auto const literal_entry{literal_length_table_.entries[literal_state]};
                auto const offset_entry{offset_table_.entries[offset_state]};
                auto const match_entry{match_length_table_.entries[match_state]};

                std::size_t match_offset{};
                if (offset_entry.additional_bits > 1U)
                {
                    match_offset = static_cast<std::size_t>(offset_entry.base_value) +
                        bits.read(offset_entry.additional_bits);
                    repeat_offsets_[2] = repeat_offsets_[1];
                    repeat_offsets_[1] = repeat_offsets_[0];
                    repeat_offsets_[0] = match_offset;
                }
                else if (offset_entry.additional_bits == 0U)
                {
                    auto const literal_is_zero{literal_entry.base_value == 0U};
                    match_offset = repeat_offsets_[literal_is_zero ? 1U : 0U];
                    repeat_offsets_[1] = repeat_offsets_[literal_is_zero ? 0U : 1U];
                    repeat_offsets_[0] = match_offset;
                }
                else
                {
                    auto const literal_is_zero{literal_entry.base_value == 0U};
                    auto const repeat_code{static_cast<std::size_t>(offset_entry.base_value) +
                        (literal_is_zero ? 1U : 0U) + bits.read(1U)};
                    if (repeat_code == 0U || repeat_code > 3U)
                    {
                        throw detail::entropy_error{"invalid Zstandard repeat offset code"};
                    }
                    match_offset = repeat_code == 3U ? repeat_offsets_[0] - 1U : repeat_offsets_[repeat_code];
                    if (match_offset == 0U)
                    {
                        throw detail::entropy_error{"Zstandard match offset is zero"};
                    }
                    if (repeat_code != 1U)
                    {
                        repeat_offsets_[2] = repeat_offsets_[1];
                    }
                    repeat_offsets_[1] = repeat_offsets_[0];
                    repeat_offsets_[0] = match_offset;
                }

                auto const match_length{static_cast<std::size_t>(match_entry.base_value) +
                    bits.read(match_entry.additional_bits)};
                auto const literal_length{static_cast<std::size_t>(literal_entry.base_value) +
                    bits.read(literal_entry.additional_bits)};
                if (literal_length > literals.size() - literal_offset ||
                    literal_length > Parameters.maximum_decoded_block_size - output.size())
                {
                    throw detail::entropy_error{"Zstandard sequence consumes too many literals"};
                }
                output.insert(output.end(), literals.begin() + static_cast<std::ptrdiff_t>(literal_offset),
                    literals.begin() + static_cast<std::ptrdiff_t>(literal_offset + literal_length));
                literal_offset += literal_length;
                append_match(output, match_offset, match_length);

                if (sequence_index + 1U != sequence_count)
                {
                    literal_state = static_cast<std::size_t>(literal_entry.next_state) +
                        bits.read(literal_entry.state_bits);
                    match_state = static_cast<std::size_t>(match_entry.next_state) +
                        bits.read(match_entry.state_bits);
                    offset_state = static_cast<std::size_t>(offset_entry.next_state) +
                        bits.read(offset_entry.state_bits);
                }
            }
            if (!bits.at_end())
            {
                throw detail::entropy_error{"Zstandard sequence bitstream has trailing bits"};
            }
            if (literals.size() - literal_offset > Parameters.maximum_decoded_block_size - output.size())
            {
                throw detail::entropy_error{"decoded Zstandard block is too large"};
            }
            output.insert(output.end(), literals.begin() + static_cast<std::ptrdiff_t>(literal_offset), literals.end());
            fse_repeat_allowed_ = true;
            return output;
        }

        auto parse_frame_checksum() -> bool
        {
            if (available() < 4)
            {
                return false;
            }
            auto const expected{detail::read_u32(input().first(4))};
            if constexpr (!Parameters.ignore_checksum)
            {
                if (expected != static_cast<std::uint32_t>(checksum_.digest()))
                {
                    fail(error_code::checksum_mismatch, "Zstandard content checksum mismatch");
                }
            }
            consume(4);
            complete_frame();
            return true;
        }

        void complete_frame()
        {
            if (information_.content_size && *information_.content_size != frame_decoded_size_)
            {
                fail(error_code::source_size_mismatch, "decoded size does not match Zstandard frame content size");
            }
            ++frame_count_;
            state_ = parse_state::frame_header;
        }

        void reset_frame_state()
        {
            history_.clear();
            huffman_table_ = {};
            literal_length_table_ = {};
            offset_table_ = {};
            match_length_table_ = {};
            repeat_offsets_ = {1, 4, 8};
            fse_repeat_allowed_ = false;
        }

        void remember(std::span<std::uint8_t const> output)
        {
            if (output.empty())
            {
                return;
            }
            auto const configured_limit{information_.window_size == 0U ?
                std::uint64_t{Parameters.maximum_decoded_block_size} : information_.window_size};
            auto const limit{static_cast<std::size_t>(std::min<std::uint64_t>(
                configured_limit, std::numeric_limits<std::size_t>::max()))};
            if (output.size() >= limit)
            {
                history_.assign(output.end() - static_cast<std::ptrdiff_t>(limit), output.end());
                return;
            }
            auto const excess{history_.size() + output.size() > limit ?
                history_.size() + output.size() - limit : 0U};
            if (excess != 0U)
            {
                history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(excess));
            }
            history_.insert(history_.end(), output.begin(), output.end());
        }

        void emit(std::span<std::uint8_t const> output)
        {
            if (!output.empty())
            {
                std::invoke(callback_, output);
                checksum_.update(output);
                frame_decoded_size_ += output.size();
                decoded_size_ += output.size();
                remember(output);
            }
        }

        void emit_run(std::uint8_t byte, std::size_t count)
        {
            std::array<std::uint8_t, 4096> output{};
            output.fill(byte);
            while (count != 0)
            {
                auto const chunk_size{std::min(count, output.size())};
                emit(std::span<std::uint8_t const>{output}.first(chunk_size));
                count -= chunk_size;
            }
        }

        Callback callback_;
        std::vector<std::uint8_t> pending_;
        std::size_t cursor_{};
        parse_state state_{parse_state::frame_header};
        block_type block_type_{block_type::raw};
        std::size_t block_size_{};
        bool last_block_{};
        frame_information information_{};
        detail::xxhash64 checksum_{};
        std::vector<std::uint8_t> history_;
        detail::huffman_table huffman_table_{};
        detail::sequence_table literal_length_table_{};
        detail::sequence_table offset_table_{};
        detail::sequence_table match_length_table_{};
        std::array<std::size_t, 3> repeat_offsets_{1, 4, 8};
        std::uint64_t frame_decoded_size_{};
        std::uint64_t encoded_size_{};
        std::uint64_t decoded_size_{};
        std::size_t frame_count_{};
        stream_status status_{stream_status::ready};
        bool fse_repeat_allowed_{};
    };

    template <typename Callback>
    zstd_decompress(Callback) -> zstd_decompress<decompression_parameters{}, std::decay_t<Callback>>;

    template <decompression_parameters Parameters = {}, typename Callback>
    [[nodiscard]] auto make_zstd_decompress(Callback&& callback)
    {
        return zstd_decompress<Parameters, std::decay_t<Callback>>{std::forward<Callback>(callback)};
    }
}
