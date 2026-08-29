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
#include <sph/zstd++/detail/zstd_parameters.h>

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
        static constexpr auto effective_parameters_{detail::resolve_parameters(Parameters)};

        using match_state_type = std::conditional_t<
            effective_parameters_.strategy == compression_strategy::fast,
            detail::fast_match_state,
            std::conditional_t<
                effective_parameters_.strategy == compression_strategy::double_fast,
                detail::double_fast_match_state,
                std::conditional_t<
                    effective_parameters_.strategy == compression_strategy::greedy ||
                    effective_parameters_.strategy == compression_strategy::lazy2,
                    std::conditional_t<effective_parameters_.strategy == compression_strategy::greedy,
                        detail::greedy_match_state<>, detail::greedy_match_state<2U>>,
                    std::conditional_t<
                        effective_parameters_.strategy == compression_strategy::binary_tree_lazy2,
                        detail::greedy_match_state<2U, true>, detail::empty_match_state>>>>;

        static_assert(Parameters.block_size > 0 && Parameters.block_size <= maximum_block_size,
            "zstd_compress block_size must be in [1, 128 KiB]");
        static_assert(effective_parameters_.window_log >= 10 && effective_parameters_.window_log <= 31,
            "zstd_compress window_log must be in [10, 31]");
        static_assert(Parameters.worker_count == 0,
            "the header-only zstd compressor does not yet implement multi-threaded frames");

    public:
        explicit zstd_compress(Callback callback) : callback_{std::move(callback)}
        {
            parsed_.sequences.reserve(Parameters.block_size / 4U + 1U);
            if constexpr (Parameters.pledged_source_size != unknown_content_size &&
                Parameters.pledged_source_size <= std::numeric_limits<std::size_t>::max())
            {
                history_.reserve(static_cast<std::size_t>(Parameters.pledged_source_size));
            }
            else
            {
                history_.reserve(std::size_t{1} << effective_parameters_.window_log);
            }
        }

        zstd_compress(zstd_compress const&) = delete;
        auto operator=(zstd_compress const&) -> zstd_compress& = delete;
        zstd_compress(zstd_compress&&) = default;
        auto operator=(zstd_compress&&) -> zstd_compress& = default;

        void update(std::span<std::uint8_t const> input)
        {
            require_writable();
            begin_frame();
            if constexpr (Parameters.checksum)
            {
                checksum_.update(input);
            }
            source_size_ += input.size();

            if (buffered_size_ == Parameters.block_size && !input.empty())
            {
                emit_block(false);
            }

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
                    constexpr bool has_pledged_size = Parameters.pledged_source_size != unknown_content_size;
                    auto const is_pledged_final_block{has_pledged_size &&
                        source_size_ == Parameters.pledged_source_size && offset == input.size()};
                    if (!is_pledged_final_block)
                    {
                        emit_block(false);
                    }
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
            do
            {
                emit_block(true);
            }
            while (buffered_size_ != 0U);
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
            block_count_ = 0;
            compression_savings_ = 0;
            history_.clear();
            match_state_.reset();
            checksum_.reset();
            status_ = stream_status::ready;
        }

        [[nodiscard]] constexpr auto status() const noexcept -> stream_status { return status_; }
        [[nodiscard]] constexpr auto source_size() const noexcept -> std::uint64_t { return source_size_; }
        [[nodiscard]] constexpr auto encoded_size() const noexcept -> std::uint64_t { return encoded_size_; }
        [[nodiscard]] static consteval auto parameters() noexcept -> compression_parameters { return Parameters; }
        [[nodiscard]] static consteval auto effective_parameters() noexcept -> compression_parameters
        {
            return effective_parameters_;
        }

    private:
        [[nodiscard]] static auto make_match_state() -> match_state_type
        {
            if constexpr (effective_parameters_.strategy == compression_strategy::fast)
            {
                return match_state_type{effective_parameters_.window_log,
                    effective_parameters_.hash_log, effective_parameters_.minimum_match,
                    effective_parameters_.target_length};
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::double_fast)
            {
                return match_state_type{effective_parameters_.window_log,
                    effective_parameters_.hash_log, effective_parameters_.chain_log,
                    effective_parameters_.minimum_match};
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::greedy)
            {
                return match_state_type{effective_parameters_.window_log,
                    effective_parameters_.hash_log, effective_parameters_.chain_log,
                    effective_parameters_.search_log, effective_parameters_.minimum_match};
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::lazy2)
            {
                return match_state_type{effective_parameters_.window_log,
                    effective_parameters_.hash_log, effective_parameters_.chain_log,
                    effective_parameters_.search_log, effective_parameters_.minimum_match};
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::binary_tree_lazy2)
            {
                return match_state_type{effective_parameters_.window_log,
                    effective_parameters_.hash_log, effective_parameters_.chain_log,
                    effective_parameters_.search_log, effective_parameters_.minimum_match};
            }
            else
            {
                return match_state_type{};
            }
        }

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
            constexpr bool single_segment = has_known_size &&
                Parameters.pledged_source_size <= (std::uint64_t{1} << effective_parameters_.window_log);
            if constexpr (has_known_size)
            {
                if constexpr (single_segment)
                {
                    descriptor |= 0x20U;
                }
                if constexpr (single_segment && Parameters.pledged_source_size < 256U)
                {
                    // The zero content-size flag has a one-byte field only for single-segment frames.
                }
                else if constexpr (Parameters.pledged_source_size < 65'792U)
                {
                    descriptor |= 0x40U;
                }
                else if constexpr (Parameters.pledged_source_size <= std::numeric_limits<std::uint32_t>::max())
                {
                    descriptor |= 0x80U;
                }
                else
                {
                    descriptor |= 0xC0U;
                }
            }

            header[size++] = descriptor;
            if constexpr (!single_segment)
            {
                header[size++] = static_cast<std::uint8_t>((effective_parameters_.window_log - 10U) << 3U);
            }
            if constexpr (has_known_size)
            {
                if constexpr (single_segment && Parameters.pledged_source_size < 256U)
                {
                    header[size++] = static_cast<std::uint8_t>(Parameters.pledged_source_size);
                }
                else if constexpr (Parameters.pledged_source_size < 65'792U)
                {
                    auto const encoded{Parameters.pledged_source_size - 256U};
                    header[size++] = static_cast<std::uint8_t>(encoded);
                    header[size++] = static_cast<std::uint8_t>(encoded >> 8U);
                }
                else if constexpr (Parameters.pledged_source_size <= std::numeric_limits<std::uint32_t>::max())
                {
                    detail::write_u32(std::span<std::uint8_t>{header}.subspan(size, 4),
                        static_cast<std::uint32_t>(Parameters.pledged_source_size));
                    size += 4U;
                }
                else
                {
                    auto value{Parameters.pledged_source_size};
                    for (std::size_t index{}; index < 8U; ++index)
                    {
                        header[size++] = static_cast<std::uint8_t>(value);
                        value >>= 8U;
                    }
                }
            }
            emit(std::span<std::uint8_t const>{header}.first(size));
        }

        void emit_block(bool last)
        {
            auto block_size{buffered_size_};
            if constexpr (effective_parameters_.strategy == compression_strategy::fast)
            {
                if (buffered_size_ == maximum_block_size && compression_savings_ >= 3)
                {
                    block_size = fast_split_size(std::span<std::uint8_t const>{block_buffer_});
                }
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::double_fast)
            {
                if (buffered_size_ == maximum_block_size && compression_savings_ >= 3)
                {
                    block_size = chunk_split_size(std::span<std::uint8_t const>{block_buffer_}, 43U, 8U);
                }
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::greedy)
            {
                if (buffered_size_ == maximum_block_size && compression_savings_ >= 3)
                {
                    block_size = chunk_split_size(std::span<std::uint8_t const>{block_buffer_}, 11U, 9U);
                }
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::lazy2 ||
                effective_parameters_.strategy == compression_strategy::binary_tree_lazy2)
            {
                if (buffered_size_ == maximum_block_size && compression_savings_ >= 3)
                {
                    block_size = chunk_split_size(std::span<std::uint8_t const>{block_buffer_}, 5U, 10U);
                }
            }
            auto const bytes{std::span<std::uint8_t const>{block_buffer_}.first(block_size)};
            auto const actual_last{last && block_size == buffered_size_};
            auto const input_is_rle{bytes.size() > 1 && std::ranges::all_of(bytes,
                [first = bytes.front()](std::uint8_t byte) { return byte == first; })};
            auto const block_begin{history_.size()};
            history_.insert(history_.end(), bytes.begin(), bytes.end());
            std::vector<std::uint8_t> compressed;
            if constexpr (effective_parameters_.strategy == compression_strategy::fast)
            {
                parsed_ = match_state_.parse(history_, block_begin, bytes.size(), std::move(parsed_));
                if (!parsed_.sequences.empty())
                {
                    auto candidate{detail::encode_sequences_block(history_, parsed_)};
                    if (candidate.size() < bytes.size())
                    {
                        compressed = std::move(candidate);
                    }
                }
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::double_fast)
            {
                parsed_ = match_state_.parse(history_, block_begin, bytes.size(), std::move(parsed_));
                if (!parsed_.sequences.empty())
                {
                    auto candidate{detail::encode_sequences_block(history_, parsed_)};
                    if (candidate.size() < bytes.size())
                    {
                        compressed = std::move(candidate);
                    }
                }
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::greedy)
            {
                parsed_ = match_state_.parse(history_, block_begin, bytes.size(), std::move(parsed_));
                if (!parsed_.sequences.empty())
                {
                    auto candidate{detail::encode_sequences_block(history_, parsed_)};
                    if (candidate.size() < bytes.size())
                    {
                        compressed = std::move(candidate);
                    }
                }
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::lazy2)
            {
                parsed_ = match_state_.parse(history_, block_begin, bytes.size(), std::move(parsed_));
                if (!parsed_.sequences.empty())
                {
                    auto candidate{detail::encode_sequences_block(history_, parsed_)};
                    if (candidate.size() < bytes.size())
                    {
                        compressed = std::move(candidate);
                    }
                }
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::binary_tree_lazy2)
            {
                parsed_ = match_state_.parse(history_, block_begin, bytes.size(), std::move(parsed_));
                if (!parsed_.sequences.empty())
                {
                    auto candidate{detail::encode_sequences_block(history_, parsed_)};
                    if (candidate.size() < bytes.size())
                    {
                        compressed = std::move(candidate);
                    }
                }
            }
            constexpr bool has_reference_parser =
                effective_parameters_.strategy == compression_strategy::fast ||
                effective_parameters_.strategy == compression_strategy::double_fast ||
                effective_parameters_.strategy == compression_strategy::greedy ||
                effective_parameters_.strategy == compression_strategy::lazy2 ||
                effective_parameters_.strategy == compression_strategy::binary_tree_lazy2;
            if constexpr (!has_reference_parser)
            {
                if (compressed.empty() && !input_is_rle)
                {
                    auto const selected{detail::find_best_fast_match(bytes, effective_parameters_.hash_log,
                        effective_parameters_.minimum_match)};
                    if (selected)
                    {
                        auto candidate{detail::encode_single_match_block(bytes, *selected)};
                        if (candidate.size() < bytes.size())
                        {
                            compressed = std::move(candidate);
                        }
                    }
                }
            }
            auto const is_rle = input_is_rle && (!has_reference_parser ||
                (block_count_ != 0U && !compressed.empty() && compressed.size() < 25U));

            auto const block_type{is_rle ? 1U : compressed.empty() ? 0U : 2U};
            auto const stored_size{is_rle || compressed.empty() ? block_size : compressed.size()};
            auto const block_header{(static_cast<std::uint32_t>(stored_size) << 3U) |
                (block_type << 1U) | (actual_last ? 1U : 0U)};
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
            auto const emitted_size{is_rle ? 4U : stored_size + 3U};
            compression_savings_ += static_cast<std::int64_t>(block_size) -
                static_cast<std::int64_t>(emitted_size);
            auto const remaining{buffered_size_ - block_size};
            std::ranges::move(block_buffer_.begin() + static_cast<std::ptrdiff_t>(block_size),
                block_buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_size_), block_buffer_.begin());
            buffered_size_ = remaining;
            ++block_count_;
        }

        [[nodiscard]] static auto fast_split_size(std::span<std::uint8_t const> bytes) -> std::size_t
        {
            constexpr std::size_t sample_size{512U};
            constexpr std::size_t half_block{maximum_block_size / 2U};
            std::array<std::uint32_t, 256> beginning{};
            std::array<std::uint32_t, 256> middle{};
            std::array<std::uint32_t, 256> ending{};
            auto add_sample = [&bytes](auto& histogram, std::size_t start)
            {
                for (auto const byte : bytes.subspan(start, sample_size))
                {
                    ++histogram[byte];
                }
            };
            auto distance = [](auto const& first, auto const& second)
            {
                std::uint64_t result{};
                for (std::size_t symbol{}; symbol < first.size(); ++symbol)
                {
                    auto const difference{static_cast<std::int64_t>(first[symbol]) *
                        static_cast<std::int64_t>(sample_size) -
                        static_cast<std::int64_t>(second[symbol]) * static_cast<std::int64_t>(sample_size)};
                    result += static_cast<std::uint64_t>(difference < 0 ? -difference : difference);
                }
                return result;
            };

            add_sample(beginning, 0U);
            add_sample(ending, bytes.size() - sample_size);
            auto const threshold{static_cast<std::uint64_t>(sample_size) * sample_size * 14U / 16U};
            if (distance(beginning, ending) < threshold)
            {
                return bytes.size();
            }
            add_sample(middle, half_block - sample_size / 2U);
            auto const distance_from_beginning{distance(beginning, middle)};
            auto const distance_from_end{distance(ending, middle)};
            auto const difference{distance_from_beginning > distance_from_end ?
                distance_from_beginning - distance_from_end : distance_from_end - distance_from_beginning};
            if (difference < static_cast<std::uint64_t>(sample_size) * sample_size / 3U)
            {
                return half_block;
            }
            return distance_from_beginning > distance_from_end ? half_block / 2U : half_block + half_block / 2U;
        }

        [[nodiscard]] static auto chunk_split_size(
            std::span<std::uint8_t const> bytes,
            std::size_t sampling_rate,
            unsigned hash_log) -> std::size_t
        {
            constexpr std::size_t chunk_size{8U * 1024U};
            struct fingerprint
            {
                std::array<std::uint32_t, 1024> events{};
                std::size_t event_count{};
            };
            auto record = [&bytes, sampling_rate, hash_log](fingerprint& result, std::size_t start)
            {
                result = {};
                constexpr auto sample_limit{chunk_size - 2U + 1U};
                for (std::size_t position{}; position < sample_limit; position += sampling_rate)
                {
                    auto const hash{hash_log == 8U ? static_cast<std::uint32_t>(bytes[start + position]) :
                        (static_cast<std::uint32_t>(bytes[start + position]) |
                            (static_cast<std::uint32_t>(bytes[start + position + 1U]) << 8U)) *
                            0x9E3779B9U >> (32U - hash_log)};
                    ++result.events[hash];
                }
                result.event_count = sample_limit / sampling_rate;
            };
            auto different = [hash_log](fingerprint const& past, fingerprint const& recent, int penalty)
            {
                std::uint64_t deviation{};
                for (std::size_t hash{}; hash < (std::size_t{1} << hash_log); ++hash)
                {
                    auto const difference{static_cast<std::int64_t>(past.events[hash]) *
                        static_cast<std::int64_t>(recent.event_count) -
                        static_cast<std::int64_t>(recent.events[hash]) *
                        static_cast<std::int64_t>(past.event_count)};
                    deviation += static_cast<std::uint64_t>(difference < 0 ? -difference : difference);
                }
                auto const probability_half{static_cast<std::uint64_t>(past.event_count) * recent.event_count};
                auto const threshold{probability_half * static_cast<std::uint64_t>(14 + penalty) / 16U};
                return deviation >= threshold;
            };
            auto merge = [hash_log](fingerprint& destination, fingerprint const& source)
            {
                for (std::size_t hash{}; hash < (std::size_t{1} << hash_log); ++hash)
                {
                    destination.events[hash] += source.events[hash];
                }
                destination.event_count += source.event_count;
            };

            fingerprint past;
            fingerprint recent;
            record(past, 0U);
            auto penalty{3};
            for (auto position{chunk_size}; position <= bytes.size() - chunk_size; position += chunk_size)
            {
                record(recent, position);
                if (different(past, recent, penalty))
                {
                    return position;
                }
                merge(past, recent);
                if (penalty > 0)
                {
                    --penalty;
                }
            }
            return bytes.size();
        }

        Callback callback_;
        std::array<std::uint8_t, Parameters.block_size> block_buffer_{};
        std::size_t buffered_size_{};
        std::uint64_t source_size_{};
        std::uint64_t encoded_size_{};
        detail::xxhash64 checksum_{};
        stream_status status_{stream_status::ready};
        bool frame_started_{};
        std::size_t block_count_{};
        std::int64_t compression_savings_{};
        std::vector<std::uint8_t> history_;
        detail::parsed_block parsed_;
        match_state_type match_state_{make_match_state()};
    };

    template <typename Callback>
    zstd_compress(Callback) -> zstd_compress<compression_parameters{}, std::decay_t<Callback>>;

    template <compression_parameters Parameters = {}, typename Callback>
    [[nodiscard]] auto make_zstd_compress(Callback&& callback)
    {
        return zstd_compress<Parameters, std::decay_t<Callback>>{std::forward<Callback>(callback)};
    }
}
