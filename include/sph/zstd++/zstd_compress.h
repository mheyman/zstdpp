#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
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
#ifdef SPH_ZSTDPP_TRACE_PHASES
    struct compression_phase_trace
    {
        std::uint64_t parse_ns{};
        std::uint64_t encode_ns{};
        std::uint64_t emit_ns{};
    };
    inline compression_phase_trace compression_trace{};
#endif
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
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
        // Parser-time literal collection removes the encoder's late gather
        // pass only for the measured short level-2 double-fast specialization.
        // Other presets retain their original parser and generated code.
        static constexpr bool collect_double_fast_literals_{
            effective_parameters_.strategy == compression_strategy::double_fast &&
            effective_parameters_.window_log == 18U &&
            effective_parameters_.hash_log == 14U &&
            effective_parameters_.chain_log == 14U &&
            effective_parameters_.minimum_match == 5U &&
            Parameters.compression_level == 2 &&
            effective_parameters_.pledged_source_size != 0U &&
            effective_parameters_.pledged_source_size <= 262144U};
#else
        static constexpr bool collect_double_fast_literals_{false};
#endif
#if defined(_MSC_VER)
        static constexpr bool prepared_double_fast_sequences_{
            effective_parameters_.strategy == compression_strategy::double_fast &&
            Parameters.compression_level == 3};
#else
        static constexpr bool prepared_double_fast_sequences_{false};
