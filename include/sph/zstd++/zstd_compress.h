#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <sph/zstd++/zstd_common.h>
#include <sph/zstd++/detail/zstd_compression.h>

namespace sph::zstd
{
    /**
     * Incrementally creates a Zstandard frame and delivers encoded bytes to Callback.
     *
     * The callback's span remains valid only for the duration of the call. Call finish()
     * to emit the final block; destruction intentionally does not hide I/O in a destructor.
     * Parameters is a structural non-type template parameter, so every option is visible
     * to the optimizer and unsupported combinations can fail during compilation.
     *
     * The fast strategy uses a bounded hash table to find profitable matches and
     * emits interoperable entropy-coded sequence blocks, with raw/RLE fallbacks.
     */
    template <compression_parameters Parameters = {}, typename Callback = std::function<void(std::span<std::uint8_t const>)>>
        requires output_callback<Callback>
    class zstd_compress
    {
        static_assert(Parameters.block_size > 0 && Parameters.block_size <= maximum_block_size,
            "zstd_compress block_size must be in [1, 128 KiB]");
        static_assert(Parameters.window_log >= 10 && Parameters.window_log <= 31,
            "zstd_compress window_log must be in [10, 31]");
        static_assert(Parameters.worker_count == 0,
            "the header-only zstd compressor does not yet implement multi-threaded frames");

    public:
        explicit zstd_compress(Callback callback) : callback_{std::move(callback)} {}

        zstd_compress(zstd_compress const&) = delete;
        auto operator=(zstd_compress const&) -> zstd_compress& = delete;
        zstd_compress(zstd_compress&&) = default;
        auto operator=(zstd_compress&&) -> zstd_compress& = default;

        void update(std::span<std::uint8_t const> input)
        {
            require_writable();
            begin_frame();
            checksum_.update(input);
            source_size_ += input.size();

            std::size_t offset{};
            while (offset < input.size())
            {
                auto const available{Parameters.block_size - buffered_size_};
                auto const count{std::min(available, input.size() - offset)};
                std::ranges::copy(input.subspan(offset, count), block_buffer_.begin() +
                    static_cast<std::ptrdiff_t>(buffered_size_));
                buffered_size_ += count;
                offset += count;
                if (buffered_size_ == Parameters.block_size)
                {
                    emit_block(false);
                }
            }
        }

        void update(std::uint8_t byte)
        {
            update(std::span<std::uint8_t const>{&byte, 1});
        }

        /** Emits currently buffered input as a non-final block. */
        void flush()
        {
            require_writable();
            begin_frame();
            if (buffered_size_ != 0)
            {
                emit_block(false);
            }
        }

        /** Completes the current frame. Calling finish() more than once is harmless. */
        void finish()
        {
            if (status_ == stream_status::finished)
            {
                return;
            }
            require_writable();
            begin_frame();
            if constexpr (Parameters.pledged_source_size != unknown_content_size)
            {
                if (source_size_ != Parameters.pledged_source_size)
                {
                    status_ = stream_status::failed;
                    throw zstd_error{error_code::source_size_mismatch,
                        "input size does not match zstd_compress pledged_source_size"};
                }
            }
            emit_block(true);
            if constexpr (Parameters.checksum)
            {
                std::array<std::uint8_t, 4> encoded_checksum{};
                detail::write_u32(encoded_checksum, static_cast<std::uint32_t>(checksum_.digest()));
                emit(encoded_checksum);
            }
            status_ = stream_status::finished;
        }

        /** Starts another frame while retaining the callback and compile-time parameters. */
        void reset()
        {
            buffered_size_ = 0;
            source_size_ = 0;
            encoded_size_ = 0;
            frame_started_ = false;
            checksum_.reset();
            status_ = stream_status::ready;
        }

        [[nodiscard]] constexpr auto status() const noexcept -> stream_status { return status_; }
        [[nodiscard]] constexpr auto source_size() const noexcept -> std::uint64_t { return source_size_; }
        [[nodiscard]] constexpr auto encoded_size() const noexcept -> std::uint64_t { return encoded_size_; }
        [[nodiscard]] static consteval auto parameters() noexcept -> compression_parameters { return Parameters; }

    private:
        void require_writable()
        {
            if (status_ == stream_status::failed)
            {
                throw zstd_error{error_code::invalid_state, "zstd_compress is in a failed state; call reset()"};
            }
            if (status_ == stream_status::finished)
            {
                throw zstd_error{error_code::invalid_state, "zstd_compress frame is finished; call reset()"};
            }
        }

        template <std::size_t Extent>
        void emit(std::span<std::uint8_t const, Extent> output)
        {
            if (!output.empty())
            {
                std::invoke(callback_, output);
                encoded_size_ += output.size();
            }
        }

