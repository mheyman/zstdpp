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
     * The first porting milestone handles raw and RLE blocks. Entropy-coded compressed
     * blocks report unsupported_frame until the FSE/Huffman and sequence ports land.
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

        void decode_compressed_block(std::span<std::uint8_t const> block)
        {
            if (block.empty())
            {
                fail(error_code::invalid_frame, "compressed Zstandard block has no literals section");
            }

            auto const literals_type{static_cast<std::uint8_t>(block[0] & 0x03U)};
            auto const size_format{static_cast<std::uint8_t>((block[0] >> 2U) & 0x03U)};
            std::size_t header_size{};
            std::size_t regenerated_size{};
            if (literals_type <= 1U)
            {
                if (size_format == 0U || size_format == 2U)
                {
                    header_size = 1;
                    regenerated_size = block[0] >> 3U;
                }
                else if (size_format == 1U)
                {
                    if (block.size() < 2)
                    {
                        fail(error_code::truncated_input, "truncated Zstandard literals header");
                    }
                    header_size = 2;
                    regenerated_size = (static_cast<std::size_t>(block[0]) |
                        (static_cast<std::size_t>(block[1]) << 8U)) >> 4U;
                }
                else
                {
                    if (block.size() < 3)
                    {
                        fail(error_code::truncated_input, "truncated Zstandard literals header");
                    }
                    header_size = 3;
                    regenerated_size = (static_cast<std::size_t>(block[0]) |
                        (static_cast<std::size_t>(block[1]) << 8U) |
                        (static_cast<std::size_t>(block[2]) << 16U)) >> 4U;
                }
            }
            else
            {
                fail(error_code::unsupported_frame,
                    "Huffman-compressed Zstandard literals are not implemented yet");
            }

            auto const stored_literals_size{literals_type == 0U ? regenerated_size : std::size_t{1}};
            if (regenerated_size > Parameters.maximum_decoded_block_size)
            {
                fail(error_code::invalid_frame, "Zstandard literals exceed maximum_decoded_block_size");
            }
            if (block.size() < header_size + stored_literals_size + 1U)
            {
                fail(error_code::invalid_frame, "Zstandard literals section exceeds compressed block size");
            }
            auto const sequence_offset{header_size + stored_literals_size};
            auto const first_sequence_byte{block[sequence_offset]};
            std::size_t sequence_header_size{1};
            std::size_t sequence_count{};
            if (first_sequence_byte < 128U)
            {
                sequence_count = first_sequence_byte;
            }
            else if (first_sequence_byte < 255U)
            {
                if (block.size() < sequence_offset + 2U)
                {
                    fail(error_code::truncated_input, "truncated Zstandard sequence count");
                }
                sequence_header_size = 2;
                sequence_count = (static_cast<std::size_t>(first_sequence_byte) - 128U) * 256U +
                    block[sequence_offset + 1U];
            }
            else
            {
                if (block.size() < sequence_offset + 3U)
                {
                    fail(error_code::truncated_input, "truncated Zstandard sequence count");
                }
                sequence_header_size = 3;
                sequence_count = 0x7F00U + block[sequence_offset + 1U] +
                    (static_cast<std::size_t>(block[sequence_offset + 2U]) << 8U);
            }
            if (sequence_count != 0)
            {
                fail(error_code::unsupported_frame,
                    "FSE-compressed Zstandard sequences are not implemented yet");
            }
            if (sequence_offset + sequence_header_size != block.size())
            {
                fail(error_code::invalid_frame, "zero-sequence Zstandard block has trailing data");
            }

            if (literals_type == 0U)
            {
                emit(block.subspan(header_size, regenerated_size));
            }
            else
            {
                emit_run(block[header_size], regenerated_size);
            }
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

        void emit(std::span<std::uint8_t const> output)
        {
            if (!output.empty())
            {
                std::invoke(callback_, output);
                checksum_.update(output);
                frame_decoded_size_ += output.size();
                decoded_size_ += output.size();
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
        std::uint64_t frame_decoded_size_{};
        std::uint64_t encoded_size_{};
        std::uint64_t decoded_size_{};
        std::size_t frame_count_{};
        stream_status status_{stream_status::ready};
    };

    template <typename Callback>
    zstd_decompress(Callback) -> zstd_decompress<decompression_parameters{}, std::decay_t<Callback>>;

    template <decompression_parameters Parameters = {}, typename Callback>
    [[nodiscard]] auto make_zstd_decompress(Callback&& callback)
    {
        return zstd_decompress<Parameters, std::decay_t<Callback>>{std::forward<Callback>(callback)};
    }
}