#endif

        using match_state_type = std::conditional_t<
            effective_parameters_.strategy == compression_strategy::fast,
            detail::fast_match_state<effective_parameters_.window_log,
                effective_parameters_.hash_log, effective_parameters_.minimum_match,
                effective_parameters_.target_length>,
            std::conditional_t<
                effective_parameters_.strategy == compression_strategy::double_fast,
                detail::double_fast_match_state<effective_parameters_.window_log,
                    effective_parameters_.hash_log, effective_parameters_.chain_log,
                    effective_parameters_.minimum_match>,
                std::conditional_t<
                    effective_parameters_.strategy == compression_strategy::greedy ||
                    effective_parameters_.strategy == compression_strategy::lazy ||
                    effective_parameters_.strategy == compression_strategy::lazy2,
                    std::conditional_t<effective_parameters_.strategy == compression_strategy::greedy,
                        detail::greedy_match_state<0U, false,
                            effective_parameters_.window_log, effective_parameters_.hash_log,
                            effective_parameters_.chain_log, effective_parameters_.search_log,
                            effective_parameters_.minimum_match, (effective_parameters_.window_log > 14U)>,
                        std::conditional_t<effective_parameters_.strategy == compression_strategy::lazy,
                            detail::greedy_match_state<1U, false,
                                effective_parameters_.window_log, effective_parameters_.hash_log,
                                effective_parameters_.chain_log, effective_parameters_.search_log,
                                effective_parameters_.minimum_match, (effective_parameters_.window_log > 14U)>,
                            detail::greedy_match_state<2U, false,
                                effective_parameters_.window_log, effective_parameters_.hash_log,
                                effective_parameters_.chain_log, effective_parameters_.search_log,
                                effective_parameters_.minimum_match, (effective_parameters_.window_log > 14U)>>>,
                    std::conditional_t<
                        effective_parameters_.strategy >= compression_strategy::binary_tree_lazy2,
                        detail::greedy_match_state<2U, true,
                            effective_parameters_.window_log, effective_parameters_.hash_log,
                            effective_parameters_.chain_log, effective_parameters_.search_log,
                            effective_parameters_.minimum_match, false,
                            (effective_parameters_.strategy == compression_strategy::binary_tree_optimal)>,
                        detail::empty_match_state>>>>;

        static_assert(Parameters.block_size > 0 && Parameters.block_size <= maximum_block_size,
            "zstd_compress block_size must be in [1, 128 KiB]");
        static_assert(effective_parameters_.window_log >= 10 && effective_parameters_.window_log <= 31,
            "zstd_compress window_log must be in [10, 31]");
        static_assert(Parameters.worker_count == 0,
            "the header-only zstd compressor does not yet implement multi-threaded frames");

    public:
        explicit zstd_compress(Callback callback) : callback_{std::move(callback)}
        {
            auto const maximum_sequences{Parameters.block_size / 3U + 1U};
            parsed_.sequences.reserve(maximum_sequences);
            compression_workspace_.reserve(Parameters.block_size, maximum_sequences);
            if constexpr (effective_parameters_.strategy != compression_strategy::fast &&
                effective_parameters_.strategy != compression_strategy::double_fast &&
                effective_parameters_.strategy != compression_strategy::greedy &&
                effective_parameters_.strategy != compression_strategy::lazy &&
                effective_parameters_.strategy != compression_strategy::lazy2 &&
                effective_parameters_.strategy != compression_strategy::binary_tree_lazy2)
            {
                auto const table_size{std::size_t{1} << effective_parameters_.hash_log};
                compression_workspace_.fast_latest.positions.reserve(table_size);
                compression_workspace_.fast_latest.generations.reserve(table_size);
            }
            if constexpr (effective_parameters_.strategy >= compression_strategy::binary_tree_optimal)
            {
                compression_workspace_.optimal.reserve();
            }
            // Entropy coding can transiently exceed the raw block by headers
            // before the raw-block fallback wins.  Reserve the bounded frame
            // overhead up front so reset/reuse never grows this hot buffer.
            compressed_.reserve(Parameters.block_size + Parameters.block_size / 128U + 64U);
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
                history_.insert(history_.end(), input.begin() + static_cast<std::ptrdiff_t>(offset),
                    input.begin() + static_cast<std::ptrdiff_t>(offset + count));
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

        /** Compresses a complete frame while borrowing input for the duration of this call. */
        void compress_frame(std::span<std::uint8_t const> input)
        {
            if (status_ != stream_status::ready || frame_started_ || buffered_size_ != 0U ||
                !history_.empty())
            {
                throw zstd_error{error_code::invalid_state,
                    "compress_frame() requires a new or reset zstd_compress"};
            }

            begin_frame();
            if constexpr (Parameters.checksum)
            {
                checksum_.update(input);
            }
            source_size_ = input.size();
            if constexpr (Parameters.pledged_source_size != unknown_content_size)
            {
                if (source_size_ != Parameters.pledged_source_size)
                {
                    status_ = stream_status::failed;
                    throw zstd_error{error_code::source_size_mismatch,
                        "input size does not match zstd_compress pledged_source_size"};
                }
            }

            std::size_t block_begin{};
            do
            {
                auto const available{input.size() - block_begin};
                auto const candidate_size{std::min<std::size_t>(Parameters.block_size, available)};
                auto const block_size{select_block_size(input.subspan(block_begin, candidate_size))};
                auto const last{block_begin + block_size == input.size()};
                emit_block(input, block_begin, block_size, last);
                block_begin += block_size;
            }
            while (block_begin != input.size());

            if constexpr (Parameters.checksum)
            {
                std::array<std::uint8_t, 4> encoded_checksum{};
                detail::write_u32(encoded_checksum, static_cast<std::uint32_t>(checksum_.digest()));
                emit(encoded_checksum);
            }
            status_ = stream_status::finished;
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
            if constexpr (prepared_double_fast_sequences_)
                parsed_.sequences.reserve(Parameters.block_size / 3U + 1U);
            buffered_size_ = 0;
            source_size_ = 0;
            encoded_size_ = 0;
            frame_started_ = false;
            block_count_ = 0;
            compression_savings_ = 0;
            history_.clear();
            match_state_.reset();
            compression_workspace_.reset_entropy_history();
            compression_workspace_.optimal.initialized = false;
            compression_workspace_.optimal.repeat_offsets = {1U, 4U, 8U};
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
            else if constexpr (effective_parameters_.strategy == compression_strategy::greedy ||
                effective_parameters_.strategy == compression_strategy::lazy ||
                effective_parameters_.strategy == compression_strategy::lazy2)
            {
                return match_state_type{effective_parameters_.window_log,
                    effective_parameters_.hash_log, effective_parameters_.chain_log,
                    effective_parameters_.search_log, effective_parameters_.minimum_match,
                    effective_parameters_.target_length};
            }
            else if constexpr (effective_parameters_.strategy >= compression_strategy::binary_tree_lazy2)
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

        [[nodiscard]] auto select_block_size(std::span<std::uint8_t const> bytes) const -> std::size_t
        {
            auto block_size{bytes.size()};
            if constexpr (effective_parameters_.strategy == compression_strategy::fast)
            {
                if (bytes.size() == maximum_block_size && compression_savings_ >= 3)
                {
                    block_size = fast_split_size(bytes);
                }
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::double_fast)
            {
                if (bytes.size() == maximum_block_size && compression_savings_ >= 3)
                {
                    block_size = chunk_split_size<43U, 8U>(bytes);
                }
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::greedy ||
                effective_parameters_.strategy == compression_strategy::lazy)
            {
                if (bytes.size() == maximum_block_size && compression_savings_ >= 3)
                {
                    block_size = chunk_split_size<11U, 9U>(bytes);
                }
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::lazy2 ||
                effective_parameters_.strategy == compression_strategy::binary_tree_lazy2)
            {
                if (bytes.size() == maximum_block_size && compression_savings_ >= 3)
                {
                    block_size = chunk_split_size<5U, 10U>(bytes);
                }
            }
            else if constexpr (effective_parameters_.strategy == compression_strategy::binary_tree_optimal ||
                effective_parameters_.strategy == compression_strategy::binary_tree_ultra ||
                effective_parameters_.strategy == compression_strategy::binary_tree_ultra2)
            {
                // Reference optimal strategies use split level 4: inspect every
                // two-byte sample, after an earlier block proves compressibility.
                if (bytes.size() == maximum_block_size && compression_savings_ >= 3)
                {
                    // All uniform chunks have identical fingerprints, so no
                    // split can be selected. Verify every byte before skipping.
                    block_size = detail::is_rle(bytes) ? bytes.size() : chunk_split_size<1U, 10U>(bytes);
                }
            }
            return block_size;
        }

        void emit_block(bool last)
        {
            auto const block_begin{history_.size() - buffered_size_};
            auto const buffered{std::span<std::uint8_t const>{history_}.last(buffered_size_)};
            auto const block_size{select_block_size(buffered)};
            auto const actual_last{last && block_size == buffered_size_};
            emit_block(history_, block_begin, block_size, actual_last);
            buffered_size_ -= block_size;
        }

        void emit_block(std::span<std::uint8_t const> input, std::size_t block_begin,
            std::size_t block_size, bool last)
        {
            auto const bytes{input.subspan(block_begin, block_size)};
            auto const initial_optimal_repeats{compression_workspace_.optimal.repeat_offsets};
            // Fast parsers defer the full RLE scan until the encoded-size gate can pass.
            // Deeper parsers retain the eager scan because its sequential pass warms the input cache.
            constexpr bool lazy_rle_check =
                effective_parameters_.strategy == compression_strategy::fast ||
                effective_parameters_.strategy == compression_strategy::double_fast ||
                effective_parameters_.strategy == compression_strategy::greedy;
            auto const rle_checked{!lazy_rle_check || block_count_ != 0U};
            bool input_is_rle{rle_checked && detail::is_rle(bytes)};
            compressed_.clear();
            auto& compressed{compressed_};
            constexpr bool has_reference_parser =
                effective_parameters_.strategy == compression_strategy::fast ||
                effective_parameters_.strategy == compression_strategy::double_fast ||
                effective_parameters_.strategy == compression_strategy::greedy ||
                effective_parameters_.strategy == compression_strategy::lazy ||
                effective_parameters_.strategy == compression_strategy::lazy2 ||
                effective_parameters_.strategy >= compression_strategy::binary_tree_lazy2;
            if constexpr (has_reference_parser)
            {
                // Optimal strategies must update their tree and price history
                // even when the encoded block will use RLE framing.
                if (!input_is_rle || block_count_ == 0U ||
                    effective_parameters_.strategy >= compression_strategy::binary_tree_optimal)
                {
#ifdef SPH_ZSTDPP_TRACE_PHASES
                    auto const parse_start{std::chrono::steady_clock::now()};
#endif
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
                    // GCC 13 measurements favor parser-time collection for
                    // minMatch 7; minMatch 6 benefits from encoder collection.
                    if constexpr (effective_parameters_.strategy == compression_strategy::fast &&
                        effective_parameters_.minimum_match >= 7U)
                        parsed_ = match_state_.template parse<true>(input, block_begin, bytes.size(),
                            std::move(parsed_), rle_checked ? std::optional<bool>{input_is_rle} : std::nullopt,
                            compression_workspace_.literals);
                    else if constexpr (collect_double_fast_literals_)
                        parsed_ = match_state_.parse_collecting(input, block_begin, bytes.size(),
                            std::move(parsed_), rle_checked ? std::optional<bool>{input_is_rle} : std::nullopt,
                            compression_workspace_.literals);
                    else
#endif
                    if constexpr (effective_parameters_.strategy >= compression_strategy::binary_tree_optimal)
                    {
                        if constexpr (effective_parameters_.strategy == compression_strategy::binary_tree_ultra2)
                        {
                            if (block_begin == 0U && bytes.size() > 8U &&
                                !compression_workspace_.optimal.initialized)
                            {
                                // Ultra2 seeds entropy prices with a preliminary
                                // first-block parse, then forgets only its history.
                                parsed_ = detail::parse_optimal_block(match_state_, input, block_begin,
                                    bytes.size(), compression_workspace_, std::move(parsed_), true,
                                    effective_parameters_.target_length);
                                match_state_.reset();
                                compression_workspace_.optimal.repeat_offsets = initial_optimal_repeats;
                            }
                        }
                        parsed_ = detail::parse_optimal_block(match_state_, input, block_begin,
                            bytes.size(), compression_workspace_, std::move(parsed_),
                            effective_parameters_.strategy != compression_strategy::binary_tree_optimal,
                            effective_parameters_.target_length);
                    }
                    else
                    {
                        parsed_ = match_state_.parse(input, block_begin, bytes.size(), std::move(parsed_),
                            rle_checked ? std::optional<bool>{input_is_rle} : std::nullopt);
                    }
#ifdef SPH_ZSTDPP_TRACE_PHASES
                    compression_trace.parse_ns += static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - parse_start).count());
#endif
                    if (!parsed_.sequences.empty())
                    {
                        if constexpr (effective_parameters_.strategy >= compression_strategy::binary_tree_optimal &&
                            effective_parameters_.window_log >= 17U)
                        {
                            bool estimates_valid{true};
                            auto estimate = [&](detail::parsed_block_view const& range)
                            {
                                auto const size{detail::estimate_partition_size(input, range, compression_workspace_)};
                                estimates_valid = estimates_valid && size.has_value();
                                return size.value_or(std::numeric_limits<std::size_t>::max() / 4U);
                            };
                            auto const partitions{detail::derive_sequence_partitions(parsed_, estimate)};
#ifdef SPH_ZSTDPP_TRACE_OPTIMAL
                            std::fprintf(stderr, "P %zu", parsed_.sequences.size());
                            for (std::size_t index{}; index < partitions.count; ++index)
                                std::fprintf(stderr, " %zu", partitions.ends[index]);
                            std::fprintf(stderr, "\n");
#endif
                            if (estimates_valid && partitions.count > 1U)
                            {
                                emit_partitions(input, partitions, initial_optimal_repeats, last);
                                return;
                            }
                        }
#ifdef SPH_ZSTDPP_TRACE_PHASES
                        auto const encode_start{std::chrono::steady_clock::now()};
#endif
                        detail::encode_sequences_block<
                            (effective_parameters_.strategy >= compression_strategy::lazy)
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
                            // Grouped fast-path table/literal selection and
                            // short level-3 entropy policy.
                            , (effective_parameters_.strategy == compression_strategy::fast ||
                               (effective_parameters_.strategy == compression_strategy::double_fast &&
                                effective_parameters_.pledged_source_size != 0U &&
                                Parameters.compression_level == 3 &&
                                effective_parameters_.pledged_source_size <= 65536U))
                            , ((effective_parameters_.strategy == compression_strategy::fast &&
                               effective_parameters_.minimum_match >= 7U) ||
                               collect_double_fast_literals_)
#else
                            , false, false
#endif
                            , (effective_parameters_.strategy >= compression_strategy::binary_tree_ultra)
                            , (effective_parameters_.strategy >= compression_strategy::binary_tree_ultra ?
                                static_cast<unsigned>(effective_parameters_.strategy) - 1U : 6U)
                            , (effective_parameters_.strategy == compression_strategy::double_fast)
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
                            // Balanced controls favor the wide sequence writer
                            // on short pledged fast frames; the 1-MiB mixed
                            // corpus consistently favors the existing writer.
                            , (effective_parameters_.strategy == compression_strategy::fast &&
                               effective_parameters_.pledged_source_size != 0U &&
                               effective_parameters_.pledged_source_size <= 262144U)
                            // Packing LL/ML extra bits wins on the measured
                            // 64-KiB level-1 fast specialization. Larger
                            // frames had mixed or negative paired results.
                            , (effective_parameters_.strategy == compression_strategy::fast &&
                               Parameters.compression_level == 1 &&
                               effective_parameters_.pledged_source_size != 0U &&
                               effective_parameters_.pledged_source_size <= 65536U)
                            // Keep the existing fast and short level-3 writes.
                            , (effective_parameters_.strategy == compression_strategy::fast ||
                               (effective_parameters_.strategy == compression_strategy::double_fast &&
                                effective_parameters_.pledged_source_size != 0U &&
                                Parameters.compression_level == 3 &&
                                effective_parameters_.pledged_source_size <= 65536U))
                            // Level 2 chooses grouped or scalar sequence
                            // writes once per block, leaving table and
                            // Huffman policy unchanged.
                            , (effective_parameters_.strategy == compression_strategy::double_fast &&
                               Parameters.compression_level == 2 &&
                               effective_parameters_.pledged_source_size != 0U &&
                               effective_parameters_.pledged_source_size <= 262144U)
                            // Dense-sequence blocks use the wider grouped
                            // writer; sparse blocks keep the scalar writer.
                            , (effective_parameters_.strategy == compression_strategy::double_fast &&
                               Parameters.compression_level == 2 &&
                               effective_parameters_.pledged_source_size != 0U &&
                               effective_parameters_.pledged_source_size <= 262144U)
#else
#if defined(_MSC_VER)
                            // On the measured level-3 double-fast path, emit
                            // OF/ML/LL FSE state bits as one bounded append.
                            , false, false, (effective_parameters_.strategy ==
                                compression_strategy::double_fast &&
                                Parameters.compression_level == 3), false, false
#else
                            , false, false, false, false, false
#endif
#endif
                            >(
                            input, parsed_, compression_workspace_, compressed);
#ifdef SPH_ZSTDPP_TRACE_PHASES
                        compression_trace.encode_ns += static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - encode_start).count());
#endif
                        if (compressed.size() >= bytes.size())
                        {
                            compressed.clear();
                        }
                    }
                }
            }
            else
            {
                if (compressed.empty() && !input_is_rle)
                {
                    auto const selected{detail::find_best_fast_match(bytes, effective_parameters_.hash_log,
                        effective_parameters_.minimum_match, &compression_workspace_.fast_latest)};
                    if (selected)
                    {
                        detail::encode_single_match_block(bytes, *selected,
                            compression_workspace_, compressed);
                        if (compressed.size() >= bytes.size())
                        {
                            compressed.clear();
                        }
                    }
                }
            }
            if constexpr (lazy_rle_check)
            {
                if (block_count_ != 0U && !compressed.empty() && compressed.size() < 25U)
                {
                    input_is_rle = detail::is_rle(bytes);
                }
            }
            auto const is_rle = input_is_rle && (!has_reference_parser ||
                (block_count_ != 0U && (compressed.empty() || compressed.size() < 25U)));

            auto const block_type{is_rle ? 1U : compressed.empty() ? 0U : 2U};
            auto const stored_size{is_rle || compressed.empty() ? block_size : compressed.size()};
            auto const block_header{(static_cast<std::uint32_t>(stored_size) << 3U) |
                (block_type << 1U) | (last ? 1U : 0U)};
            std::array<std::uint8_t, 3> encoded_header{
                static_cast<std::uint8_t>(block_header),
                static_cast<std::uint8_t>(block_header >> 8U),
                static_cast<std::uint8_t>(block_header >> 16U)
            };