        template <typename Range>
        void emit(Range const& output)
        {
            emit(std::span<std::uint8_t const>{output});
        }

        void begin_frame()
        {
            if (frame_started_)
            {
                return;
            }
            frame_started_ = true;
            status_ = stream_status::active;

            std::array<std::uint8_t, 18> header{};
            std::size_t size{};
            if constexpr (Parameters.format == frame_format::standard)
            {
                detail::write_u32(std::span<std::uint8_t>{header}.first<4>(), detail::frame_magic);
                size += 4;
            }

            std::uint8_t descriptor{Parameters.checksum ? std::uint8_t{0x04} : std::uint8_t{0x00}};
            constexpr bool has_known_size = Parameters.content_size &&
                Parameters.pledged_source_size != unknown_content_size;
            if constexpr (has_known_size)
            {
                descriptor |= 0x20U; // Single-segment frames use content size as their window size.
                if constexpr (Parameters.pledged_source_size < 256U)
                {
                    header[size + 1] = static_cast<std::uint8_t>(Parameters.pledged_source_size);
                }
                else if constexpr (Parameters.pledged_source_size < 65'792U)
                {
                    descriptor |= 0x40U;
                    auto const encoded{Parameters.pledged_source_size - 256U};
                    header[size + 1] = static_cast<std::uint8_t>(encoded);
                    header[size + 2] = static_cast<std::uint8_t>(encoded >> 8U);
                }
                else if constexpr (Parameters.pledged_source_size <= std::numeric_limits<std::uint32_t>::max())
                {
                    descriptor |= 0x80U;
                    detail::write_u32(std::span<std::uint8_t>{header}.subspan(size + 1, 4),
                        static_cast<std::uint32_t>(Parameters.pledged_source_size));
                }
                else
                {
                    descriptor |= 0xC0U;
                    auto value{Parameters.pledged_source_size};
                    for (std::size_t index{}; index < 8; ++index)
                    {
                        header[size + 1 + index] = static_cast<std::uint8_t>(value);
                        value >>= 8U;
                    }
                }
            }

            header[size++] = descriptor;
            if constexpr (has_known_size)
            {
                size += descriptor < 0x40U ? 1U : descriptor < 0x80U ? 2U : descriptor < 0xC0U ? 4U : 8U;
            }
            else
            {
                header[size++] = static_cast<std::uint8_t>((Parameters.window_log - 10U) << 3U);
            }
            emit(std::span<std::uint8_t const>{header}.first(size));
        }

        void emit_block(bool last)
        {
            auto const bytes{std::span<std::uint8_t const>{block_buffer_}.first(buffered_size_)};
            auto const is_rle{bytes.size() > 1 && std::ranges::all_of(bytes,
                [first = bytes.front()](std::uint8_t byte) { return byte == first; })};
            constexpr auto hash_log{Parameters.hash_log == 0U ? 15U : Parameters.hash_log};
            std::vector<std::uint8_t> compressed;
            if (!is_rle)
            {
                auto const selected{detail::find_best_fast_match(bytes, hash_log)};
                if (selected && selected->length >= 4U)
                {
                    auto candidate{detail::encode_single_match_block(bytes, *selected)};
                    if (candidate.size() < bytes.size())
                    {
                        compressed = std::move(candidate);
                    }
                }
            }

            auto const block_type{is_rle ? 1U : compressed.empty() ? 0U : 2U};
            auto const stored_size{compressed.empty() ? buffered_size_ : compressed.size()};
            auto const block_header{(static_cast<std::uint32_t>(stored_size) << 3U) |
                (block_type << 1U) | (last ? 1U : 0U)};
            std::array<std::uint8_t, 3> encoded_header{
                static_cast<std::uint8_t>(block_header),
                static_cast<std::uint8_t>(block_header >> 8U),
                static_cast<std::uint8_t>(block_header >> 16U)
            };
            emit(encoded_header);
            if (is_rle)
            {
                emit(bytes.first<1>());
            }
            else if (!compressed.empty())
            {
                emit(compressed);
            }
            else
            {
                emit(bytes);
            }
            buffered_size_ = 0;
        }

        Callback callback_;
        std::array<std::uint8_t, Parameters.block_size> block_buffer_{};
        std::size_t buffered_size_{};
        std::uint64_t source_size_{};
        std::uint64_t encoded_size_{};
        detail::xxhash64 checksum_{};
        stream_status status_{stream_status::ready};
        bool frame_started_{};
    };

    template <typename Callback>
    zstd_compress(Callback) -> zstd_compress<compression_parameters{}, std::decay_t<Callback>>;

    template <compression_parameters Parameters = {}, typename Callback>
    [[nodiscard]] auto make_zstd_compress(Callback&& callback)
    {
        return zstd_compress<Parameters, std::decay_t<Callback>>{std::forward<Callback>(callback)};
    }
}