#ifdef SPH_ZSTDPP_TRACE_PHASES
            auto const emit_start{std::chrono::steady_clock::now()};
#endif
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
#ifdef SPH_ZSTDPP_TRACE_PHASES
            compression_trace.emit_ns += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - emit_start).count());
#endif
            auto const emitted_size{is_rle ? 4U : stored_size + 3U};
            compression_savings_ += static_cast<std::int64_t>(block_size) -
                static_cast<std::int64_t>(emitted_size);
            ++block_count_;
        }

        void emit_partitions(std::span<std::uint8_t const> input,
            detail::sequence_partitions const& partitions,
            std::array<std::uint32_t, 3> encoder_repeats, bool last)
        {
            auto decoder_repeats{encoder_repeats};
            auto& workspace{compression_workspace_};
            detail::parsed_block_view const full{parsed_};
            std::size_t begin{};
            for (std::size_t index{}; index < partitions.count; ++index)
            {
                auto const end{partitions.ends[index]};
                auto const part{full.subblock(begin, end)};
                auto const bytes{input.subspan(part.input_begin,
                    part.trailing_literal_position + part.trailing_literal_length - part.input_begin)};
                auto const saved_repeats{decoder_repeats};
                auto const saved_sequences{workspace.sequence_histories};
                auto const saved_lengths{workspace.huffman_repeat_lengths};
                auto const saved_frequencies{workspace.huffman_repeat_frequencies};
                auto const saved_log{workspace.huffman_repeat_log};
                auto const saved_maximum{workspace.huffman_repeat_maximum_symbol};
                auto const saved_size{workspace.huffman_repeat_literal_size};
                auto const saved_valid{workspace.huffman_repeat_valid};
                detail::reconcile_partition_repeats(
                    std::span<detail::parsed_sequence>{parsed_.sequences}.subspan(begin, end - begin),
                    encoder_repeats, decoder_repeats);
                detail::encode_sequences_block<true, false, false,
                    (effective_parameters_.strategy >= compression_strategy::binary_tree_ultra),
                    (effective_parameters_.strategy >= compression_strategy::binary_tree_ultra ?
                        static_cast<unsigned>(effective_parameters_.strategy) - 1U : 6U)>(
                    input, part, workspace, compressed_);
                constexpr auto gain_log{effective_parameters_.strategy >= compression_strategy::binary_tree_ultra ?
                    static_cast<unsigned>(effective_parameters_.strategy) - 1U : 6U};
                auto const minimum_gain{(bytes.size() >> gain_log) + 2U};
                if (bytes.size() <= minimum_gain || compressed_.size() >= bytes.size() - minimum_gain)
                    compressed_.clear();
                auto const rle{block_count_ != 0U && compressed_.size() < 25U && detail::is_rle(bytes)};
                if (rle || compressed_.empty())
                {
                    decoder_repeats = saved_repeats;
                    workspace.sequence_histories = saved_sequences;
                    workspace.huffman_repeat_lengths = saved_lengths;
                    workspace.huffman_repeat_frequencies = saved_frequencies;
                    workspace.huffman_repeat_log = saved_log;
                    workspace.huffman_repeat_maximum_symbol = saved_maximum;
                    workspace.huffman_repeat_literal_size = saved_size;
                    workspace.huffman_repeat_valid = saved_valid;
                    workspace.huffman_repeat_codes_valid = false;
                }
                auto const type{rle ? 1U : compressed_.empty() ? 0U : 2U};
                auto const stored_size{type == 2U ? compressed_.size() : bytes.size()};
                auto const header{(static_cast<std::uint32_t>(stored_size) << 3U) |
                    (type << 1U) | (last && index + 1U == partitions.count ? 1U : 0U)};
                std::array<std::uint8_t, 3> const encoded_header{
                    static_cast<std::uint8_t>(header), static_cast<std::uint8_t>(header >> 8U),
                    static_cast<std::uint8_t>(header >> 16U)};
                emit(encoded_header);
                if (rle) emit(bytes.first<1>());
                else if (type == 2U) emit(compressed_);
                else emit(bytes);
                compression_savings_ += static_cast<std::int64_t>(bytes.size()) -
                    static_cast<std::int64_t>(rle ? 4U : stored_size + 3U);
                ++block_count_;
                begin = end;
            }
            workspace.optimal.repeat_offsets = decoder_repeats;
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

        template <std::size_t SamplingRate, unsigned HashLog>
        [[nodiscard]] static auto chunk_split_size(std::span<std::uint8_t const> bytes) -> std::size_t
        {
            static_assert(HashLog >= 8U && HashLog <= 10U);
            constexpr std::size_t chunk_size{8U * 1024U};
            struct fingerprint
            {
                std::array<std::uint32_t, 1024> events{};
                std::size_t event_count{};
            };
            auto record = [&bytes](fingerprint& result, std::size_t start)
            {
                std::ranges::fill(result.events.begin(),
                    result.events.begin() + (std::size_t{1} << HashLog), 0U);
                constexpr auto sample_limit{chunk_size - 2U + 1U};
                for (std::size_t position{}; position < sample_limit; position += SamplingRate)
                {
                    auto const hash{HashLog == 8U ? static_cast<std::uint32_t>(bytes[start + position]) :
                        (static_cast<std::uint32_t>(bytes[start + position]) |
                            (static_cast<std::uint32_t>(bytes[start + position + 1U]) << 8U)) *
                            0x9E3779B9U >> (32U - HashLog)};
                    ++result.events[hash];
                }
                result.event_count = sample_limit / SamplingRate;
            };
            auto different = [](fingerprint const& past, fingerprint const& recent, int penalty)
            {
                std::uint64_t deviation{};
                for (std::size_t hash{}; hash < (std::size_t{1} << HashLog); ++hash)
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
            auto merge = [](fingerprint& destination, fingerprint const& source)
            {
                for (std::size_t hash{}; hash < (std::size_t{1} << HashLog); ++hash)
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
        std::size_t buffered_size_{};
        std::uint64_t source_size_{};
        std::uint64_t encoded_size_{};
        detail::xxhash64 checksum_{};
        stream_status status_{stream_status::ready};
        bool frame_started_{};
        std::size_t block_count_{};
        std::int64_t compression_savings_{};
        std::vector<std::uint8_t> history_;
        // Compact storage benefits the minMatch-7 fast parser, but the minMatch-6
        // path and deeper strategies retain the original sequence representation.
        std::conditional_t<effective_parameters_.strategy == compression_strategy::fast &&
            effective_parameters_.minimum_match >= 7U,
            detail::compact_parsed_block,
            std::conditional_t<prepared_double_fast_sequences_,
                detail::prepared_parsed_block, detail::parsed_block>
            > parsed_;
        // Alignment benefits the four-byte double-fast literal distribution.
        // Larger-source five-byte presets retain ordinary storage: their
        // measured copy/layout tradeoff did not reproduce that improvement.
        // GCC 13 fast minMatch3..6 also avoids a measured raw-literal copy
        // alignment penalty. Keep unmeasured compilers and minMatch7 unchanged.
        detail::basic_compression_workspace<
            (effective_parameters_.strategy == compression_strategy::double_fast &&
            effective_parameters_.minimum_match == 4U)
#if defined(__GNUC__) && __GNUC__ == 13 && !defined(__clang__)
            || (effective_parameters_.strategy == compression_strategy::fast &&
            effective_parameters_.minimum_match < 7U)
#endif
            > compression_workspace_;
        std::vector<std::uint8_t> compressed_;
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
