#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <numeric>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef SPH_ZSTDPP_TRACE_PHASES
#include <chrono>
#endif

#ifdef SPH_ZSTDPP_TRACE_OPTIMAL
#include <cstdio>
#endif

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#include <emmintrin.h>
#endif

#include <sph/zstd++/detail/zstd_entropy.h>
#include <sph/zstd++/detail/compact_sequences.h>

namespace sph::zstd::detail
{
#ifdef SPH_ZSTDPP_TRACE_PHASES
    struct entropy_phase_trace
    {
        std::uint64_t gather_ns{}, literals_ns{}, tables_ns{}, bits_ns{};
        std::uint64_t huffman_histogram_ns{}, huffman_tree_ns{}, huffman_weights_ns{};
        std::uint64_t huffman_selection_ns{}, huffman_streams_ns{}, huffman_emit_ns{};
        std::uint64_t huffman_new_blocks{}, huffman_repeat_blocks{}, huffman_discarded_weights{};
        std::uint64_t huffman_histogram_reject_blocks{}, huffman_histogram_reject_bytes{};
        std::uint64_t huffman_sample_eligible_blocks{};
        std::uint64_t blocks{}, sequences{}, literal_bytes{};
    };
    inline entropy_phase_trace entropy_trace{};

    inline auto entropy_trace_elapsed(std::chrono::steady_clock::time_point start)
        -> std::uint64_t
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count());
    }
#endif
    // Reference zstd uses this XXH3-style mixer to salt row-hash tables when a
    // compression context is reset. Keep it separate from the ordinary hash
    // functions so the eventual row matcher can share the exact transition.
    [[nodiscard]] constexpr auto row_hash_bitmix(std::uint64_t value,
        std::uint64_t length) noexcept -> std::uint64_t
    {
        value ^= std::rotr(value, 49U) ^ std::rotr(value, 24U);
        value *= 0x9FB21C651E98DF25ULL;
        value ^= (value >> 35U) + length;
        value *= 0x9FB21C651E98DF25ULL;
        return value ^ (value >> 28U);
    }

    [[nodiscard]] constexpr auto advance_row_hash_salt(std::uint64_t salt,
        std::uint32_t entropy) noexcept -> std::uint64_t
    {
        return row_hash_bitmix(salt, 8U) ^ row_hash_bitmix(entropy, 4U);
    }

    inline std::size_t cache_aligned_allocation_count{};
    inline std::size_t cache_aligned_deallocation_count{};

    template <typename T>
    class cache_aligned_allocator
    {
    public:
        using value_type = T;

        constexpr cache_aligned_allocator() noexcept = default;

        template <typename U>
        constexpr cache_aligned_allocator(cache_aligned_allocator<U> const&) noexcept
        {
        }

        [[nodiscard]] auto allocate(std::size_t count) -> T*
        {
            if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
            {
                throw std::bad_array_new_length{};
            }
            ++cache_aligned_allocation_count;
            return static_cast<T*>(::operator new(
                count * sizeof(T), std::align_val_t{cache_line_size}));
        }

        void deallocate(T* memory, std::size_t) noexcept
        {
            ++cache_aligned_deallocation_count;
            ::operator delete(memory, std::align_val_t{cache_line_size});
        }

        template <typename U>
        [[nodiscard]] constexpr auto operator==(cache_aligned_allocator<U> const&) const noexcept
            -> bool
        {
            return true;
        }

    private:
        static constexpr std::size_t cache_line_size{64U};
    };

    using match_table = std::vector<std::uint32_t, cache_aligned_allocator<std::uint32_t>>;
    using row_tag_table = std::vector<std::uint8_t, cache_aligned_allocator<std::uint8_t>>;

    struct packed_match_entry
    {
        std::array<std::uint8_t, 5> bytes{};
    };
    static_assert(sizeof(packed_match_entry) == 5U);

    struct packed_index_view
    {
        packed_match_entry* entries{};

        struct reference
        {
            packed_match_entry* entry{};

            [[nodiscard]] operator std::uint32_t() const noexcept
            {
                std::uint32_t value{};
                std::memcpy(&value, entry->bytes.data(), sizeof(value));
                return value;
            }

            auto operator=(std::uint32_t value) noexcept -> reference&
            {
                std::memcpy(entry->bytes.data(), &value, sizeof(value));
                return *this;
            }

            auto operator=(reference const& other) noexcept -> reference&
            {
                return *this = static_cast<std::uint32_t>(other);
            }
        };

        [[nodiscard]] auto operator[](std::size_t index) const noexcept -> reference
        {
            return {entries + index};
        }
    };

    struct packed_tag_view
    {
        packed_match_entry* entries{};

        struct reference
        {
            packed_match_entry* entry{};

            [[nodiscard]] operator std::uint8_t() const noexcept
            {
                return entry->bytes[4];
            }

            auto operator=(std::uint8_t value) noexcept -> reference&
            {
                entry->bytes[4] = value;
                return *this;
            }

            auto operator=(reference const& other) noexcept -> reference&
            {
                return *this = static_cast<std::uint8_t>(other);
            }
        };

        [[nodiscard]] auto operator[](std::size_t index) const noexcept -> reference
        {
            return {entries + index};
        }
    };

    using packed_match_table = std::vector<packed_match_entry,
        cache_aligned_allocator<packed_match_entry>>;
    struct empty_match_table {};
    using symbol_counts = std::array<std::uint32_t, 256>;
    using symbol_count_workspace = std::array<symbol_counts, 4>;

    // Match reference zstd's suspect-incompressible literal sampling.
    // Each end has a separate histogram: sum their maxima, not merged counts.
    [[nodiscard]] inline auto skip_huffman_from_samples(
        std::span<std::uint8_t const> literals, std::size_t sequence_count,
        symbol_count_workspace& workspace) noexcept -> bool
    {
        constexpr std::size_t sample_size{4096U};
        if (literals.size() < sample_size * 10U ||
            (sequence_count != 0U && literals.size() / sequence_count < 20U))
            return false;
        std::uint32_t largest_total{};
        auto& counts{workspace[0]};
        for (auto start : {std::size_t{}, literals.size() - sample_size})
        {
            counts.fill(0U);
            for (auto byte : literals.subspan(start, sample_size))
                ++counts[byte];
            largest_total += *std::max_element(counts.begin(), counts.end());
        }
        return largest_total <= ((2U * sample_size) >> 7U) + 4U;
    }

    // Sequence-code callers may bound the alphabet; only that prefix of the
    // returned workspace is valid when AlphabetSize is smaller than 256.
    template <std::size_t ParallelThreshold = 1500U, std::size_t AlphabetSize = 256U>
    [[nodiscard]] inline auto count_symbols(std::span<std::uint8_t const> input,
        symbol_count_workspace& workspace) noexcept -> symbol_counts const&
    {
        static_assert(AlphabetSize > 0U && AlphabetSize <= 256U);
        if (input.size() < ParallelThreshold)
        {
            if constexpr (AlphabetSize == 256U) workspace[0].fill(0U);
            else std::fill_n(workspace[0].begin(), AlphabetSize, 0U);
            for (auto const symbol : input)
            {
                assert(symbol < AlphabetSize);
                ++workspace[0][symbol];
            }
            return workspace[0];
        }

        for (auto& counts : workspace)
        {
            if constexpr (AlphabetSize == 256U) counts.fill(0U);
            else std::fill_n(counts.begin(), AlphabetSize, 0U);
        }
        auto position{std::size_t{0}};
        for (; position + 16U <= input.size(); position += 16U)
        {
            for (std::size_t stripe{}; stripe < 4U; ++stripe)
            {
                auto const begin{position + stripe * 4U};
                assert(input[begin] < AlphabetSize && input[begin + 1U] < AlphabetSize &&
                    input[begin + 2U] < AlphabetSize && input[begin + 3U] < AlphabetSize);
                ++workspace[0][input[begin]];
                ++workspace[1][input[begin + 1U]];
                ++workspace[2][input[begin + 2U]];
                ++workspace[3][input[begin + 3U]];
            }
        }
        for (; position < input.size(); ++position)
        {
            assert(input[position] < AlphabetSize);
            ++workspace[0][input[position]];
        }
        for (std::size_t symbol{}; symbol < AlphabetSize; ++symbol)
        {
            workspace[0][symbol] += workspace[1][symbol] + workspace[2][symbol] +
                workspace[3][symbol];
        }
        return workspace[0];
    }

    [[nodiscard]] inline auto count_symbols_dominant_prefix(std::span<std::uint8_t const> input,
        symbol_count_workspace& workspace) noexcept -> symbol_counts const&
    {
        // Four independent counters avoid dependencies for long streams;
        // short streams are cheaper without a dominance probe.
        if (input.size() < 1500U || input.size() > 5000U ||
            !std::all_of(input.begin() + 1, input.begin() + 17,
                [first = input.front()](std::uint8_t value) { return value == first; }))
            return count_symbols<128U>(input, workspace);

        auto const dominant{input.front()};
        auto& counts{workspace[0]};
        counts.fill(0U);
        std::uint32_t dominant_count{};
        for (auto const symbol : input)
        {
            if (symbol == dominant)
                ++dominant_count;
            else
                ++counts[symbol];
        }
        counts[dominant] = dominant_count;
        return counts;
    }

    template <unsigned Configured>
    class match_parameter
    {
    public:
        constexpr explicit match_parameter(unsigned runtime) noexcept : runtime_{runtime} {}

        [[nodiscard]] static constexpr auto resolve(unsigned runtime) noexcept -> unsigned
        {
            if constexpr (Configured != 0U)
            {
                return Configured;
            }
            else
            {
                return runtime;
            }
        }

        [[nodiscard]] constexpr operator unsigned() const noexcept
        {
            return resolve(runtime_);
        }

    private:
        unsigned runtime_{};
    };

    [[nodiscard]] inline auto load_native_u32(std::uint8_t const* input) noexcept -> std::uint32_t
    {
        std::uint32_t value{};
        std::memcpy(&value, input, sizeof(value));
        return value;
    }

    [[nodiscard]] inline auto load_native_u64(std::uint8_t const* input) noexcept -> std::uint64_t
    {
        std::uint64_t value{};
        std::memcpy(&value, input, sizeof(value));
        return value;
    }

    [[nodiscard]] inline auto row_hash4(std::uint8_t const* input, unsigned hash_bits,
        std::uint32_t salt) noexcept -> std::uint32_t
    {
        auto value{load_native_u32(input)};
        if constexpr (std::endian::native == std::endian::big)
            value = std::byteswap(value);
        return ((value * 2654435761U) ^ salt) >> (32U - hash_bits);
    }

    [[nodiscard]] inline auto row_hash5(std::uint8_t const* input, unsigned hash_bits,
        std::uint64_t salt) noexcept -> std::uint32_t
    {
        auto value{load_native_u64(input)};
        if constexpr (std::endian::native == std::endian::big)
            value = std::byteswap(value);
        return static_cast<std::uint32_t>((((value << 24U) * 889523592379ULL) ^ salt) >>
            (64U - hash_bits));
    }

    [[nodiscard]] inline auto row_hash6(std::uint8_t const* input, unsigned hash_bits,
        std::uint64_t salt) noexcept -> std::uint32_t
    {
        auto value{load_native_u64(input)};
        if constexpr (std::endian::native == std::endian::big)
            value = std::byteswap(value);
        return static_cast<std::uint32_t>((((value << 16U) * 227718039650203ULL) ^ salt) >>
            (64U - hash_bits));
    }

    [[nodiscard]] inline auto row_hash7(std::uint8_t const* input, unsigned hash_bits,
        std::uint64_t salt) noexcept -> std::uint32_t
    {
        auto value{load_native_u64(input)};
        if constexpr (std::endian::native == std::endian::big)
            value = std::byteswap(value);
        return static_cast<std::uint32_t>((((value << 8U) * 58295818150454627ULL) ^ salt) >>
            (64U - hash_bits));
    }

    [[nodiscard]] inline auto row_hash8(std::uint8_t const* input, unsigned hash_bits,
        std::uint64_t salt) noexcept -> std::uint32_t
    {
        auto value{load_native_u64(input)};
        if constexpr (std::endian::native == std::endian::big)
            value = std::byteswap(value);
        return static_cast<std::uint32_t>(((value * 0xCF1BBCDCB7A56463ULL) ^ salt) >>
            (64U - hash_bits));
    }

    template <unsigned MinimumMatch>
    [[nodiscard]] inline auto row_hash_fixed(std::uint8_t const* input, unsigned hash_bits,
        std::uint64_t salt) noexcept -> std::uint32_t
    {
        if constexpr (MinimumMatch == 4U)
            return row_hash4(input, hash_bits, static_cast<std::uint32_t>(salt));
        else if constexpr (MinimumMatch == 5U)
            return row_hash5(input, hash_bits, salt);
        else if constexpr (MinimumMatch == 6U)
            return row_hash6(input, hash_bits, salt);
        else if constexpr (MinimumMatch == 7U)
            return row_hash7(input, hash_bits, salt);
        else
            return row_hash8(input, hash_bits, salt);
    }

    [[nodiscard]] inline auto row_hash(std::uint8_t const* input, unsigned hash_bits,
        unsigned minimum_match, std::uint64_t salt) noexcept -> std::uint32_t
    {
        switch (minimum_match)
        {
        case 4U: return row_hash4(input, hash_bits, static_cast<std::uint32_t>(salt));
        case 5U: return row_hash5(input, hash_bits, salt);
        case 6U: return row_hash6(input, hash_bits, salt);
        case 7U: return row_hash7(input, hash_bits, salt);
        default: return row_hash8(input, hash_bits, salt);
        }
    }

    /** Preallocated storage for reference zstd's row-based match finder. */
    class row_match_table
    {
    public:
        explicit row_match_table(unsigned hash_log)
            : hash_table_(std::size_t{1} << std::clamp(hash_log, 6U, 30U)),
              tag_table_(hash_table_.size())
        {
        }

        void reset() noexcept
        {
            std::ranges::fill(tag_table_, std::uint8_t{0});
        }

        [[nodiscard]] auto hash_table() noexcept -> std::uint32_t* { return hash_table_.data(); }
        [[nodiscard]] auto tag_table() noexcept -> std::uint8_t* { return tag_table_.data(); }

        [[nodiscard]] auto insert(std::uint32_t hash, unsigned row_log,
            std::uint32_t index) noexcept -> std::uint32_t
        {
            switch (std::clamp(row_log, 4U, 6U))
            {
            case 4U: return insert_fixed<4U>(hash, index);
            case 5U: return insert_fixed<5U>(hash, index);
            default: return insert_fixed<6U>(hash, index);
            }
        }

        template <unsigned RowLog>
        [[nodiscard]] auto insert_fixed(std::uint32_t hash, std::uint32_t index) noexcept -> std::uint32_t
        {
            static_assert(RowLog >= 4U && RowLog <= 6U);
            auto constexpr row_entries{std::uint32_t{1} << RowLog};
            auto const row_mask{row_entries - 1U};
            auto const row{(hash >> 8U) << RowLog};
            auto* const tags{tag_table_.data() + row};
            auto next{(static_cast<std::uint32_t>(*tags) - 1U) & row_mask};
            if (next == 0U)
                next = row_mask;
            *tags = static_cast<std::uint8_t>(next);
            tag_table_[row + next] = static_cast<std::uint8_t>(hash);
            hash_table_[row + next] = index;
            return next;
        }

        [[nodiscard]] auto collect_candidates(std::uint32_t hash, unsigned row_log,
            std::uint32_t low_limit, std::uint32_t attempt_limit,
            std::span<std::uint32_t> candidates) const noexcept -> std::size_t
        {
            switch (std::clamp(row_log, 4U, 6U))
            {
            case 4U: return collect_candidates_fixed<4U>(hash, low_limit, attempt_limit, candidates);
            case 5U: return collect_candidates_fixed<5U>(hash, low_limit, attempt_limit, candidates);
            default: return collect_candidates_fixed<6U>(hash, low_limit, attempt_limit, candidates);
            }
        }

        template <unsigned RowLog>
        [[nodiscard]] auto collect_candidates_fixed(std::uint32_t hash,
            std::uint32_t low_limit, std::uint32_t attempt_limit,
            std::span<std::uint32_t> candidates) const noexcept -> std::size_t
        {
            static_assert(RowLog >= 4U && RowLog <= 6U);
            auto constexpr row_entries{std::uint32_t{1} << RowLog};
            auto const row_mask{row_entries - 1U};
            auto const row{(hash >> 8U) << RowLog};
            auto const tag{static_cast<std::uint8_t>(hash)};
            auto const head{static_cast<std::uint32_t>(tag_table_[row]) & row_mask};
            auto const limit{std::min<std::uint32_t>(attempt_limit, row_entries)};
            auto const matching_tags{matching_tag_mask(tag_table_.data() + row, tag, row_entries)};
            std::uint64_t row_bits{};
            if constexpr (RowLog == 6U)
                row_bits = std::numeric_limits<std::uint64_t>::max();
            else
                row_bits = (std::uint64_t{1} << row_entries) - 1U;
            auto const rotated_matches{head == 0U ? matching_tags :
                ((matching_tags >> head) | (matching_tags << (row_entries - head))) & row_bits};
            std::size_t count{};
            auto matches{rotated_matches};
            while (matches != 0U && count < limit)
            {
                auto const offset{static_cast<std::uint32_t>(std::countr_zero(matches))};
                matches &= matches - 1U;
                auto const slot{(head + offset) & row_mask};
                if (slot == 0U)
                    continue;
                auto const index{hash_table_[row + slot]};
                if (index < low_limit)
                    break;
                if (count < candidates.size())
                    candidates[count++] = index;
            }
            return count;
        }

    private:
        [[nodiscard]] static auto matching_tag_mask(std::uint8_t const* tags,
            std::uint8_t tag, std::uint32_t entries) noexcept -> std::uint64_t
        {
            std::uint64_t result{};
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
            auto const needle{_mm_set1_epi8(static_cast<char>(tag))};
            for (std::uint32_t offset{}; offset < entries; offset += 16U)
            {
                auto const values{_mm_load_si128(reinterpret_cast<__m128i const*>(tags + offset))};
                auto const equal{_mm_cmpeq_epi8(values, needle)};
                result |= static_cast<std::uint64_t>(
                    static_cast<unsigned>(_mm_movemask_epi8(equal))) << offset;
            }
#else
            for (std::uint32_t offset{}; offset < entries; ++offset)
                if (tags[offset] == tag) result |= std::uint64_t{1} << offset;
#endif
            return result;
        }

        match_table hash_table_;
        row_tag_table tag_table_;
    };

    /** Eight-entry rolling hash cache used by the reference row matcher. */
    class row_hash_cache
    {
    public:
        static constexpr std::uint32_t size{8U};
        static constexpr std::uint32_t mask{size - 1U};

        void reset() noexcept { values_.fill(0U); }

        template <typename Hasher>
        // `end` is inclusive, matching the reference iLimit contract.
        void fill(std::uint32_t begin, std::uint32_t end, Hasher&& hasher) noexcept
        {
            if (end < begin)
                return;
            auto const count{std::min<std::uint64_t>(size,
                static_cast<std::uint64_t>(end) - begin + 1U)};
            for (auto index{begin}; index < begin + count; ++index)
                values_[index & mask] = static_cast<std::uint32_t>(hasher(index));
        }

        template <unsigned MinimumMatch>
        void fill_fixed(std::uint32_t begin, std::uint32_t end, std::uint8_t const* base,
            unsigned hash_bits, std::uint64_t salt) noexcept
        {
            if (end < begin)
                return;
            auto const count{std::min<std::uint64_t>(size,
                static_cast<std::uint64_t>(end) - begin + 1U)};
            for (auto index{begin}; index < begin + count; ++index)
                values_[index & mask] = row_hash_fixed<MinimumMatch>(base + index, hash_bits, salt);
        }

        template <typename Hasher>
        [[nodiscard]] auto next(std::uint32_t index, Hasher&& hasher) noexcept -> std::uint32_t
        {
            auto const slot{index & mask};
            auto const current{values_[slot]};
            values_[slot] = static_cast<std::uint32_t>(hasher(index + size));
            return current;
        }

        template <unsigned MinimumMatch>
        [[nodiscard]] auto next_fixed(std::uint32_t index, std::uint8_t const* base,
            unsigned hash_bits, std::uint64_t salt) noexcept -> std::uint32_t
        {
            auto const slot{index & mask};
            auto const current{values_[slot]};
            values_[slot] = row_hash_fixed<MinimumMatch>(base + index + size, hash_bits, salt);
            return current;
        }

        [[nodiscard]] auto operator[](std::uint32_t index) const noexcept -> std::uint32_t
        {
            return values_[index & mask];
        }

    private:
        std::array<std::uint32_t, size> values_{};
    };

    /** Persistent salt/entropy state for a reusable row-hash match state. */
    class row_hash_salt_state
    {
    public:
        void observe(std::uint32_t hash) noexcept { entropy_ += hash; }

        void reset(bool dictionary) noexcept
        {
            salt_ = dictionary ? 0U : advance_row_hash_salt(salt_, entropy_);
        }

        [[nodiscard]] auto salt() const noexcept -> std::uint64_t { return salt_; }
        [[nodiscard]] auto entropy() const noexcept -> std::uint32_t { return entropy_; }

    private:
        std::uint64_t salt_{};
        std::uint32_t entropy_{};
    };

    /** Allocation-free composition of the row table, rolling cache, and salt state. */
    class row_match_state
    {
    public:
        row_match_state(unsigned hash_log, unsigned row_log)
            : table_(hash_log), row_log_(std::clamp(row_log, 4U, 6U))
        {
        }

        void reset(bool dictionary = false) noexcept
        {
            table_.reset();
            cache_.reset();
            salt_.reset(dictionary);
            next_to_update_ = 0U;
        }

        template <typename Hasher>
        void update(std::uint32_t begin, std::uint32_t end, Hasher&& hasher) noexcept
        {
            if (end < begin)
                return;
            cache_.fill(begin, end, hasher);
            for (auto index{begin}; index <= end; ++index)
            {
                auto const hash{cache_.next(index, hasher)};
                static_cast<void>(table_.insert(hash, row_log_, index));
                salt_.observe(hash);
                if (index == std::numeric_limits<std::uint32_t>::max())
                    break;
            }
            next_to_update_ = end + (end != std::numeric_limits<std::uint32_t>::max());
        }

        template <unsigned MinimumMatch, unsigned RowLog>
        // Caller provides readable lookahead through end + cache size + 7.
        // The production row parser enforces this with its blockEnd-16 cutoff.
        void update_fixed(std::uint32_t begin, std::uint32_t end, std::uint8_t const* base,
            unsigned hash_bits) noexcept
        {
            static_assert(RowLog >= 4U && RowLog <= 6U);
            if (end < begin)
                return;
            cache_.fill_fixed<MinimumMatch>(begin, end, base, hash_bits, salt_.salt());
            for (auto index{begin}; index <= end; ++index)
            {
                auto const hash{cache_.next_fixed<MinimumMatch>(index, base, hash_bits, salt_.salt())};
                static_cast<void>(table_.insert_fixed<RowLog>(hash, index));
                salt_.observe(hash);
                if (index == std::numeric_limits<std::uint32_t>::max())
                    break;
            }
            next_to_update_ = end + (end != std::numeric_limits<std::uint32_t>::max());
        }

        [[nodiscard]] auto search(std::uint32_t hash, std::uint32_t low_limit,
            std::uint32_t attempts, std::span<std::uint32_t> candidates) noexcept -> std::size_t
        {
            salt_.observe(hash);
            return table_.collect_candidates(hash, row_log_, low_limit, attempts, candidates);
        }

        template <unsigned RowLog>
        [[nodiscard]] auto search_fixed(std::uint32_t hash, std::uint32_t low_limit,
            std::uint32_t attempts, std::span<std::uint32_t> candidates) noexcept -> std::size_t
        {
            static_assert(RowLog >= 4U && RowLog <= 6U);
            salt_.observe(hash);
            return table_.collect_candidates_fixed<RowLog>(hash, low_limit, attempts, candidates);
        }

        // Reference row matching updates positions strictly before the current
        // input, searches the current hash, and inserts the current position only
        // after candidate collection. Keep that ordering in one reusable seam so
        // the production parser cannot accidentally double-insert the current byte.
        template <unsigned MinimumMatch, unsigned RowLog>
        [[nodiscard]] auto find_fixed(std::uint32_t current, std::uint32_t low_limit,
            std::uint32_t attempts, std::uint8_t const* base, unsigned hash_bits,
            std::span<std::uint32_t> candidates) noexcept -> std::size_t
        {
            static_assert(RowLog >= 4U && RowLog <= 6U);
            if (current < next_to_update_)
                return 0U;
            if (next_to_update_ < current)
                update_fixed<MinimumMatch, RowLog>(next_to_update_, current - 1U,
                    base, hash_bits);
            auto const hash{row_hash_fixed<MinimumMatch>(base + current, hash_bits, salt_.salt())};
            salt_.observe(hash);
            auto const count{table_.collect_candidates_fixed<RowLog>(
                hash, low_limit, attempts, candidates)};
            static_cast<void>(table_.insert_fixed<RowLog>(hash, current));
            next_to_update_ = current + (current != std::numeric_limits<std::uint32_t>::max());
            return count;
        }

        [[nodiscard]] auto salt() const noexcept -> std::uint64_t { return salt_.salt(); }
        [[nodiscard]] auto next_to_update() const noexcept -> std::uint32_t { return next_to_update_; }

    private:
        row_match_table table_;
        row_hash_cache cache_;
        row_hash_salt_state salt_;
        unsigned row_log_{};
        std::uint32_t next_to_update_{};
    };

    [[nodiscard]] inline auto load_native_u32(std::span<std::uint8_t const> input,
        std::size_t position) noexcept -> std::uint32_t
    {
        return load_native_u32(input.data() + position);
    }

    [[nodiscard]] inline auto load_native_u64(std::span<std::uint8_t const> input,
        std::size_t position) noexcept -> std::uint64_t
    {
        return load_native_u64(input.data() + position);
    }

    [[nodiscard]] inline auto load_little_u64(std::span<std::uint8_t const> input,
        std::size_t position) noexcept -> std::uint64_t
    {
        auto const value{load_native_u64(input, position)};
        if constexpr (std::endian::native == std::endian::big)
        {
            return std::byteswap(value);
        }
        return value;
    }

    // Reference zstd checks RLE blocks with unrolled machine-word compares.
    // Keep the scan bounded (no padded over-read) while avoiding a predicate
    // call for every byte on highly repetitive blocks.
    [[nodiscard]] inline auto is_rle(std::span<std::uint8_t const> input) noexcept -> bool
    {
        if (input.size() <= 1U)
            return false;
        auto const value{input.front()};
        auto const pattern{std::uint64_t{0x0101010101010101ULL} * value};
        std::size_t position{1U};
        for (; position + 4U * sizeof(std::uint64_t) <= input.size(); position += 4U * sizeof(std::uint64_t))
        {
            if (load_native_u64(input.data() + position) != pattern ||
                load_native_u64(input.data() + position + sizeof(std::uint64_t)) != pattern ||
                load_native_u64(input.data() + position + 2U * sizeof(std::uint64_t)) != pattern ||
                load_native_u64(input.data() + position + 3U * sizeof(std::uint64_t)) != pattern)
                return false;
        }
        for (; position + sizeof(std::uint64_t) <= input.size(); position += sizeof(std::uint64_t))
            if (load_native_u64(input.data() + position) != pattern)
                return false;
        for (; position < input.size(); ++position)
        {
            if (input[position] != value)
                return false;
        }
        return true;
    }

    [[nodiscard]] inline auto equal_four_bytes(std::span<std::uint8_t const> input,
        std::size_t left, std::size_t right) noexcept -> bool
    {
        return load_native_u32(input.data() + left) == load_native_u32(input.data() + right);
    }

    [[nodiscard]] inline auto equal_four_bytes(std::uint8_t const* left,
        std::uint8_t const* right) noexcept -> bool
    {
        return load_native_u32(left) == load_native_u32(right);
    }

    template <bool SeparateFirstWord = false>
    [[nodiscard]] inline auto count_matching_bytes(std::uint8_t const* position,
        std::uint8_t const* match_position, std::uint8_t const* end) noexcept -> std::size_t
    {
        auto const* const start{position};
        auto const* const word_limit{end - (sizeof(std::uint64_t) - 1U)};
        if constexpr (SeparateFirstWord)
        {
            if (position < word_limit)
            {
                auto difference{load_native_u64(position) ^ load_native_u64(match_position)};
                if (difference != 0U)
                {
                    if constexpr (std::endian::native == std::endian::big)
                    {
                        difference = std::byteswap(difference);
                    }
                    return static_cast<std::size_t>(position - start) +
                        static_cast<std::size_t>(std::countr_zero(difference) >> 3U);
                }
                position += sizeof(std::uint64_t);
                match_position += sizeof(std::uint64_t);
                while (position + 2U * sizeof(std::uint64_t) <= word_limit)
                {
                    difference = load_native_u64(position) ^ load_native_u64(match_position);
                    if (difference != 0U)
                    {
                        if constexpr (std::endian::native == std::endian::big)
                            difference = std::byteswap(difference);
                        return static_cast<std::size_t>(position - start) +
                            static_cast<std::size_t>(std::countr_zero(difference) >> 3U);
                    }
                    position += sizeof(std::uint64_t);
                    match_position += sizeof(std::uint64_t);
                    difference = load_native_u64(position) ^ load_native_u64(match_position);
                    if (difference != 0U)
                    {
                        if constexpr (std::endian::native == std::endian::big)
                            difference = std::byteswap(difference);
                        return static_cast<std::size_t>(position - start) +
                            static_cast<std::size_t>(std::countr_zero(difference) >> 3U);
                    }
                    position += sizeof(std::uint64_t);
                    match_position += sizeof(std::uint64_t);
                }
                while (position < word_limit)
                {
                    difference = load_native_u64(position) ^ load_native_u64(match_position);
                    if (difference == 0U)
                    {
                        position += sizeof(std::uint64_t);
                        match_position += sizeof(std::uint64_t);
                        continue;
                    }
                    if constexpr (std::endian::native == std::endian::big)
                    {
                        difference = std::byteswap(difference);
                    }
                    return static_cast<std::size_t>(position - start) +
                        static_cast<std::size_t>(std::countr_zero(difference) >> 3U);
                }
            }
        }
        else
        {
            if (position < word_limit)
            {
                auto difference{load_native_u64(position) ^ load_native_u64(match_position)};
                if (difference != 0U)
                {
                    if constexpr (std::endian::native == std::endian::big)
                    {
                        difference = std::byteswap(difference);
                    }
                    return static_cast<std::size_t>(position - start) +
                        static_cast<std::size_t>(std::countr_zero(difference) >> 3U);
                }
                position += sizeof(std::uint64_t);
                match_position += sizeof(std::uint64_t);
                while (position + 2U * sizeof(std::uint64_t) <= word_limit)
                {
                    difference = load_native_u64(position) ^ load_native_u64(match_position);
                    if (difference != 0U)
                    {
                        if constexpr (std::endian::native == std::endian::big)
                            difference = std::byteswap(difference);
                        return static_cast<std::size_t>(position - start) +
                            static_cast<std::size_t>(std::countr_zero(difference) >> 3U);
                    }
                    position += sizeof(std::uint64_t);
                    match_position += sizeof(std::uint64_t);
                    difference = load_native_u64(position) ^ load_native_u64(match_position);
                    if (difference != 0U)
                    {
                        if constexpr (std::endian::native == std::endian::big)
                            difference = std::byteswap(difference);
                        return static_cast<std::size_t>(position - start) +
                            static_cast<std::size_t>(std::countr_zero(difference) >> 3U);
                    }
                    position += sizeof(std::uint64_t);
                    match_position += sizeof(std::uint64_t);
                }
                while (position < word_limit)
                {
                    difference = load_native_u64(position) ^ load_native_u64(match_position);
                    if (difference == 0U)
                    {
                        position += sizeof(std::uint64_t);
                        match_position += sizeof(std::uint64_t);
                        continue;
                    }
                    if constexpr (std::endian::native == std::endian::big)
                    {
                        difference = std::byteswap(difference);
                    }
                    return static_cast<std::size_t>(position - start) +
                        static_cast<std::size_t>(std::countr_zero(difference) >> 3U);
                }
            }
        }
        if (position + sizeof(std::uint32_t) <= end &&
            load_native_u32(position) == load_native_u32(match_position))
        {
            position += sizeof(std::uint32_t);
            match_position += sizeof(std::uint32_t);
        }
        if (position + sizeof(std::uint16_t) <= end)
        {
            std::uint16_t left{};
            std::uint16_t right{};
            std::memcpy(&left, position, sizeof(left));
            std::memcpy(&right, match_position, sizeof(right));
            if (left == right)
            {
                position += sizeof(std::uint16_t);
                match_position += sizeof(std::uint16_t);
            }
        }
        if (position < end && *position == *match_position)
        {
            ++position;
        }
        return static_cast<std::size_t>(position - start);
    }

    [[nodiscard]] inline auto count_matching_bytes(std::span<std::uint8_t const> input,
        std::size_t position, std::size_t match_position, std::size_t end,
        std::size_t initial) noexcept -> std::size_t
    {
        return initial + count_matching_bytes(input.data() + position + initial,
            input.data() + match_position + initial, input.data() + end);
    }

    struct match
    {
        std::size_t position{};
        std::size_t length{};
        std::size_t offset{};
    };

    struct parsed_sequence
    {
        constexpr parsed_sequence() = default;

        constexpr parsed_sequence(std::size_t literal_length_value,
            std::size_t match_length_value,
            std::size_t offset_value, std::uint8_t repeat_code_value) noexcept
            : literal_length{static_cast<std::uint32_t>(literal_length_value)},
              match_length{static_cast<std::uint32_t>(match_length_value) & 0x3FFFFFFFU},
              repeat_code{repeat_code_value & 3U}, offset{static_cast<std::uint32_t>(offset_value)}
        {
        }

        std::uint32_t literal_length{};
        std::uint32_t match_length : 30 {};
        std::uint32_t repeat_code : 2 {};
        std::uint32_t offset{};

        constexpr auto operator==(parsed_sequence const&) const -> bool = default;
    };

    static_assert(sizeof(parsed_sequence) == 12U);

    struct parsed_block
    {
        using sequence_type = parsed_sequence;
        std::vector<parsed_sequence> sequences;
        std::size_t input_begin{};
        std::size_t trailing_literal_position{};
        std::size_t trailing_literal_length{};
    };

    // Pre-size ordinary 12-byte records once so the double-fast parser can
    // append by index without repeating vector capacity and lifetime work.
    class prepared_sequence_store
    {
    public:
        prepared_sequence_store() = default;
        prepared_sequence_store(prepared_sequence_store&& other) noexcept
            : records_{std::move(other.records_)}, count_{std::exchange(other.count_, 0U)} {}
        auto operator=(prepared_sequence_store&& other) noexcept -> prepared_sequence_store&
        {
            if (this != &other)
            {
                records_ = std::move(other.records_);
                count_ = std::exchange(other.count_, 0U);
            }
            return *this;
        }
        void reserve(std::size_t count)
        {
            if (records_.size() < count) records_.resize(count);
        }
        void clear() noexcept { count_ = 0U; }
        [[nodiscard]] auto size() const noexcept -> std::size_t { return count_; }
        [[nodiscard]] auto empty() const noexcept -> bool { return count_ == 0U; }
        [[nodiscard]] auto data() noexcept -> parsed_sequence* { return records_.data(); }
        [[nodiscard]] auto data() const noexcept -> parsed_sequence const* { return records_.data(); }
        [[nodiscard]] auto begin() const noexcept -> parsed_sequence const* { return data(); }
        [[nodiscard]] auto end() const noexcept -> parsed_sequence const* { return data() + count_; }
        [[nodiscard]] auto operator[](std::size_t index) const noexcept -> parsed_sequence const&
        {
            assert(index < count_);
            return records_[index];
        }
        void push_back(parsed_sequence value) noexcept
        {
            assert(count_ < records_.size());
            records_[count_++] = value;
        }

    private:
        std::vector<parsed_sequence> records_;
        std::size_t count_{};
    };

    struct prepared_parsed_block
    {
        using sequence_type = parsed_sequence;
        prepared_sequence_store sequences;
        std::size_t input_begin{};
        std::size_t trailing_literal_position{};
        std::size_t trailing_literal_length{};
    };

    // Partitions borrow the parser's sequence storage. Only the partition ending
    // at the parent's end owns its trailing literals, as in ZSTD_deriveSeqStoreChunk.
    struct parsed_block_view
    {
        parsed_block_view() = default;
        parsed_block_view(parsed_block const& block) noexcept
            : sequences{block.sequences}, input_begin{block.input_begin},
              trailing_literal_position{block.trailing_literal_position},
              trailing_literal_length{block.trailing_literal_length} {}
        parsed_block_view(prepared_parsed_block const& block) noexcept
            : sequences{block.sequences.data(), block.sequences.size()},
              input_begin{block.input_begin},
              trailing_literal_position{block.trailing_literal_position},
              trailing_literal_length{block.trailing_literal_length} {}

        [[nodiscard]] auto subblock(std::size_t begin, std::size_t end) const noexcept
            -> parsed_block_view
        {
            assert(begin <= end && end <= sequences.size());
            auto position{input_begin};
            for (auto const& sequence : sequences.first(begin))
                position += static_cast<std::size_t>(sequence.literal_length) + sequence.match_length;
            parsed_block_view result;
            result.sequences = sequences.subspan(begin, end - begin);
            result.input_begin = position;
            for (auto const& sequence : result.sequences)
                position += static_cast<std::size_t>(sequence.literal_length) + sequence.match_length;
            result.trailing_literal_position = position;
            if (end == sequences.size())
            {
                result.trailing_literal_position = trailing_literal_position;
                result.trailing_literal_length = trailing_literal_length;
            }
            return result;
        }

        std::span<parsed_sequence const> sequences;
        std::size_t input_begin{};
        std::size_t trailing_literal_position{};
        std::size_t trailing_literal_length{};
    };

    struct sequence_partitions
    {
        // Reference allows 196 splits, followed by the final sequence count.
        std::array<std::size_t, 197U> ends{};
        std::size_t count{};
    };

    // Estimate must evaluate each range against the same committed entropy state.
    // It must not commit tables while exploring possible partitions.
    template <typename Estimate>
    inline void derive_sequence_partitions(parsed_block_view const& block,
        std::size_t sequence_begin, Estimate& estimate, sequence_partitions& result)
    {
        if (block.sequences.size() < 300U || result.count >= 196U) return;
        auto const midpoint{block.sequences.size() / 2U};
        auto const first{block.subblock(0U, midpoint)};
        auto const second{block.subblock(midpoint, block.sequences.size())};
        auto const original_size{estimate(block)};
        auto const first_size{estimate(first)};
        auto const second_size{estimate(second)};
#ifdef SPH_ZSTDPP_TRACE_OPTIMAL
        std::fprintf(stderr, "T %zu %zu %zu %zu %zu\n", sequence_begin,
            sequence_begin + block.sequences.size(), original_size, first_size, second_size);
#endif
        if (first_size + second_size < original_size)
        {
            derive_sequence_partitions(first, sequence_begin, estimate, result);
            if (result.count >= 196U) return;
            result.ends[result.count++] = sequence_begin + midpoint;
            derive_sequence_partitions(second, sequence_begin + midpoint, estimate, result);
        }
    }

    template <typename Estimate>
    [[nodiscard]] inline auto derive_sequence_partitions(parsed_block_view const& block,
        Estimate&& estimate) -> sequence_partitions
    {
        sequence_partitions result;
        derive_sequence_partitions(block, 0U, estimate, result);
        result.ends[result.count++] = block.sequences.size();
        return result;
    }

    struct binary_tree_match_candidate
    {
        std::uint32_t offset_base{};
        std::uint32_t length{};
    };

    struct empty_match_state
    {
        constexpr void reset() const noexcept {}
    };

#ifdef SPH_ZSTDPP_TRACE_BT
    struct binary_tree_trace_counters
    {
        std::uint64_t unsorted_visits{};
        std::uint64_t main_visits{};
        std::uint64_t match_extensions{};
    };
    inline binary_tree_trace_counters binary_tree_trace{};
#endif

    /**
     * Persistent no-dictionary match state for the reference `fast` parser.
     * Positions are stored with Zstandard's two-index bias so zero remains the
     * empty hash-table sentinel across block boundaries.
     */
    template <unsigned WindowLog = 0U, unsigned HashLog = 0U,
        unsigned MinimumMatch = 0U,
        std::size_t TargetLength = std::numeric_limits<std::size_t>::max()>
    class fast_match_state
    {
        // On GCC 13, the minMatch-6 fast parser benefits from comparing
        // candidates directly: maintaining a separate byte-tag table costs
        // more than the input loads it avoids. Keep other paths unchanged.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
        static constexpr bool use_tags{MinimumMatch != 6U};
#else
        static constexpr bool use_tags{true};
#endif
        // GCC 13 x86-64's minMatch-7 loop benefits from fetching the index and its
        // byte tag in one aligned table entry instead of two address streams.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13 && defined(__x86_64__)
        static constexpr bool packed_entries{MinimumMatch == 7U};
#else
        static constexpr bool packed_entries{false};
#endif
        using index_table = std::conditional_t<packed_entries,
            std::vector<std::uint64_t, cache_aligned_allocator<std::uint64_t>>, match_table>;
    public:
        fast_match_state(unsigned window_log, unsigned hash_log, unsigned minimum_match,
            std::size_t target_length)
            : window_log_{window_log}, hash_log_{hash_log}, minimum_match_{minimum_match},
              target_length_{target_length},
              hash_table_(std::size_t{1} << match_parameter<HashLog>::resolve(hash_log)),
              hash_tags_(use_tags && !packed_entries ? hash_table_.size() : 0U)
        {
        }

        void reset()
        {
            auto const window_size{std::uint64_t{1} <<
                match_parameter<WindowLog>::resolve(window_log_)};
            auto const next_base{static_cast<std::uint64_t>(frame_index_base_) +
                frame_extent_ + index_bias + window_size};
            if (next_base + window_size + index_bias >
                std::numeric_limits<std::uint32_t>::max())
            {
                std::ranges::fill(hash_table_, 0U);
                frame_index_base_ = 0U;
            }
            else
            {
                frame_index_base_ = static_cast<std::uint32_t>(next_base);
            }
            frame_extent_ = 0U;
            repeat_offsets_ = {1U, 4U, 8U};
        }

        template <bool CollectLiterals = false, typename Block = parsed_block>
        [[nodiscard]] auto parse(std::span<std::uint8_t const> input,
            std::size_t block_begin, std::size_t block_size,
            Block result = {}, std::optional<bool> known_rle = std::nullopt,
            std::span<std::uint8_t> literal_storage = {}) -> Block
        {
            using sequence_type = typename Block::sequence_type;
            result.sequences.clear();
            result.input_begin = block_begin;
            result.trailing_literal_position = block_begin;
            result.trailing_literal_length = block_size;
            if (block_size < hash_read_size || block_begin > input.size() ||
                block_size > input.size() - block_begin)
            {
                return result;
            }

            ensure_index_headroom(block_begin + block_size);

            if constexpr (CollectLiterals)
            {
                if (literal_storage.size() < block_size)
                    throw entropy_error{"literal gathering storage is too small"};
            }
            auto* literal_cursor{literal_storage.data()};
            auto const gather_literals{[&](std::size_t start, std::size_t count) {
                if constexpr (CollectLiterals)
                {
                    auto const remaining{literal_storage.size() -
                        static_cast<std::size_t>(literal_cursor - literal_storage.data())};
                    if (count <= 16U && remaining >= 16U && input.size() - start >= 16U)
                        std::memcpy(literal_cursor, input.data() + start, 16U);
                    else if (count != 0U)
                        std::memcpy(literal_cursor, input.data() + start, count);
                    literal_cursor += count;
                }
                else
                {
                    (void)start;
                    (void)count;
                }
            }};

            // With fresh history and the default skip distance, the reference
            // probes repcode at position 3, then catches back one byte: two
            // literals followed by an offset-1 match. Other histories/skip
            // distances must use the parser rather than assume this result.
            if (block_begin == 0U && resolve_target_length(target_length_) == 0U &&
                block_size >= 1024U &&
                (known_rle.has_value() ? *known_rle : is_rle(input.subspan(block_begin, block_size))))
            {
                result.sequences.push_back(sequence_type{2U, block_size - 2U, 1U, 1U});
                gather_literals(block_begin, result.sequences.back().literal_length);
                result.trailing_literal_position = block_begin + block_size;
                result.trailing_literal_length = 0U;
                frame_extent_ = std::max(frame_extent_, static_cast<std::uint32_t>(block_begin + block_size));
                repeat_offsets_ = {1U, 4U, 8U};
                return result;
            }

            auto const block_end{block_begin + block_size};
            frame_extent_ = std::max(frame_extent_, static_cast<std::uint32_t>(block_end));
            constexpr auto configured_window_log{match_parameter<WindowLog>::resolve(0U)};
            auto const window_log{configured_window_log != 0U ? configured_window_log : window_log_};
            auto const prefix_start{block_end > (std::size_t{1} << window_log) ?
                block_end - (std::size_t{1} << window_log) : 0U};
            auto const limit{block_end - hash_read_size};
            auto* const hash_table{hash_table_.data()};
            auto* const hash_tags{hash_tags_.data()};
            auto const read_index{[&](std::size_t slot) {
                return static_cast<std::uint32_t>(hash_table[slot]);
            }};
            auto const read_tag{[&](std::size_t slot) -> std::uint8_t {
                if constexpr (packed_entries)
                    return static_cast<std::uint8_t>(hash_table[slot] >> 32U);
                else
                    return hash_tags[slot];
            }};
            auto const insert_entry{[&](std::size_t slot, std::uint32_t index, std::size_t source) {
                if constexpr (packed_entries)
                    hash_table[slot] = static_cast<std::uint64_t>(index) |
                        (static_cast<std::uint64_t>(input[source]) << 32U);
                else {
                    hash_table[slot] = index;
                    if constexpr (use_tags) hash_tags[slot] = input[source];
                }
            }};
            auto anchor{block_begin};
            auto position{block_begin + static_cast<std::size_t>(block_begin == prefix_start)};
            auto repeat_one{repeat_offsets_[0]};
            auto repeat_two{repeat_offsets_[1]};
            std::size_t saved_one{};
            std::size_t saved_two{};

            auto const maximum_repeat{position - prefix_start};
            if (repeat_two > maximum_repeat) saved_two = repeat_two, repeat_two = 0U;
            if (repeat_one > maximum_repeat) saved_one = repeat_one, repeat_one = 0U;
            auto const target_length{resolve_target_length(target_length_)};

            while (position + 1U < limit)
            {
                auto step{target_length + static_cast<std::size_t>(target_length == 0U) + 1U};
                auto next_step{position + (std::size_t{1} << (search_strength - 1U))};
                auto position_one{position + 1U};
                auto position_two{position + step};
                auto position_three{position_two + 1U};
                if (position_three >= limit)
                {
                    break;
                }

                auto hash_zero{hash(input, position)};
                auto hash_one{hash(input, position_one)};
                auto match_index{read_index(hash_zero)};
                std::size_t current_index{};
                bool found{};
                bool repeat_match{};
                auto const prefix_index{static_cast<std::uint32_t>(frame_index_base_ + prefix_start + index_bias)};
                do
                {
                    current_index = frame_index_base_ + position + index_bias;
                    auto const previous_tag{use_tags ? read_tag(hash_zero) : std::uint8_t{}};
                    insert_entry(hash_zero, static_cast<std::uint32_t>(current_index), position);

                    if (repeat_one != 0U && equal_four(input, position_two, position_two - repeat_one))
                    {
                        position = position_two;
                        insert_entry(hash_one, static_cast<std::uint32_t>(
                            frame_index_base_ + position_one + index_bias), position_one);
                        found = true;
                        repeat_match = true;
                        break;
                    }

                    if ((!use_tags || previous_tag == input[position]) &&
                        matches(input, position, match_index, prefix_index))
                    {
                        insert_entry(hash_one, static_cast<std::uint32_t>(
                            frame_index_base_ + position_one + index_bias), position_one);
                        found = true;
                        break;
                    }

                    match_index = read_index(hash_one);
                    hash_zero = hash_one;
                    hash_one = hash(input, position_two);
                    position = position_one;
                    position_one = position_two;
                    position_two = position_three;
                    current_index = frame_index_base_ + position + index_bias;
                    auto const previous_tag_one{use_tags ? read_tag(hash_zero) : std::uint8_t{}};
                    insert_entry(hash_zero, static_cast<std::uint32_t>(current_index), position);

                    if ((!use_tags || previous_tag_one == input[position]) &&
                        matches(input, position, match_index, prefix_index))
                    {
                        if (step <= 4U)
                        {
                            insert_entry(hash_one, static_cast<std::uint32_t>(
                                frame_index_base_ + position_one + index_bias), position_one);
                        }
                        found = true;
                        break;
                    }

                    match_index = read_index(hash_one);
                    hash_zero = hash_one;
                    hash_one = hash(input, position_two);
                    position = position_one;
                    position_one = position_two;
                    position_two = position + step;
                    position_three = position_one + step;
                    if (position_two >= next_step)
                    {
                        ++step;
                        next_step += std::size_t{1} << (search_strength - 1U);
                    }
                } while (position_three < limit);

                if (!found)
                {
                    break;
                }

                std::size_t match_position{};
                std::size_t match_length{4U};
                std::size_t offset{};
                std::uint8_t repeat_code{};
                if (repeat_match)
                {
                    match_position = position - repeat_one;
                    offset = repeat_one;
                    repeat_code = 1U;
                    if (position > anchor && match_position > prefix_start &&
                        input[position - 1U] == input[match_position - 1U])
                    {
                        --position;
                        --match_position;
                        ++match_length;
                    }
                }
                else
                {
                    match_position = static_cast<std::size_t>(
                        match_index - frame_index_base_ - index_bias);
                    repeat_two = repeat_one;
                    repeat_one = position - match_position;
                    offset = repeat_one;
                    while (position > anchor && match_position > prefix_start &&
                        input[position - 1U] == input[match_position - 1U])
                    {
                        --position;
                        --match_position;
                        ++match_length;
                    }
                }
                match_length = count_matching_bytes(
                    input, position, match_position, block_end, match_length);

                result.sequences.push_back(sequence_type{
                    position - anchor, match_length, offset, repeat_code});
                gather_literals(anchor, position - anchor);
                position += match_length;
                anchor = position;

                if (position <= limit)
                {
                    auto const catchup_position{current_index - frame_index_base_ - index_bias + 2U};
                    auto const catchup_hash{hash(input, catchup_position)};
                    insert_entry(catchup_hash, static_cast<std::uint32_t>(current_index + 2U), catchup_position);
                    auto const trailing_hash{hash(input, position - 2U)};
                    insert_entry(trailing_hash, static_cast<std::uint32_t>(
                        frame_index_base_ + position - 2U + index_bias), position - 2U);

                    if (repeat_two != 0U)
                    {
                        while (position <= limit && equal_four(input, position, position - repeat_two))
                        {
                            auto const repeat_length{count_matching_bytes(
                                input, position, position - repeat_two, block_end, 4U)};
                            std::swap(repeat_one, repeat_two);
                            auto const repeat_hash{hash(input, position)};
                            insert_entry(repeat_hash, static_cast<std::uint32_t>(
                                frame_index_base_ + position + index_bias), position);
                            result.sequences.push_back(sequence_type{
                                0U, repeat_length, repeat_one, 1U});
                            position += repeat_length;
                            anchor = position;
                        }
                    }
                }
            }

            saved_two = saved_one != 0U && repeat_one != 0U ? saved_one : saved_two;
            repeat_offsets_[0] = repeat_one != 0U ? repeat_one : saved_one;
            repeat_offsets_[1] = repeat_two != 0U ? repeat_two : saved_two;
            result.trailing_literal_position = anchor;
            result.trailing_literal_length = block_end - anchor;
            // A sequence-free block bypasses this literal buffer entirely.
            if (!result.sequences.empty()) gather_literals(anchor, block_end - anchor);
            return result;
        }

    private:
        static constexpr std::size_t hash_read_size{8U};
        static constexpr std::size_t index_bias{2U};
        static constexpr unsigned search_strength{8U};

        void ensure_index_headroom(std::size_t block_end)
        {
            constexpr auto maximum{std::numeric_limits<std::uint32_t>::max()};
            if (block_end > maximum - index_bias)
                throw entropy_error{"fast parser frame exceeds the 32-bit index range"};
            if (static_cast<std::uint64_t>(frame_index_base_) + block_end + index_bias <= maximum)
                return;

            // reset() reserves a window, not an arbitrarily large next frame.
            // Rebase before an index can wrap within that frame. Current-frame
            // entries retain their positions; stale epochs become empty slots.
            auto const first_current{frame_index_base_ + static_cast<std::uint32_t>(index_bias)};
            for (auto& entry : hash_table_)
            {
                if constexpr (packed_entries)
                {
                    // The tag is not part of the index epoch; subtracting the
                    // base from the full 64-bit entry would corrupt it.
                    auto const index{static_cast<std::uint32_t>(entry)};
                    auto const rebased{index >= first_current ? index - frame_index_base_ : 0U};
                    entry = (entry & 0xFFFFFFFF00000000ULL) | rebased;
                }
                else
                    entry = entry >= first_current ? entry - frame_index_base_ : 0U;
            }
            frame_index_base_ = 0U;
        }

        [[nodiscard]] static constexpr auto resolve_target_length(
            std::size_t runtime) noexcept -> std::size_t
        {
            if constexpr (TargetLength != std::numeric_limits<std::size_t>::max())
            {
                return TargetLength;
            }
            else
            {
                return runtime;
            }
        }

        [[nodiscard]] auto hash(std::span<std::uint8_t const> input, std::size_t position) const
            -> std::size_t
        {
            constexpr auto configured_minimum_match{match_parameter<MinimumMatch>::resolve(0U)};
            constexpr auto configured_hash_log{match_parameter<HashLog>::resolve(0U)};
            auto const hash_log{configured_hash_log != 0U ? configured_hash_log : hash_log_};
            auto const value{load_little_u64(input, position)};
            if constexpr (configured_minimum_match == 4U)
            {
                return static_cast<std::size_t>((static_cast<std::uint32_t>(value) * 2654435761U) >>
                    (32U - hash_log));
            }
            else if constexpr (configured_minimum_match == 5U)
            {
                return static_cast<std::size_t>(((value << 24U) * 889523592379ULL) >>
                    (64U - hash_log));
            }
            else if constexpr (configured_minimum_match == 6U)
            {
                return static_cast<std::size_t>(((value << 16U) * 227718039650203ULL) >>
                    (64U - hash_log));
            }
            else if constexpr (configured_minimum_match == 7U)
            {
                return static_cast<std::size_t>(((value << 8U) * 58295818150454627ULL) >>
                    (64U - hash_log));
            }
            else
            {
                switch (minimum_match_)
                {
                case 5U:
                    return static_cast<std::size_t>(((value << 24U) * 889523592379ULL) >>
                        (64U - hash_log));
                case 6U:
                    return static_cast<std::size_t>(((value << 16U) * 227718039650203ULL) >>
                        (64U - hash_log));
                case 7U:
                    return static_cast<std::size_t>(((value << 8U) * 58295818150454627ULL) >>
                        (64U - hash_log));
                default:
                    return static_cast<std::size_t>((static_cast<std::uint32_t>(value) * 2654435761U) >>
                        (32U - hash_log));
                }
            }
        }

        [[nodiscard]] static auto equal_four(std::span<std::uint8_t const> input,
            std::size_t left, std::size_t right) -> bool
        {
            return equal_four_bytes(input, left, right);
        }

        [[nodiscard]] auto matches(std::span<std::uint8_t const> input,
            std::size_t position, std::uint32_t biased_match, std::uint32_t lowest) const -> bool
        {
#if defined(__GNUC__) && __GNUC__ == 13 && !defined(__clang__) && defined(__x86_64__)
            if constexpr (MinimumMatch == 6U)
            {
                // Select an in-bounds integer offset before forming any pointer.
                // Stale/empty entries may underflow in unsigned arithmetic only.
                auto const candidate{static_cast<std::uint32_t>(
                    biased_match - frame_index_base_ - index_bias)};
                std::uint32_t selected{};
                asm("cmpl %2, %1; cmovael %3, %0"
                    : "+&r"(selected)
                    : "r"(biased_match), "r"(lowest), "r"(candidate)
                    : "cc");
                return (biased_match >= lowest) & equal_four(input, position, selected);
            }
#endif
            if (biased_match < lowest)
            {
                return false;
            }
            return equal_four(input, position,
                biased_match - frame_index_base_ - index_bias);
        }

        unsigned window_log_{};
        unsigned hash_log_{};
        unsigned minimum_match_{};
        std::size_t target_length_{};
        index_table hash_table_;
        row_tag_table hash_tags_;
        std::uint32_t frame_index_base_{};
        std::uint32_t frame_extent_{};
        std::array<std::size_t, 3> repeat_offsets_{1U, 4U, 8U};
    };

    /** Persistent no-dictionary state for reference zstd's `double_fast` parser. */
    template <unsigned WindowLog = 0U, unsigned LongHashLog = 0U,
        unsigned ShortHashLog = 0U, unsigned MinimumMatch = 0U>
    class double_fast_match_state
    {
        friend struct index_epoch_test_access;
    public:
        double_fast_match_state(unsigned window_log, unsigned long_hash_log,
            unsigned short_hash_log, unsigned minimum_match)
            : window_log_{window_log}, long_hash_log_{long_hash_log}, short_hash_log_{short_hash_log},
              minimum_match_{minimum_match}
        {
            auto const long_count{std::size_t{1} << match_parameter<LongHashLog>::resolve(long_hash_log)};
            auto const short_count{std::size_t{1} << match_parameter<ShortHashLog>::resolve(short_hash_log)};
            initialize_tables(long_count, short_count);
        }

        void reset()
        {
            auto const window_size{std::uint64_t{1} <<
                match_parameter<WindowLog>::resolve(window_log_)};
            auto const next_base{static_cast<std::uint64_t>(frame_index_base_) +
                frame_extent_ + index_bias + window_size};
            if (next_base + window_size + index_bias >
                std::numeric_limits<std::uint32_t>::max())
            {
                clear_tables();
                frame_index_base_ = 0U;
            }
            else
            {
                frame_index_base_ = static_cast<std::uint32_t>(next_base);
            }
            frame_extent_ = 0U;
            repeat_offsets_ = {1U, 4U, 8U};
        }

        template <typename Block = parsed_block>
        [[nodiscard]] auto parse(std::span<std::uint8_t const> input,
            std::size_t block_begin, std::size_t block_size,
            Block result = {}, std::optional<bool> known_rle = std::nullopt) -> Block
        {
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
            if constexpr (MinimumMatch == 5U)
            {
                // Measured direct-candidate specialization avoids tag traffic
                // and classifier work. Keeping one policy for the whole frame
                // also prevents stale tags after untagged hash-table updates.
                return parse_impl<false>(input, block_begin, block_size,
                    std::move(result), known_rle);
            }
#endif
#if defined(_MSC_VER)
            if constexpr (packed_level3)
                return parse_impl_masked<true>(input, block_begin, block_size,
                    std::move(result), known_rle);
            else
#endif
                return parse_impl<true>(input, block_begin, block_size, std::move(result), known_rle);
        }

#if defined(_MSC_VER)
        // Keep the prepared level-3 call on its proven eager tag-guard path.
        // Other presets retain their ordinary parser implementation.
        [[nodiscard]] auto parse(std::span<std::uint8_t const> input,
            std::size_t block_begin, std::size_t block_size,
            prepared_parsed_block result, std::optional<bool> known_rle = std::nullopt)
            -> prepared_parsed_block
        {
            if constexpr (WindowLog == 20U && LongHashLog == 17U &&
                ShortHashLog == 16U && MinimumMatch == 5U)
                return parse_impl_masked<true>(input, block_begin, block_size,
                    std::move(result), known_rle);
            else
                return parse_impl<true>(input, block_begin, block_size,
                    std::move(result), known_rle);
        }
#endif

        [[nodiscard]] auto parse_collecting(std::span<std::uint8_t const> input,
            std::size_t block_begin, std::size_t block_size, parsed_block result,
            std::optional<bool> known_rle, std::span<std::uint8_t> literal_storage) -> parsed_block
        {
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
            if constexpr (MinimumMatch == 5U)
                return parse_impl_collecting<false>(input, block_begin, block_size,
                    std::move(result), known_rle, literal_storage);
#endif
#if defined(_MSC_VER)
            if constexpr (packed_level3)
                return parse_impl_packed_collecting<true>(input, block_begin, block_size,
                    std::move(result), known_rle, literal_storage);
            else
#endif
                return parse_impl_collecting<true>(input, block_begin, block_size,
                    std::move(result), known_rle, literal_storage);
        }

    private:
#if defined(_MSC_VER)
        static constexpr bool packed_level3{WindowLog == 20U && LongHashLog == 17U &&
            ShortHashLog == 16U && MinimumMatch == 5U};
#else
        static constexpr bool packed_level3{false};
#endif

        template <bool Packed = packed_level3>
        void initialize_tables(std::size_t long_count, std::size_t short_count)
        {
#if defined(_MSC_VER)
            if constexpr (Packed)
            {
                packed_long_table_.resize(long_count);
                packed_short_table_.resize(short_count);
            }
            else
#endif
            {
                long_table_.resize(long_count);
                short_table_.resize(short_count);
                long_tags_.resize(long_count);
                short_tags_.resize(short_count);
            }
        }

        template <bool Packed = packed_level3>
        void clear_tables()
        {
#if defined(_MSC_VER)
            if constexpr (Packed)
            {
                std::ranges::fill(packed_long_table_, packed_match_entry{});
                std::ranges::fill(packed_short_table_, packed_match_entry{});
            }
            else
#endif
            {
                std::ranges::fill(long_table_, 0U);
                std::ranges::fill(short_table_, 0U);
            }
        }

        template <bool Packed = packed_level3>
        void rebase_tables(std::uint32_t first_current)
        {
#if defined(_MSC_VER)
            if constexpr (Packed)
            {
                for (auto* table : {&packed_long_table_, &packed_short_table_})
                    for (auto& entry : *table)
                    {
                        std::uint32_t index{};
                        std::memcpy(&index, entry.bytes.data(), sizeof(index));
                        index = index >= first_current ? index - frame_index_base_ : 0U;
                        std::memcpy(entry.bytes.data(), &index, sizeof(index));
                    }
            }
            else
#endif
            {
                for (auto* table : {&long_table_, &short_table_})
                    for (auto& index : *table)
                        index = index >= first_current ? index - frame_index_base_ : 0U;
            }
        }

#define SPH_ZSTDPP_DOUBLE_FAST_PARSE_NAME parse_impl
#define SPH_ZSTDPP_DOUBLE_FAST_COLLECT 0
#include "double_fast_parse.inl"
#undef SPH_ZSTDPP_DOUBLE_FAST_COLLECT
#undef SPH_ZSTDPP_DOUBLE_FAST_PARSE_NAME

#if defined(_MSC_VER)
#define SPH_ZSTDPP_DOUBLE_FAST_PARSE_NAME parse_impl_masked
#define SPH_ZSTDPP_DOUBLE_FAST_COLLECT 0
#define SPH_ZSTDPP_DOUBLE_FAST_PACKED_TABLE 1
#define SPH_ZSTDPP_DOUBLE_FAST_MASKED_TAG_GUARD 1
#include "double_fast_parse.inl"
#undef SPH_ZSTDPP_DOUBLE_FAST_MASKED_TAG_GUARD
#undef SPH_ZSTDPP_DOUBLE_FAST_PACKED_TABLE
#undef SPH_ZSTDPP_DOUBLE_FAST_COLLECT
#undef SPH_ZSTDPP_DOUBLE_FAST_PARSE_NAME

#define SPH_ZSTDPP_DOUBLE_FAST_PARSE_NAME parse_impl_packed_collecting
#define SPH_ZSTDPP_DOUBLE_FAST_COLLECT 1
#define SPH_ZSTDPP_DOUBLE_FAST_PACKED_TABLE 1
#include "double_fast_parse.inl"
#undef SPH_ZSTDPP_DOUBLE_FAST_PACKED_TABLE
#undef SPH_ZSTDPP_DOUBLE_FAST_COLLECT
#undef SPH_ZSTDPP_DOUBLE_FAST_PARSE_NAME
#endif

#define SPH_ZSTDPP_DOUBLE_FAST_PARSE_NAME parse_impl_collecting
#define SPH_ZSTDPP_DOUBLE_FAST_COLLECT 1
#include "double_fast_parse.inl"
#undef SPH_ZSTDPP_DOUBLE_FAST_COLLECT
#undef SPH_ZSTDPP_DOUBLE_FAST_PARSE_NAME

        static constexpr std::size_t hash_read_size{8U};
        static constexpr std::size_t index_bias{2U};
        static constexpr std::size_t search_step{256U};

        void ensure_index_headroom(std::size_t block_end)
        {
            constexpr auto maximum{std::numeric_limits<std::uint32_t>::max()};
            if (block_end > maximum - index_bias)
                throw entropy_error{"double-fast frame exceeds the 32-bit index range"};
            if (static_cast<std::uint64_t>(frame_index_base_) + block_end + index_bias <= maximum)
                return;
            auto const first_current{frame_index_base_ + static_cast<std::uint32_t>(index_bias)};
            rebase_tables(first_current);
            frame_index_base_ = 0U;
        }

        [[nodiscard]] static auto read_word(std::uint8_t const* input) -> std::uint64_t
        {
            auto const value{load_native_u64(input)};
            if constexpr (std::endian::native == std::endian::big)
                return std::byteswap(value);
            return value;
        }

        [[nodiscard]] auto long_hash(std::uint8_t const* input) const -> std::size_t
        {
            return static_cast<std::size_t>((read_word(input) * 0xCF1BBCDCB7A56463ULL) >>
                (64U - match_parameter<LongHashLog>::resolve(long_hash_log_)));
        }

        [[nodiscard]] auto short_hash(std::uint8_t const* input) const -> std::size_t
        {
            constexpr auto configured_minimum_match{match_parameter<MinimumMatch>::resolve(0U)};
            constexpr auto configured_hash_log{match_parameter<ShortHashLog>::resolve(0U)};
            auto const hash_log{configured_hash_log != 0U ? configured_hash_log : short_hash_log_};
            if constexpr (configured_minimum_match == 4U)
            {
                return static_cast<std::size_t>((load_native_u32(input) * 2654435761U) >>
                    (32U - hash_log));
            }
            else if constexpr (configured_minimum_match == 5U)
            {
                auto const value{read_word(input)};
                return static_cast<std::size_t>(((value << 24U) * 889523592379ULL) >>
                    (64U - hash_log));
            }
            else if constexpr (configured_minimum_match == 6U)
            {
                auto const value{read_word(input)};
                return static_cast<std::size_t>(((value << 16U) * 227718039650203ULL) >>
                    (64U - hash_log));
            }
            else if constexpr (configured_minimum_match == 7U)
            {
                auto const value{read_word(input)};
                return static_cast<std::size_t>(((value << 8U) * 58295818150454627ULL) >>
                    (64U - hash_log));
            }
            else
            {
                switch (minimum_match_)
                {
                case 5U:
                {
                    auto const value{read_word(input)};
                    return static_cast<std::size_t>(((value << 24U) * 889523592379ULL) >>
                        (64U - hash_log));
                }
                case 6U:
                {
                    auto const value{read_word(input)};
                    return static_cast<std::size_t>(((value << 16U) * 227718039650203ULL) >>
                        (64U - hash_log));
                }
                case 7U:
                {
                    auto const value{read_word(input)};
                    return static_cast<std::size_t>(((value << 8U) * 58295818150454627ULL) >>
                        (64U - hash_log));
                }
                default:
                    return static_cast<std::size_t>((load_native_u32(input) * 2654435761U) >>
                        (32U - hash_log));
                }
            }
        }

        [[nodiscard]] constexpr auto unbiased(std::uint32_t position) const -> std::size_t
        {
            return position - frame_index_base_ - index_bias;
        }

        [[nodiscard]] static constexpr auto valid(std::uint32_t position,
            std::size_t lowest_biased) -> bool
        {
            return position >= lowest_biased;
        }

        [[nodiscard]] static auto equal_four(std::uint8_t const* left,
            std::uint8_t const* right) -> bool
        {
            return load_native_u32(left) == load_native_u32(right);
        }

        [[nodiscard]] static auto equal_eight(std::uint8_t const* left,
            std::uint8_t const* right) -> bool
        {
            return read_word(left) == read_word(right);
        }

        [[nodiscard]] static auto count_match(std::uint8_t const* input,
            std::size_t position, std::size_t match_position, std::size_t end,
            std::size_t initial) -> std::size_t
        {
            return initial + count_matching_bytes(input + position + initial,
                input + match_position + initial, input + end);
        }

        static void catch_up(std::span<std::uint8_t const> input,
            std::size_t& position, std::size_t& match_position, std::size_t anchor,
            std::size_t prefix_start, std::size_t& length)
        {
            while (position > anchor && match_position > prefix_start &&
                input[position - 1U] == input[match_position - 1U])
            {
                --position;
                --match_position;
                ++length;
            }
        }

        unsigned window_log_{};
        unsigned long_hash_log_{};
        unsigned short_hash_log_{};
        unsigned minimum_match_{};
        using index_storage = std::conditional_t<packed_level3, empty_match_table, match_table>;
        using tag_storage = std::conditional_t<packed_level3, empty_match_table, row_tag_table>;
        using packed_storage = std::conditional_t<packed_level3, packed_match_table, empty_match_table>;
        [[no_unique_address]] index_storage long_table_;
        [[no_unique_address]] index_storage short_table_;
        [[no_unique_address]] tag_storage long_tags_;
        [[no_unique_address]] tag_storage short_tags_;
        std::uint32_t frame_index_base_{};
        std::uint32_t frame_extent_{};
        std::array<std::size_t, 3> repeat_offsets_{1U, 4U, 8U};
#if defined(_MSC_VER)
        [[no_unique_address]] packed_storage packed_long_table_;
        [[no_unique_address]] packed_storage packed_short_table_;
#endif
    };

    template <bool Enabled>
    struct row_parser_workspace
    {
        explicit row_parser_workspace(unsigned) {}
    };

    template <>
    struct row_parser_workspace<true>
    {
        explicit row_parser_workspace(unsigned hash_log)
            : table(hash_log), table_size(std::size_t{1} << hash_log)
        {
            salt.reset(false);
        }

        void reset() noexcept
        {
            auto const next_base{static_cast<std::uint64_t>(frame_base) + next + 1U};
            if (next_base > std::numeric_limits<std::uint32_t>::max() / 2U)
            {
                table.reset();
                std::fill_n(table.hash_table(), table_size, 0U);
                frame_base = 0U;
            }
            else frame_base = static_cast<std::uint32_t>(next_base);
            next = 0U;
            cache.reset();
            salt.reset(false);
        }

        static void prefetch(void const* address) noexcept
        {
#if defined(__GNUC__) || defined(__clang__)
            __builtin_prefetch(address, 0, 3);
#elif defined(_M_X64) || defined(_M_IX86)
            _mm_prefetch(static_cast<char const*>(address), _MM_HINT_T0);
#else
            (void)address;
#endif
        }

        template <unsigned MinimumMatch, unsigned RowLog>
        auto hash(std::span<std::uint8_t const> input, std::uint32_t index,
            unsigned hash_bits) noexcept -> std::uint32_t
        {
            // Row searches stop at blockEnd-16. Cache replacements hash at
            // most current+8, leaving eight readable bytes even for exact-size
            // input spans. Block-start fills use the same bounded limit.
            assert(static_cast<std::size_t>(index) + 8U <= input.size());
            auto const value{row_hash_fixed<MinimumMatch>(input.data() + index, hash_bits, salt.salt())};
            auto const row{(value >> 8U) << RowLog};
            prefetch(table.hash_table() + row);
            prefetch(table.tag_table() + row);
            if constexpr (RowLog >= 5U) prefetch(table.hash_table() + row + 16U);
            if constexpr (RowLog == 6U) prefetch(table.tag_table() + row + 32U);
            return value;
        }

        template <unsigned MinimumMatch, unsigned RowLog>
        void fill(std::span<std::uint8_t const> input, std::uint32_t limit,
            unsigned hash_bits) noexcept
        {
            cache.fill(next, limit, [&](std::uint32_t index) {
                return hash<MinimumMatch, RowLog>(input, index, hash_bits);
            });
        }

        template <unsigned MinimumMatch, unsigned RowLog, bool BatchLocal = false>
        auto find(std::span<std::uint8_t const> input, std::uint32_t current,
            std::uint32_t low_position, unsigned hash_bits, unsigned attempts,
            bool skipping, std::span<std::uint32_t> candidates) noexcept -> std::size_t
        {
            auto const hasher = [&](std::uint32_t index) {
                return hash<MinimumMatch, RowLog>(input, index, hash_bits);
            };
            auto insert_until = [&](std::uint32_t end) {
                // Greedy and depth-1 lazy parsing benefit from batch-local state
                // and branch-free wrap. Deeper lazy parsing keeps its measured path.
                if constexpr (!BatchLocal || RowLog != 4U)
                {
                    while (next < end)
                    {
                        auto const value{cache.next(next, hasher)};
                        (void)table.insert_fixed<RowLog>(value, frame_base + next + 1U);
                        ++next;
                    }
                }
                else
                {
                    auto* const positions{table.hash_table()};
                    auto* const tags{table.tag_table()};
                    auto const hash_salt{salt.salt()};
                    auto const origin{frame_base + 1U};
                    auto cursor{next};
                    auto const batch_hasher = [&](std::uint32_t index) {
                        assert(static_cast<std::size_t>(index) + 8U <= input.size());
                        auto const value{row_hash_fixed<MinimumMatch>(input.data() + index, hash_bits, hash_salt)};
                        auto const row{(value >> 8U) << RowLog};
                        prefetch(positions + row);
                        prefetch(tags + row);
                        return value;
                    };
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
                    // Unrolling benefits the five-byte greedy hash path; keep the
                    // four-byte path unchanged (small-input controls regress).
                    if constexpr (MinimumMatch == 5U)
                    {
#pragma GCC unroll 2
                        while (cursor < end)
                        {
                            auto const value{cache.next(cursor, batch_hasher)};
                            auto const row{(value >> 8U) << RowLog};
                            auto constexpr mask{(1U << RowLog) - 1U};
                            auto slot{(static_cast<std::uint32_t>(tags[row]) - 1U) & mask};
                            slot += slot == 0U ? mask : 0U;
                            tags[row] = static_cast<std::uint8_t>(slot);
                            tags[row + slot] = static_cast<std::uint8_t>(value);
                            positions[row + slot] = origin + cursor;
                            ++cursor;
                        }
                    }
                    else
#endif
                    {
                        while (cursor < end)
                        {
                            auto const value{cache.next(cursor, batch_hasher)};
                            auto const row{(value >> 8U) << RowLog};
                            auto constexpr mask{(1U << RowLog) - 1U};
                            auto slot{(static_cast<std::uint32_t>(tags[row]) - 1U) & mask};
                            slot += slot == 0U ? mask : 0U;
                            tags[row] = static_cast<std::uint8_t>(slot);
                            tags[row + slot] = static_cast<std::uint8_t>(value);
                            positions[row + slot] = origin + cursor;
                            ++cursor;
                        }
                    }
                    next = cursor;
                }
            };
            std::uint32_t value{};
            if (skipping)
            {
                value = hasher(current);
                next = current;
            }
            else
            {
                if (current - next > 384U)
                {
                    insert_until(next + 96U);
                    next = current - 32U;
                    cache.fill(next, current + 1U, hasher);
                }
                insert_until(current);
                value = cache.next(current, hasher);
            }
            salt.observe(value); // Only searches contribute reference salt entropy.
            auto const count{table.collect_candidates_fixed<RowLog>(value,
                frame_base + low_position + 1U, attempts, candidates)};
            for (std::size_t index{}; index < count; ++index)
            {
                candidates[index] -= frame_base + 1U;
                prefetch(input.data() + candidates[index]);
            }
            (void)table.insert_fixed<RowLog>(value, frame_base + current + 1U);
            next = current + 1U;
            return count;
        }

        row_match_table table;
        row_hash_cache cache;
        row_hash_salt_state salt;
        std::size_t table_size{};
        std::uint32_t frame_base{};
        std::uint32_t next{};
    };

    template <bool Enabled>
    struct adjacent_insertion_cache {};

    template <>
    struct adjacent_insertion_cache<true>
    {
        struct entry
        {
            std::uint32_t current{};
            std::uint32_t match{};
            std::uint32_t length{};
        };
        std::array<std::array<entry, 64>, 2> rows{};
    };

    /** Persistent match state for reference zstd's greedy and lazy parsers. */
    template <unsigned LazyDepth = 0U, bool BinaryTree = false,
        unsigned WindowLog = 0U, unsigned HashLog = 0U, unsigned ChainLog = 0U,
        unsigned SearchLog = 0U, unsigned MinimumMatch = 0U, bool RowMatch = false,
        bool CacheAdjacentInsertion = false>
    class greedy_match_state
    {
        friend struct index_epoch_test_access;
    public:
        greedy_match_state(unsigned window_log, unsigned hash_log, unsigned chain_log,
            unsigned search_log, unsigned minimum_match, unsigned /*target_length*/ = 0U)
            : window_log_{window_log}, hash_log_{hash_log}, chain_log_{chain_log},
              search_log_{search_log}, minimum_match_{std::clamp(minimum_match, 3U, 6U)},
              hash_table_(RowMatch ? 0U : std::size_t{1} << match_parameter<HashLog>::resolve(hash_log)),
              hash3_table_(BinaryTree && match_parameter<MinimumMatch>::resolve(minimum_match) == 3U ?
                  std::size_t{1} << std::min(17U, match_parameter<WindowLog>::resolve(window_log)) : 0U),
              chain_table_(RowMatch ? 0U : std::size_t{1} << match_parameter<ChainLog>::resolve(chain_log)),
              chain_tags_(BinaryTree || RowMatch ? 0U : chain_table_.size()),
              chain_mask_(static_cast<std::uint32_t>(chain_table_.size() - 1U)),
              tree_mask_(chain_mask_ >> 1U), row_state_(hash_log)
        {
        }

        void reset()
        {
            if constexpr (RowMatch) row_state_.reset();
            auto const window_size{std::uint64_t{1} << parameter_value<WindowLog>(window_log_)};
            auto const next_base{static_cast<std::uint64_t>(next_to_update_) + window_size};
            if (next_base + window_size + index_bias >
                std::numeric_limits<std::uint32_t>::max())
            {
                std::ranges::fill(hash_table_, 0U);
                std::ranges::fill(chain_table_, 0U);
                std::ranges::fill(hash3_table_, 0U);
                frame_index_base_ = 0U;
            }
            else
            {
                frame_index_base_ = static_cast<std::uint32_t>(next_base);
            }
            repeat_offsets_ = {1U, 4U, 8U};
            next_to_update_ = frame_index_base_ + index_bias;
            next_to_update3_ = next_to_update_;
        }

        [[nodiscard]] auto optimal_minimum_match() const noexcept -> unsigned
        {
            return parameter_value<MinimumMatch>(minimum_match_) == 3U ? 3U : 4U;
        }

        void begin_optimal_block(std::size_t block_begin = 0U, std::size_t block_end = 0U)
        {
            ensure_index_headroom(std::max(block_begin, block_end));
            // Exact lengths are reusable only under an unchanged block end.
            if constexpr (CacheAdjacentInsertion) comparison_cache_.rows = {};
            // ZSTD_buildSeqStore bounds catch-up before every strategy. The
            // optimal parser bypasses parse(), so it needs the same adjustment.
            auto const block_index{frame_index_base_ +
                static_cast<std::uint32_t>(block_begin) + index_bias};
            if (block_index > next_to_update_ + block_update_skip_threshold)
                next_to_update_ = block_index - std::min(block_update_tail,
                    block_index - next_to_update_ - block_update_skip_threshold);
            next_to_update3_ = next_to_update_;
        }

        [[nodiscard]] auto parse(std::span<std::uint8_t const> input,
            std::size_t block_begin, std::size_t block_size,
            parsed_block result = {}, std::optional<bool> known_rle = std::nullopt) -> parsed_block
        {
            result.sequences.clear();
            result.input_begin = block_begin;
            result.trailing_literal_position = block_begin;
            result.trailing_literal_length = block_size;
            if (block_size < hash_read_size || block_begin > input.size() ||
                block_size > input.size() - block_begin)
            {
                return result;
            }

            ensure_index_headroom(block_begin + block_size);

            if constexpr (RowMatch)
                if (block_size < hash_read_size + row_hash_cache::size) return result;
            if constexpr (LazyDepth == 1U && !RowMatch)
            {
                if (block_size >= 1024U &&
                    (known_rle.has_value() ? *known_rle : is_rle(input.subspan(block_begin, block_size))))
                {
                    result.sequences.push_back(parsed_sequence{1U, block_size - 1U, 1U, 0U});
                    result.trailing_literal_position = block_begin + block_size;
                    result.trailing_literal_length = 0U;
                    repeat_offsets_ = {1U, 4U, 8U};
                    return result;
                }
            }
            if constexpr (LazyDepth == 2U && !BinaryTree && !RowMatch)
            {
                if (block_size >= 1024U &&
                    (known_rle.has_value() ? *known_rle : is_rle(input.subspan(block_begin, block_size))))
                {
                    result.sequences.push_back(parsed_sequence{1U, block_size - 1U, 1U, 0U});
                    result.trailing_literal_position = block_begin + block_size;
                    result.trailing_literal_length = 0U;
                    repeat_offsets_ = {1U, 4U, 8U};
                    return result;
                }
            }
            if constexpr (LazyDepth == 2U && BinaryTree)
            {
                if (block_size >= 1024U && is_rle(input.subspan(block_begin, block_size)))
                {
                    if (block_begin == 0U)
                        result.sequences.push_back(parsed_sequence{2U, block_size - 2U, 1U, 1U});
                    else
                        result.sequences.push_back(parsed_sequence{0U, block_size, 1U, 0U});
                    result.trailing_literal_position = block_begin + block_size;
                    result.trailing_literal_length = 0U;
                    repeat_offsets_ = {1U, 4U, 8U};
                    return result;
                }
            }
            if constexpr (LazyDepth == 0U && !BinaryTree && !RowMatch)
            {
                if (block_size >= 1024U && is_rle(input.subspan(block_begin, block_size)))
                {
                    result.sequences.push_back(parsed_sequence{2U, block_size - 2U, 1U, 1U});
                    result.trailing_literal_position = block_begin + block_size;
                    result.trailing_literal_length = 0U;
                    repeat_offsets_ = {1U, 4U, 8U};
                    return result;
                }
            }
            auto const block_end{block_begin + block_size};
            if constexpr (RowMatch)
            {
                auto const begin{static_cast<std::uint32_t>(block_begin)};
                if (begin > row_state_.next + 384U)
                    row_state_.next = begin - std::min(192U, begin - row_state_.next - 384U);
            }
            if constexpr (BinaryTree)
            {
                // Match reference zstd's bounded table catch-up at each block boundary.
                // A long match near the end of the previous block can leave a large gap;
                // retaining only a small prefix and suffix of that gap preserves useful
                // candidates without rebuilding the entire skipped range.
                auto const block_index{frame_index_base_ +
                    static_cast<std::uint32_t>(block_begin) + index_bias};
                if (block_index > next_to_update_ + block_update_skip_threshold)
                {
                    next_to_update_ = block_index - std::min(block_update_tail,
                        block_index - next_to_update_ - block_update_skip_threshold);
                }
            }
            auto const limit{block_end - hash_read_size - (RowMatch ? row_hash_cache::size : 0U)};
            if constexpr (RowMatch)
                row_state_.template fill<row_mls, row_log>(input,
                    static_cast<std::uint32_t>(limit), HashLog - row_log + 8U);
            auto anchor{block_begin};
            auto position{block_begin + static_cast<std::size_t>(block_begin == 0U)};
            auto repeat_one{repeat_offsets_[0]};
            auto repeat_two{repeat_offsets_[1]};
            bool lazy_skipping{};
            std::size_t saved_one{};
            std::size_t saved_two{};
            auto const window_size{std::size_t{1} << parameter_value<WindowLog>(window_log_)};
            auto const window_start_at_position{position > window_size ? position - window_size : 0U};
            auto const maximum_repeat{position - window_start_at_position};
            if (repeat_two > maximum_repeat) saved_two = repeat_two, repeat_two = 0U;
            if (repeat_one > maximum_repeat) saved_one = repeat_one, repeat_one = 0U;

            while (position < limit)
            {
                std::size_t match_length{};
                std::size_t offset{};
                std::uint8_t repeat_code{};
                auto match_start{position + 1U};
                std::size_t encoded_offset_base{1U};
                auto encoded_offset_bits{high_bit(encoded_offset_base)};
                bool baseline_repeat{};

                if (repeat_one != 0U && equal_four(input, position + 1U, position + 1U - repeat_one))
                {
                    match_length = count_match(
                        input, position + 1U, position + 1U - repeat_one, block_end, 4U);
                    offset = repeat_one;
                    repeat_code = 1U;
                    baseline_repeat = true;
                }
                if (!baseline_repeat || LazyDepth != 0U)
                {
                    auto const found{find_best_match(input, position, block_end, lazy_skipping)};
                    if (found.length > match_length)
                    {
                        match_length = found.length;
                        offset = found.offset;
                        match_start = position;
                        encoded_offset_base = offset + 3U;
                        encoded_offset_bits = high_bit(encoded_offset_base);
                        repeat_code = 0U;
                    }
                }

                if (match_length < 4U)
                {
                    auto const step{((position - anchor) >> search_strength) + 1U};
                    position += step;
                    lazy_skipping = step > lazy_skipping_step;
                    continue;
                }

                if constexpr (LazyDepth >= 1U)
                {
                    while (position < limit)
                    {
                        ++position;
                        if (repeat_one != 0U && equal_four(input, position, position - repeat_one))
                        {
                            auto const repeat_length{count_match(
                                input, position, position - repeat_one, block_end, 4U)};
                            auto const repeat_gain{static_cast<std::int64_t>(repeat_length * 3U)};
                            auto const current_gain{static_cast<std::int64_t>(match_length * 3U) -
                                static_cast<std::int64_t>(encoded_offset_bits) + 1};
                            if (repeat_gain > current_gain)
                            {
                                match_length = repeat_length;
                                offset = repeat_one;
                                encoded_offset_base = 1U;
                                encoded_offset_bits = 0U;
                                repeat_code = 1U;
                                match_start = position;
                            }
                        }
                        auto candidate{find_best_match(input, position, block_end, lazy_skipping)};
                        auto candidate_base{candidate.offset + 3U};
                        auto const candidate_bits{high_bit(candidate_base)};
                        auto const candidate_gain{static_cast<std::int64_t>(candidate.length * 4U) -
                            static_cast<std::int64_t>(candidate_bits)};
                        auto const current_gain{static_cast<std::int64_t>(match_length * 4U) -
                            static_cast<std::int64_t>(encoded_offset_bits) + 4};
                        if (candidate.length >= 4U && candidate_gain > current_gain)
                        {
                            match_length = candidate.length;
                            offset = candidate.offset;
                            encoded_offset_base = candidate_base;
                            encoded_offset_bits = candidate_bits;
                            repeat_code = 0U;
                            match_start = position;
                            continue;
                        }

                        if constexpr (LazyDepth == 2U)
                        {
                            if (position < limit)
                            {
                                ++position;
                                if (repeat_one != 0U && equal_four(input, position, position - repeat_one))
                                {
                                    auto const repeat_length{count_match(
                                        input, position, position - repeat_one, block_end, 4U)};
                                    auto const repeat_gain{static_cast<std::int64_t>(repeat_length * 4U)};
                                    auto const current_repeat_gain{static_cast<std::int64_t>(match_length * 4U) -
                                        static_cast<std::int64_t>(encoded_offset_bits) + 1};
                                    if (repeat_gain > current_repeat_gain)
                                    {
                                        match_length = repeat_length;
                                        offset = repeat_one;
                                        encoded_offset_base = 1U;
                                        encoded_offset_bits = 0U;
                                        repeat_code = 1U;
                                        match_start = position;
                                    }
                                }
                                candidate = find_best_match(input, position, block_end, lazy_skipping);
                                candidate_base = candidate.offset + 3U;
                                auto const second_candidate_gain{static_cast<std::int64_t>(candidate.length * 4U) -
                                    static_cast<std::int64_t>(high_bit(candidate_base))};
                                auto const second_current_gain{static_cast<std::int64_t>(match_length * 4U) -
                                    static_cast<std::int64_t>(encoded_offset_bits) + 7};
                                if (candidate.length >= 4U && second_candidate_gain > second_current_gain)
                                {
                                    match_length = candidate.length;
                                    offset = candidate.offset;
                                    encoded_offset_base = candidate_base;
                                    encoded_offset_bits = high_bit(candidate_base);
                                    repeat_code = 0U;
                                    match_start = position;
                                    continue;
                                }
                            }
                        }
                        break;
                    }
                }

                if (repeat_code == 0U)
                {
                    auto match_position{match_start - offset};
                    while (match_start > anchor && match_position > 0U &&
                        input[match_start - 1U] == input[match_position - 1U])
                    {
                        --match_start;
                        --match_position;
                        ++match_length;
                    }
                    repeat_two = repeat_one;
                    repeat_one = offset;
                }
                result.sequences.push_back(parsed_sequence{
                    match_start - anchor, match_length, offset, repeat_code});
                position = match_start + match_length;
                anchor = position;
                if constexpr (RowMatch)
                    if (lazy_skipping)
                        row_state_.template fill<row_mls, row_log>(input,
                            static_cast<std::uint32_t>(limit), HashLog - row_log + 8U);
                lazy_skipping = false;

                while (position <= limit && repeat_two != 0U &&
                    equal_four(input, position, position - repeat_two))
                {
                    auto const repeat_length{count_match(
                        input, position, position - repeat_two, block_end, 4U)};
                    std::swap(repeat_one, repeat_two);
                    result.sequences.push_back(parsed_sequence{
                        0U, repeat_length, repeat_one, 1U});
                    position += repeat_length;
                    anchor = position;
                }
            }

            saved_two = saved_one != 0U && repeat_one != 0U ? saved_one : saved_two;
            repeat_offsets_[0] = repeat_one != 0U ? repeat_one : saved_one;
            repeat_offsets_[1] = repeat_two != 0U ? repeat_two : saved_two;
            result.trailing_literal_position = anchor;
            result.trailing_literal_length = block_end - anchor;
            return result;
        }

        [[nodiscard]] auto collect_binary_tree_matches(std::span<std::uint8_t const> input,
            std::size_t position, std::size_t block_end,
            std::span<binary_tree_match_candidate> output) -> std::size_t
        {
            if constexpr (!BinaryTree)
            {
                (void)input;
                (void)position;
                (void)block_end;
                (void)output;
                return 0U;
            }
            else
            {
                std::size_t count{};
                (void)find_best_binary_tree_match<true>(input, position, block_end, output, &count);
                return count;
            }
        }

        template <typename Workspace>
        [[nodiscard]] auto collect_optimal_candidates(std::span<std::uint8_t const> input,
            std::size_t position, std::size_t block_end, Workspace& workspace)
            -> std::span<binary_tree_match_candidate const>
        {
            auto const count{collect_binary_tree_matches(input, position, block_end,
                std::span<binary_tree_match_candidate>{workspace.optimal.matches})};
            return std::span<binary_tree_match_candidate const>{workspace.optimal.matches}.first(count);
        }

        template <typename Workspace>
        [[nodiscard]] auto collect_optimal_candidates(std::span<std::uint8_t const> input,
            std::size_t position, std::size_t block_end, Workspace& workspace,
            std::array<std::uint32_t, 3> const& repeats, bool no_literals,
            std::size_t sufficient_length) -> std::span<binary_tree_match_candidate const>
        {
            std::size_t count{};
            if (optimal_minimum_match() == 3U)
                (void)find_best_binary_tree_match<true, true>(input, position, block_end,
                    workspace.optimal.matches, &count, &repeats, no_literals, sufficient_length);
            else
                (void)find_best_binary_tree_match<true, false, true>(input, position, block_end,
                    workspace.optimal.matches, &count, &repeats, no_literals, sufficient_length);
            return std::span<binary_tree_match_candidate const>{workspace.optimal.matches}.first(count);
        }

    private:
        void ensure_index_headroom(std::size_t block_end)
        {
            constexpr auto maximum{std::numeric_limits<std::uint32_t>::max()};
            // Tree insertion can retain a position plus nine bytes of lookahead.
            constexpr auto guard{index_bias + 9U};
            if (block_end > maximum - guard)
                throw entropy_error{"match parser frame exceeds the 32-bit index range"};
            if constexpr (RowMatch)
            {
                if (static_cast<std::uint64_t>(row_state_.frame_base) + block_end + 1U <= maximum)
                    return;
                auto const old_base{row_state_.frame_base};
                auto* positions{row_state_.table.hash_table()};
                for (std::size_t slot{}; slot < row_state_.table_size; ++slot)
                    positions[slot] = positions[slot] > old_base ? positions[slot] - old_base : 0U;
                row_state_.frame_base = 0U;
            }
            else
            {
                if (static_cast<std::uint64_t>(frame_index_base_) + block_end + guard <= maximum)
                    return;
                auto const old_base{frame_index_base_};
                auto const first_current{old_base + index_bias};
                auto const rebase = [=](std::uint32_t index) {
                    return index >= first_current ? index - old_base : 0U;
                };
                for (auto& index : hash_table_) index = rebase(index);
                for (auto& index : hash3_table_) index = rebase(index);
                for (auto& index : chain_table_)
                {
                    if constexpr (BinaryTree)
                        if (index == unsorted_mark) continue;
                    index = rebase(index);
                }
                // Links are addressed by absolute index modulo the ring size.
                // Moving the origin therefore also moves slots, not just values.
                auto const shift{BinaryTree ? (2U * (old_base & tree_mask_)) : (old_base & chain_mask_)};
                std::rotate(chain_table_.begin(), chain_table_.begin() + shift, chain_table_.end());
                if constexpr (!BinaryTree)
                    std::rotate(chain_tags_.begin(), chain_tags_.begin() + shift, chain_tags_.end());
                next_to_update_ = std::max(index_bias, rebase(next_to_update_));
                next_to_update3_ = std::max(index_bias, rebase(next_to_update3_));
                frame_index_base_ = 0U;
                if constexpr (CacheAdjacentInsertion) comparison_cache_.rows = {};
            }
        }

        template <unsigned Configured>
        [[nodiscard]] static constexpr auto parameter_value(
            match_parameter<Configured> const& runtime) noexcept -> unsigned
        {
            if constexpr (Configured != 0U)
            {
                return Configured;
            }
            else
            {
                return runtime;
            }
        }

        struct match_result
        {
            std::uint32_t length{3U};
            std::uint32_t offset{};
        };

        static constexpr std::size_t hash_read_size{8U};
        static constexpr std::uint32_t index_bias{2U};
        static constexpr unsigned search_strength{8U};
        static constexpr std::size_t lazy_skipping_step{8U};
        static constexpr std::uint32_t block_update_skip_threshold{384U};
        static constexpr std::uint32_t block_update_tail{192U};

        [[nodiscard]] static constexpr auto high_bit(std::size_t value) -> unsigned
        {
            return static_cast<unsigned>(std::bit_width(value)) - 1U;
        }

        [[nodiscard]] auto find_best_match(std::span<std::uint8_t const> input,
            std::size_t position, std::size_t block_end, bool lazy_skipping) -> match_result
        {
            if constexpr (RowMatch)
            {
                static_assert(!BinaryTree && HashLog >= 6U && HashLog <= 30U &&
                    HashLog >= row_log && HashLog - row_log + 8U <= 32U && MinimumMatch >= 3U);
                auto const window{std::uint32_t{1} << WindowLog};
                auto const current{static_cast<std::uint32_t>(position)};
                auto const low{current > window ? current - window : 0U};
                // Candidate collection initializes exactly the returned prefix.
                std::array<std::uint32_t, 64> candidates;
                auto const count{row_state_.template find<row_mls, row_log,
                    (LazyDepth == 0U || LazyDepth == 1U)>(input,
                    current, low, HashLog - row_log + 8U,
                    std::uint32_t{1} << std::min(SearchLog, row_log), lazy_skipping, candidates)};
                match_result best;
                for (std::size_t index{}; index < count; ++index)
                {
                    auto const match{candidates[index]};
                    if (equal_four_bytes(input.data() + match + best.length - 3U,
                        input.data() + current + best.length - 3U))
                    {
                        auto const length{count_matching_bytes(input.data() + current,
                            input.data() + match, input.data() + block_end)};
                        if (length > best.length)
                        {
                            best.length = static_cast<std::uint32_t>(length);
                            best.offset = current - match;
                            if (position + length == block_end) break;
                        }
                    }
                }
                return best;
            }
            else if constexpr (BinaryTree)
            {
                return find_best_binary_tree_match(input, position, block_end);
            }
            else
            {
                return find_best_hash_chain_match(input, position, block_end, lazy_skipping);
            }
        }

        [[nodiscard]] auto find_best_hash_chain_match(std::span<std::uint8_t const> input,
            std::size_t position, std::size_t block_end, bool lazy_skipping) -> match_result
        {
            auto const* const base{input.data()};
            auto* const hash_table{hash_table_.data()};
            auto* const chain_table{chain_table_.data()};
            auto* const chain_tags{chain_tags_.data()};
            auto const current{frame_index_base_ + static_cast<std::uint32_t>(position) + index_bias};
            auto const insert_until = [&](std::uint32_t end)
            {
                while (next_to_update_ < end)
                {
                    auto const update_position{static_cast<std::size_t>(
                        next_to_update_ - frame_index_base_ - index_bias)};
                    auto const slot{hash(base + update_position)};
                    chain_table[next_to_update_ & chain_mask_] = hash_table[slot];
                    chain_tags[next_to_update_ & chain_mask_] = base[update_position];
                    hash_table[slot] = next_to_update_;
                    ++next_to_update_;
                }
            };
            // Reference zstd's row matcher bounds the table work crossed by a long match.
            // Keeping both ends preserves the useful future candidates without hashing the
            // usually redundant interior of large matches.
            if (!lazy_skipping && current - next_to_update_ > skip_threshold)
            {
                insert_until(next_to_update_ + maximum_match_start_updates);
                next_to_update_ = current - maximum_match_end_updates;
            }
            if (lazy_skipping)
            {
                insert_until(std::min(current, next_to_update_ + 1U));
            }
            else
            {
                insert_until(current);
            }
            next_to_update_ = current;

            auto match_index{hash_table[hash(base + position)]};
            auto const window_size{std::uint32_t{1} << parameter_value<WindowLog>(window_log_)};
            auto const frame_low{frame_index_base_ + index_bias};
            auto const low_limit{current - frame_low > window_size ? current - window_size : frame_low};
                auto const chain_size{static_cast<std::uint32_t>(chain_table_.size())};
            auto const minimum_chain{current > chain_size ? current - chain_size : 0U};
            auto attempts{std::uint32_t{1} << parameter_value<SearchLog>(search_log_)};
            match_result best;
            while (match_index >= low_limit && attempts-- != 0U)
            {
                auto const match_position{static_cast<std::size_t>(
                    match_index - frame_index_base_ - index_bias)};
                std::size_t current_length{};
                auto const chain_slot{match_index & chain_mask_};
                if (chain_tags[chain_slot] == base[position] &&
                    equal_four_bytes(base + match_position + best.length - 3U,
                        base + position + best.length - 3U))
                {
                    current_length = count_matching_bytes(
                        base + position, base + match_position, base + block_end);
                }
                if (current_length > best.length)
                {
                    best.length = static_cast<std::uint32_t>(current_length);
                    best.offset = current - match_index;
                    if (position + current_length == block_end)
                    {
                        break;
                    }
                }
                if (match_index <= minimum_chain)
                {
                    break;
                }
                match_index = chain_table[match_index & chain_mask_];
            }
            return best;
        }

        void update_binary_tree(std::span<std::uint8_t const> input, std::uint32_t current)
        {
            auto const tree_mask{tree_mask_};
            auto const* const base{input.data()};
            auto* const hash_table{hash_table_.data()};
            auto* const chain_table{chain_table_.data()};
            auto update_index{next_to_update_};
            auto update_position{static_cast<std::size_t>(
                update_index - frame_index_base_ - index_bias)};
            while (update_index < current)
            {
                auto const slot{hash(base + update_position)};
                auto const match_index{hash_table[slot]};
                auto const child_slot{2U * (update_index & tree_mask)};
                hash_table[slot] = update_index;
                chain_table[child_slot] = match_index;
                chain_table[child_slot + 1U] = unsorted_mark;
                ++update_index;
                ++update_position;
            }
            next_to_update_ = current;
        }

        template <bool TrackForward = false>
        [[nodiscard]] auto insert_binary_tree_candidate(std::span<std::uint8_t const> input,
            std::uint32_t current, std::size_t block_end, std::uint32_t attempts,
            std::uint32_t tree_low, std::uint32_t window_low) -> std::uint32_t
        {
            auto const tree_mask{tree_mask_};
            auto const* const base{input.data()};
            auto* const chain_table{chain_table_.data()};
            auto const current_position{static_cast<std::size_t>(
                current - frame_index_base_ - index_bias)};
            auto const* const current_base{base + current_position};
            auto smaller_slot{static_cast<std::size_t>(2U * (current & tree_mask))};
            auto larger_slot{smaller_slot + 1U};
            bool smaller_is_dummy{};
            bool larger_is_dummy{};
            auto match_index{chain_table[smaller_slot]};
            std::size_t common_smaller{};
            std::size_t common_larger{};
            [[maybe_unused]] auto match_end_index{current + 9U};
            [[maybe_unused]] std::size_t best_length{8U};
            while (attempts-- != 0U && match_index > window_low)
            {
#ifdef SPH_ZSTDPP_TRACE_BT
                ++binary_tree_trace.unsorted_visits;
#endif
                auto const next_slot{static_cast<std::size_t>(2U * (match_index & tree_mask))};
                auto const match_base{current_base - (current - match_index)};
                auto const length = [&]() -> std::size_t {
                    if constexpr (TrackForward && CacheAdjacentInsertion)
                    {
                        auto const cached{comparison_cache_.rows[(current - 1U) & 1U]
                            [(match_index - 1U) & 63U]};
                        if (cached.current == current - 1U &&
                            cached.match == match_index - 1U && cached.length != 0U)
                        {
                            // Shifting both strings by one byte under the same
                            // end turns a positive exact LCP into exactly LCP-1.
                            return cached.length - 1U;
                        }
                    }
                    auto const common{std::min(common_smaller, common_larger)};
                    return common + count_matching_bytes<true>(current_base + common,
                        match_base + common, base + block_end);
                }();
                if constexpr (TrackForward && CacheAdjacentInsertion)
                    comparison_cache_.rows[current & 1U][match_index & 63U] =
                        {current, match_index, static_cast<std::uint32_t>(length)};
#ifdef SPH_ZSTDPP_TRACE_BT
                ++binary_tree_trace.match_extensions;
#endif
                if constexpr (TrackForward)
                {
                    if (length > best_length)
                    {
                        best_length = length;
                        match_end_index = std::max(match_end_index,
                            static_cast<std::uint32_t>(match_index + length));
                    }
                }
                if (current_base + length == base + block_end)
                {
                    break;
                }
                if (match_base[length] < current_base[length])
                {
                    if (!smaller_is_dummy) chain_table[smaller_slot] = match_index;
                    common_smaller = length;
                    if (match_index <= tree_low)
                    {
                        smaller_is_dummy = true;
                        break;
                    }
                    smaller_slot = next_slot + 1U;
                    match_index = chain_table[next_slot + 1U];
                }
                else
                {
                    if (!larger_is_dummy) chain_table[larger_slot] = match_index;
                    common_larger = length;
                    if (match_index <= tree_low)
                    {
                        larger_is_dummy = true;
                        break;
                    }
                    larger_slot = next_slot;
                    match_index = chain_table[next_slot];
                }
            }
            if (!smaller_is_dummy) chain_table[smaller_slot] = 0U;
            if (!larger_is_dummy) chain_table[larger_slot] = 0U;
            if constexpr (TrackForward)
            {
                auto const repetitive_skip{best_length > 384U ?
                    std::min<std::size_t>(192U, best_length - 384U) : 0U};
                return std::max(static_cast<std::uint32_t>(repetitive_skip),
                    match_end_index - (current + 8U));
            }
            else
            {
                return 1U;
            }
        }

        void update_optimal_binary_tree(std::span<std::uint8_t const> input,
            std::uint32_t current, std::size_t block_end)
        {
            auto const tree_mask{tree_mask_};
            auto const* const base{input.data()};
            auto* const hash_table{hash_table_.data()};
            auto* const chain_table{chain_table_.data()};
            auto const window_size{std::uint32_t{1} << parameter_value<WindowLog>(window_log_)};
            auto const frame_low{frame_index_base_ + index_bias};
            auto const window_low{current - frame_low > window_size ? current - window_size : frame_low};
            auto update_index{next_to_update_};
            auto update_position{static_cast<std::size_t>(
                update_index - frame_index_base_ - index_bias)};
            while (update_index < current)
            {
                auto const slot{hash(base + update_position)};
                auto const child_slot{2U * (update_index & tree_mask)};
                chain_table[child_slot] = hash_table[slot];
                chain_table[child_slot + 1U] = unsorted_mark;
                hash_table[slot] = update_index;
                auto const tree_low{tree_mask >= update_index ? 0U : update_index - tree_mask};
                auto const forward{insert_binary_tree_candidate<true>(input, update_index, block_end,
                    std::uint32_t{1} << parameter_value<SearchLog>(search_log_), tree_low,
                    window_low - static_cast<std::uint32_t>(window_low != 0U))};
                auto const step{std::max(forward, 1U)};
#ifdef SPH_ZSTDPP_TRACE_OPTIMAL
                if (current - frame_low < 131076U)
                    std::fprintf(stderr, "U %u %u %u\n", update_index - frame_low,
                        current - frame_low, step);
#endif
                update_index += step;
                update_position += step;
            }
            next_to_update_ = current;
        }

        template <bool CollectAll = false, bool ThreeByte = false, bool RepeatCandidates = ThreeByte>
        [[nodiscard]] auto find_best_binary_tree_match(std::span<std::uint8_t const> input,
            std::size_t position, std::size_t block_end,
            std::span<binary_tree_match_candidate> output = {},
            std::size_t* collected_count = nullptr,
            std::array<std::uint32_t, 3> const* repeats = nullptr,
            bool no_literals = false, std::size_t sufficient_length = 0U) -> match_result
        {
            auto const* const base{input.data()};
            auto const* const current_base{base + position};
            auto* const hash_table{hash_table_.data()};
            auto* const chain_table{chain_table_.data()};
            auto const current{frame_index_base_ + static_cast<std::uint32_t>(position) + index_bias};
            if (current < next_to_update_)
            {
                return match_result{0U, 0U};
            }
            if constexpr (CollectAll)
                update_optimal_binary_tree(input, current, block_end);
            else
                update_binary_tree(input, current);

            auto const hash_slot{hash(base + position)};
            auto match_index{hash_table[hash_slot]};
            auto const window_size{std::uint32_t{1} << parameter_value<WindowLog>(window_log_)};
            auto const frame_low{frame_index_base_ + index_bias};
            auto const window_low{current - frame_low > window_size ? current - window_size : frame_low};
            auto const tree_mask{tree_mask_};
            auto const tree_low{tree_mask >= current ? 0U : current - tree_mask};
            auto const unsorted_limit{std::max(tree_low, window_low)};
            auto attempts{std::uint32_t{1} << parameter_value<SearchLog>(search_log_)};
            auto candidates{attempts};
            std::uint32_t previous_candidate{};

            if constexpr (!CollectAll)
            {
                while (match_index > unsorted_limit &&
                    chain_table[2U * (match_index & tree_mask) + 1U] == unsorted_mark &&
                    candidates > 1U)
                {
                    auto const candidate_slot{2U * (match_index & tree_mask)};
                    chain_table[candidate_slot + 1U] = previous_candidate;
                    previous_candidate = match_index;
                    match_index = chain_table[candidate_slot];
                    --candidates;
                }
                if (match_index > unsorted_limit &&
                    chain_table[2U * (match_index & tree_mask) + 1U] == unsorted_mark)
                {
                    auto const candidate_slot{2U * (match_index & tree_mask)};
                    chain_table[candidate_slot] = 0U;
                    chain_table[candidate_slot + 1U] = 0U;
                }

                match_index = previous_candidate;
                while (match_index != 0U)
                {
                    auto const next_candidate{chain_table[2U * (match_index & tree_mask) + 1U]};
                    (void)insert_binary_tree_candidate(input, match_index, block_end, candidates,
                        unsorted_limit, window_low);
                    match_index = next_candidate;
                    ++candidates;
                }
            }

            std::size_t common_smaller{};
            std::size_t common_larger{};
            auto smaller_slot{static_cast<std::size_t>(2U * (current & tree_mask))};
            auto larger_slot{smaller_slot + 1U};
            bool smaller_is_dummy{};
            bool larger_is_dummy{};
            auto match_end_index{current + 9U};
            match_result best{0U, 0U};
            std::size_t best_offset_base{999999999U};
            std::size_t longest_collected{ThreeByte ? 2U : 3U};
            if constexpr (RepeatCandidates)
            {
                // The reference checks repcodes, then HC3, then the main tree.
                // In particular, a sufficient repeat must not insert this node.
                auto append = [&](std::uint32_t offset, std::size_t length)
                {
                    if (*collected_count < output.size())
                        output[*collected_count] = {offset, static_cast<std::uint32_t>(length)};
                    ++*collected_count;
                    longest_collected = length;
                };
                for (std::uint32_t code{1U}; code <= 3U; ++code)
                {
                    auto const rep_index{code - 1U + static_cast<std::uint32_t>(no_literals)};
                    auto const distance{rep_index == 3U ? (*repeats)[0] - 1U : (*repeats)[rep_index]};
                    if (distance == 0U || distance > current - window_low) continue;
                    // Reject short repeat misses before entering the word matcher.
                    // For minMatch3 the fourth readable byte is not compared.
                    auto const difference{load_native_u32(current_base) ^
                        load_native_u32(current_base - distance)};
                    constexpr auto mask{!ThreeByte ? 0xFFFFFFFFU :
                        (std::endian::native == std::endian::big ? 0xFFFFFF00U : 0x00FFFFFFU)};
                    if ((difference & mask) != 0U) continue;
                    constexpr auto minimum{ThreeByte ? 3U : 4U};
                    auto const length{minimum + count_matching_bytes<true>(current_base + minimum,
                        current_base - distance + minimum, base + block_end)};
                    if (length <= longest_collected) continue;
                    append(code, length);
                    if (length > sufficient_length || position + length == block_end) return best;
                }
                if (ThreeByte && longest_collected < 3U)
                {
                    auto const hash_log3{std::min(17U, parameter_value<WindowLog>(window_log_))};
                    auto hash3 = [hash_log3](std::uint8_t const* bytes)
                    {
                        auto value{load_native_u32(bytes)};
                        if constexpr (std::endian::native == std::endian::big)
                            value = std::byteswap(value);
                        return ((value << 8U) * 506832829U) >> (32U - hash_log3);
                    };
                    for (; next_to_update3_ < current; ++next_to_update3_)
                        hash3_table_[hash3(base + next_to_update3_ - frame_low)] = next_to_update3_;
                    auto const match3{hash3_table_[hash3(current_base)]};
                    auto const distance{current - match3};
                    if (match3 >= window_low && match3 < current && distance < (1U << 18U))
                    {
                        auto const length{count_matching_bytes<true>(current_base,
                            current_base - distance, base + block_end)};
                        if (length >= 3U)
                        {
                            append(distance + 3U, length);
                            if (length > sufficient_length || position + length == block_end)
                            {
                                next_to_update_ = current + 1U;
                                return best;
                            }
                        }
                    }
                }
            }
            match_index = hash_table[hash_slot];
            hash_table[hash_slot] = current;

            while (attempts-- != 0U &&
                (CollectAll ? match_index >= window_low : match_index > window_low))
            {
#ifdef SPH_ZSTDPP_TRACE_BT
                ++binary_tree_trace.main_visits;
#endif
                auto const next_slot{static_cast<std::size_t>(2U * (match_index & tree_mask))};
                auto const match_base{current_base - (current - match_index)};
                auto const common{std::min(common_smaller, common_larger)};
                auto const length{common + count_matching_bytes<true>(current_base + common,
                    match_base + common, base + block_end)};
#ifdef SPH_ZSTDPP_TRACE_BT
                ++binary_tree_trace.match_extensions;
#endif
                if constexpr (CollectAll)
                {
                    if (length > longest_collected)
                    {
                        match_end_index = std::max(match_end_index,
                            static_cast<std::uint32_t>(match_index + length));
                        if (*collected_count < output.size())
                            output[*collected_count] = binary_tree_match_candidate{
                                static_cast<std::uint32_t>(current - match_index + 3U),
                                static_cast<std::uint32_t>(length)};
                        ++*collected_count;
                        longest_collected = length;
                        // Reference stops at the first match exceeding its
                        // optimal path window, even if a longer match exists.
                        if (length > 4096U) break;
                    }
                    if (current_base + length == base + block_end)
                    {
                        break;
                    }
                }
                else if (length > best.length)
                {
                    match_end_index = std::max(match_end_index,
                        static_cast<std::uint32_t>(match_index + length));
                    auto const candidate_offset_base{
                        static_cast<std::size_t>(current - match_index) + 3U};
                    auto const length_gain{static_cast<std::int64_t>(4U * (length - best.length))};
                    auto const offset_cost{static_cast<std::int64_t>(high_bit(candidate_offset_base)) -
                        static_cast<std::int64_t>(high_bit(best_offset_base))};
                    if (length_gain > offset_cost)
                    {
                        best.length = static_cast<std::uint32_t>(length);
                        best.offset = current - match_index;
                        best_offset_base = candidate_offset_base;
                    }
                    if (current_base + length == base + block_end) break;
                }

                if (match_base[length] < current_base[length])
                {
                    if (!smaller_is_dummy) chain_table[smaller_slot] = match_index;
                    common_smaller = length;
                    if (match_index <= tree_low)
                    {
                        smaller_is_dummy = true;
                        break;
                    }
                    smaller_slot = next_slot + 1U;
                    match_index = chain_table[next_slot + 1U];
                }
                else
                {
                    if (!larger_is_dummy) chain_table[larger_slot] = match_index;
                    common_larger = length;
                    if (match_index <= tree_low)
                    {
                        larger_is_dummy = true;
                        break;
                    }
                    larger_slot = next_slot;
                    match_index = chain_table[next_slot];
                }
            }
            if (!smaller_is_dummy) chain_table[smaller_slot] = 0U;
            if (!larger_is_dummy) chain_table[larger_slot] = 0U;
            next_to_update_ = match_end_index - 8U;
            return best;
        }

        [[nodiscard]] static auto read_word(std::span<std::uint8_t const> input,
            std::size_t position) -> std::uint64_t
        {
            return load_little_u64(input, position);
        }

        [[nodiscard]] auto hash(std::uint8_t const* input) const -> std::size_t
        {
            if constexpr (MinimumMatch == 5U)
            {
                auto value{load_native_u64(input)};
                if constexpr (std::endian::native == std::endian::big)
                    value = std::byteswap(value);
                return static_cast<std::size_t>(((value << 24U) * 889523592379ULL) >>
                    (64U - parameter_value<HashLog>(hash_log_)));
            }
            else if constexpr (MinimumMatch == 6U)
            {
                auto value{load_native_u64(input)};
                if constexpr (std::endian::native == std::endian::big)
                    value = std::byteswap(value);
                return static_cast<std::size_t>(((value << 16U) * 227718039650203ULL) >>
                    (64U - parameter_value<HashLog>(hash_log_)));
            }
            else if constexpr (MinimumMatch != 0U)
            {
                auto const value{load_native_u32(input)};
                if constexpr (std::endian::native == std::endian::big)
                {
                    return static_cast<std::size_t>((std::byteswap(value) * 2654435761U) >>
                        (32U - parameter_value<HashLog>(hash_log_)));
                }
                return static_cast<std::size_t>((value * 2654435761U) >>
                    (32U - parameter_value<HashLog>(hash_log_)));
            }
            else
            {
                auto const minimum_match{parameter_value<MinimumMatch>(minimum_match_)};
                auto const hash_log{parameter_value<HashLog>(hash_log_)};
                if (minimum_match == 5U)
                {
                    auto value{load_native_u64(input)};
                    if constexpr (std::endian::native == std::endian::big)
                        value = std::byteswap(value);
                    return static_cast<std::size_t>(((value << 24U) * 889523592379ULL) >>
                        (64U - hash_log));
                }
                if (minimum_match == 6U)
                {
                    auto value{load_native_u64(input)};
                    if constexpr (std::endian::native == std::endian::big)
                        value = std::byteswap(value);
                    return static_cast<std::size_t>(((value << 16U) * 227718039650203ULL) >>
                        (64U - hash_log));
                }
                auto value{load_native_u32(input)};
                if constexpr (std::endian::native == std::endian::big)
                    value = std::byteswap(value);
                return static_cast<std::size_t>((value * 2654435761U) >>
                    (32U - hash_log));
            }
        }

        [[nodiscard]] auto hash(std::span<std::uint8_t const> input,
            std::size_t position) const -> std::size_t
        {
            return hash(input.data() + position);
        }

        [[nodiscard]] static auto equal_four(std::span<std::uint8_t const> input,
            std::size_t left, std::size_t right) -> bool
        {
            return equal_four_bytes(input, left, right);
        }

        [[nodiscard]] static auto count_match(std::span<std::uint8_t const> input,
            std::size_t position, std::size_t match_position, std::size_t end,
            std::size_t initial) -> std::size_t
        {
            return count_matching_bytes(input, position, match_position, end, initial);
        }

        match_parameter<WindowLog> window_log_;
        match_parameter<HashLog> hash_log_;
        match_parameter<ChainLog> chain_log_;
        match_parameter<SearchLog> search_log_;
        match_parameter<MinimumMatch> minimum_match_;
        match_table hash_table_;
        match_table hash3_table_;
        match_table chain_table_;
        row_tag_table chain_tags_;
        std::uint32_t chain_mask_{};
        std::uint32_t tree_mask_{};
        static constexpr unsigned row_log{std::clamp(SearchLog, 4U, 6U)};
        static constexpr unsigned row_mls{std::clamp(MinimumMatch, 4U, 6U)};
        [[no_unique_address]] row_parser_workspace<RowMatch> row_state_;
        std::uint32_t frame_index_base_{};
        std::uint32_t next_to_update_{index_bias};
        std::uint32_t next_to_update3_{index_bias};
        std::array<std::size_t, 3> repeat_offsets_{1U, 4U, 8U};
        static constexpr std::uint32_t skip_threshold{384U};
        static constexpr std::uint32_t maximum_match_start_updates{96U};
        static constexpr std::uint32_t maximum_match_end_updates{32U};
        static constexpr std::uint32_t unsorted_mark{1U};
#if defined(_MSC_VER)
        [[msvc::no_unique_address]]
#else
        [[no_unique_address]]
#endif
        adjacent_insertion_cache<CacheAdjacentInsertion> comparison_cache_;
    };

    /**
     * A single-candidate hash-chain match finder. This is the baseline for the
     * `fast` strategy: it is linear, bounded by the input block, and deliberately
     * keeps only the most recent position for each hash. More expensive template
     * strategies can replace it without changing the frame or streaming layers.
     */
    struct fast_match_scratch
    {
        std::vector<std::size_t> positions;
        std::vector<std::uint32_t> generations;
        std::uint32_t generation{};

        void prepare(std::size_t table_size)
        {
            if (positions.size() != table_size)
            {
                positions.resize(table_size);
                generations.assign(table_size, 0U);
                generation = 0U;
            }
            ++generation;
            if (generation == 0U)
            {
                std::ranges::fill(generations, 0U);
                generation = 1U;
            }
        }
    };

    [[nodiscard]] inline auto find_best_fast_match(
        std::span<std::uint8_t const> input, unsigned hash_log, std::size_t minimum_match = 4U,
        fast_match_scratch* reusable_latest = nullptr)
        -> std::optional<match>
    {
        minimum_match = std::max<std::size_t>(minimum_match, 4U);
        if (input.size() < minimum_match)
        {
            return std::nullopt;
        }
        hash_log = std::clamp(hash_log, 10U, 20U);
        auto const table_size{std::size_t{1} << hash_log};
        auto constexpr missing{std::numeric_limits<std::size_t>::max()};
        fast_match_scratch local_latest;
        auto& latest{reusable_latest != nullptr ? *reusable_latest : local_latest};
        latest.prepare(table_size);
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
            auto const candidate{latest.generations[slot] == latest.generation ?
                    latest.positions[slot] : missing};
            latest.positions[slot] = position;
            latest.generations[slot] = latest.generation;
            if (candidate == missing || candidate >= position || position + minimum_match > input.size() ||
                !std::ranges::equal(input.subspan(candidate, minimum_match),
                    input.subspan(position, minimum_match)))
            {
                continue;
            }
            if (best && input.size() - position <= best->length)
            {
                continue;
            }

            std::size_t length{minimum_match};
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

    class forward_bit_writer
    {
    public:
        void reset() noexcept
        {
            bytes_.clear();
            external_output_ = nullptr;
            external_write_position_ = 0U;
            bit_container_ = 0U;
            bit_count_ = 0U;
        }

        void reset(std::vector<std::uint8_t>& output) noexcept
        {
            bytes_.clear();
            external_output_ = &output;
            external_write_position_ = output.size();
            bit_container_ = 0U;
            bit_count_ = 0U;
        }

        void append(std::uint32_t value, unsigned count)
        {
            if (count == 0U)
            {
                return;
            }
            auto const mask{count == 32U ? std::numeric_limits<std::uint32_t>::max() :
                (std::uint32_t{1} << count) - 1U};
            bit_container_ |= static_cast<std::uint64_t>(value & mask) << bit_count_;
            bit_count_ += count;
            if (bit_count_ >= 32U)
            {
                flush_word();
            }
        }

        // FSE symbols always provide a non-zero count and are already bounded
        // to the low bits being emitted. Keep this path free of the generic
        // count and masking checks used by arbitrary fields.
        void append_unchecked(std::uint32_t value, unsigned count)
        {
            bit_container_ |= static_cast<std::uint64_t>(value) << bit_count_;
            bit_count_ += count;
            if (bit_count_ >= 32U)
            {
                flush_word();
            }
        }

        [[nodiscard]] auto finish() -> std::vector<std::uint8_t>
        {
            std::vector<std::uint8_t> output;
            finish_into(output);
            return output;
        }

        void finish_into(std::vector<std::uint8_t>& output)
        {
            if (external_output_ == nullptr)
            {
                output.assign(bytes_.begin(), bytes_.end());
            }
            else if (external_output_ != &output)
            {
                throw entropy_error{"bit writer finished into a different output buffer"};
            }
            else
            {
                output.resize(external_write_position_);
            }
            auto remaining{bit_container_};
            auto const byte_count{(bit_count_ + 7U) / 8U};
            for (unsigned index{}; index < byte_count; ++index)
            {
                output.push_back(static_cast<std::uint8_t>(remaining));
                remaining >>= 8U;
            }
            finish_marker(output);
            external_output_ = nullptr;
        }

    private:
        void flush_word()
        {
            auto completed{static_cast<std::uint32_t>(bit_container_)};
            if constexpr (std::endian::native == std::endian::big)
            {
                completed = std::byteswap(completed);
            }
            if (external_output_ != nullptr)
            {
                // Resize in chunks so a long sequence stream does not update
                // vector size and zero-initialize four bytes on every flush.
                // finish_into() removes the unused part of the last chunk.
                constexpr std::size_t chunk_size{256U};
                auto& output{*external_output_};
                if (external_write_position_ + sizeof(completed) > output.size())
                {
                    output.resize(external_write_position_ + chunk_size);
                }
                std::memcpy(output.data() + external_write_position_, &completed, sizeof(completed));
                external_write_position_ += sizeof(completed);
            }
            else
            {
                auto const previous_size{bytes_.size()};
                bytes_.resize(previous_size + sizeof(completed));
                std::memcpy(bytes_.data() + previous_size, &completed, sizeof(completed));
            }
            bit_container_ >>= 32U;
            bit_count_ -= 32U;
        }

        void finish_marker(std::vector<std::uint8_t>& output) const
        {
            auto const bit_offset{bit_count_ & 7U};
            if (bit_offset == 0U)
            {
                output.push_back(1U);
            }
            else
            {
                output.back() |= static_cast<std::uint8_t>(1U << bit_offset);
            }
        }

        std::vector<std::uint8_t> bytes_;
        std::vector<std::uint8_t>* external_output_{};
        std::size_t external_write_position_{};
        std::uint64_t bit_container_{};
        unsigned bit_count_{};
    };

    // The short-frame fast path benefits from a 64-bit accumulator. Writes may
    // overlap by one partial byte; finish_into() trims the speculative tail.
    class wide_sequence_bit_writer
    {
    public:
        void reset(std::vector<std::uint8_t>& output) noexcept
        {
            output_ = &output;
            position_ = output.size();
            container_ = 0U;
            count_ = 0U;
        }

        void append(std::uint32_t value, unsigned count)
        {
            assert(count <= 32U);
            if (count == 0U) return;
            auto const mask{count == 32U ? std::numeric_limits<std::uint32_t>::max() :
                (std::uint32_t{1} << count) - 1U};
            append_unchecked(value & mask, count);
        }

        void append_unchecked(std::uint32_t value, unsigned count)
        {
            assert(count <= 32U);
            if (count_ + count > 64U) flush_bytes();
            container_ |= static_cast<std::uint64_t>(value) << count_;
            count_ += count;
            if (count_ >= 56U) flush_bytes();
        }

        void finish_into(std::vector<std::uint8_t>& output)
        {
            if (output_ != &output)
                throw entropy_error{"wide bit writer finished into a different output buffer"};
            output.resize(position_);
            auto remaining{container_};
            auto const byte_count{(count_ + 7U) / 8U};
            for (unsigned index{}; index < byte_count; ++index)
            {
                output.push_back(static_cast<std::uint8_t>(remaining));
                remaining >>= 8U;
            }
            if ((count_ & 7U) == 0U)
                output.push_back(1U);
            else
                output.back() |= static_cast<std::uint8_t>(1U << (count_ & 7U));
            output_ = nullptr;
        }

    private:
        void flush_bytes()
        {
            assert(output_ != nullptr && count_ >= 8U);
            auto completed{container_};
            if constexpr (std::endian::native == std::endian::big)
                completed = std::byteswap(completed);
            auto& output{*output_};
            constexpr std::size_t chunk_size{256U};
            if (position_ + sizeof(completed) > output.size())
                output.resize(position_ + chunk_size);
            std::memcpy(output.data() + position_, &completed, sizeof(completed));
            auto const completed_bytes{count_ / 8U};
            position_ += completed_bytes;
            container_ = completed_bytes == 8U ? 0U : container_ >> (completed_bytes * 8U);
            count_ &= 7U;
        }

        std::vector<std::uint8_t>* output_{};
        std::size_t position_{};
        std::uint64_t container_{};
        unsigned count_{};
    };

    struct fse_compression_transform
    {
        std::int32_t delta_find_state{};
        std::uint32_t delta_number_bits{};
    };

    struct fse_compression_table
    {
        static constexpr unsigned maximum_table_log{9U};
        std::array<std::uint16_t, 1U << maximum_table_log> states{};
        std::array<fse_compression_transform, 256> transforms{};
        unsigned table_log{};
        std::optional<unsigned> run_length_symbol;
    };

    struct sequence_table_history
    {
        fse_compression_table table{};
        normalized_counts counts{};
        bool valid{};
    };

    [[nodiscard]] inline auto build_fse_compression_table(normalized_counts const& counts)
        -> fse_compression_table
    {
        if (counts.table_log > fse_compression_table::maximum_table_log)
        {
            throw entropy_error{"FSE compression table log exceeds supported maximum"};
        }
        auto const table_size{std::size_t{1} << counts.table_log};
        auto const table_mask{table_size - 1U};
        auto const step{(table_size >> 1U) + (table_size >> 3U) + 3U};
        std::array<std::uint16_t, 257> cumulative{};
        // Compression tables are capped at maximum_table_log; the previous
        // 4K scratch buffer was needlessly eight times larger than required.
        std::array<std::uint8_t, 1U << fse_compression_table::maximum_table_log> symbols{};
        auto high_threshold{table_size - 1U};

        for (unsigned symbol{1U}; symbol <= counts.maximum_symbol + 1U; ++symbol)
        {
            auto const count{counts.values[symbol - 1U]};
            if (count == -1)
            {
                cumulative[symbol] = static_cast<std::uint16_t>(cumulative[symbol - 1U] + 1U);
                symbols[high_threshold--] = static_cast<std::uint8_t>(symbol - 1U);
            }
            else
            {
                cumulative[symbol] = static_cast<std::uint16_t>(cumulative[symbol - 1U] + count);
            }
        }

        std::size_t position{};
        for (unsigned symbol{}; symbol <= counts.maximum_symbol; ++symbol)
        {
            for (int occurrence{}; occurrence < counts.values[symbol]; ++occurrence)
            {
                symbols[position] = static_cast<std::uint8_t>(symbol);
                position = (position + step) & table_mask;
                while (position > high_threshold)
                {
                    position = (position + step) & table_mask;
                }
            }
        }
        if (position != 0U)
        {
            throw entropy_error{"invalid Zstandard FSE compression symbol spread"};
        }

        fse_compression_table result{};
        result.table_log = counts.table_log;
        for (std::size_t index{}; index < table_size; ++index)
        {
            auto const symbol{symbols[index]};
            result.states[cumulative[symbol]++] = static_cast<std::uint16_t>(table_size + index);
        }

        unsigned total{};
        for (unsigned symbol{}; symbol <= counts.maximum_symbol; ++symbol)
        {
            auto const count{counts.values[symbol]};
            auto& transform{result.transforms[symbol]};
            if (count == 0)
            {
                transform.delta_number_bits = ((counts.table_log + 1U) << 16U) -
                    static_cast<unsigned>(table_size);
            }
            else if (count == -1 || count == 1)
            {
                transform.delta_number_bits = (counts.table_log << 16U) -
                    static_cast<unsigned>(table_size);
                transform.delta_find_state = static_cast<std::int32_t>(total) - 1;
                ++total;
            }
            else
            {
                auto const maximum_bits{counts.table_log -
                    (static_cast<unsigned>(std::bit_width(static_cast<unsigned>(count - 1))) - 1U)};
                auto const minimum_state{static_cast<unsigned>(count) << maximum_bits};
                transform.delta_number_bits = (maximum_bits << 16U) - minimum_state;
                transform.delta_find_state = static_cast<std::int32_t>(total - static_cast<unsigned>(count));
                total += static_cast<unsigned>(count);
            }
        }
        return result;
    }

    [[nodiscard]] inline auto make_run_length_fse_table(unsigned symbol) -> fse_compression_table
    {
        return {{}, {}, 0U, symbol};
    }

    class fse_compression_state
    {
    public:
        explicit fse_compression_state(fse_compression_table const& table) : table_{&table} {}

        void initialize(unsigned symbol)
        {
            if (table_->run_length_symbol)
            {
                if (symbol != *table_->run_length_symbol)
                {
                    throw entropy_error{"symbol does not match Zstandard run-length FSE table"};
                }
                value_ = 0U;
                return;
            }
            auto const transform{table_->transforms[symbol]};
            auto const number_bits{(transform.delta_number_bits + (1U << 15U)) >> 16U};
            value_ = (number_bits << 16U) - transform.delta_number_bits;
            auto const index{static_cast<std::int64_t>(value_ >> number_bits) + transform.delta_find_state};
            value_ = table_->states[static_cast<std::size_t>(index)];
        }

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
        void encode(forward_bit_writer& bits, unsigned symbol)
        {
            encode_inlined(bits, symbol);
        }

        [[gnu::always_inline]]
        void encode_inlined(forward_bit_writer& bits, unsigned symbol)
#else
        void encode(forward_bit_writer& bits, unsigned symbol)
#endif
        {
            if (table_->run_length_symbol)
            {
                if (symbol != *table_->run_length_symbol)
                {
                    throw entropy_error{"symbol does not match Zstandard run-length FSE table"};
                }
                return;
            }
            auto const transform{table_->transforms[symbol]};
            auto const number_bits{(value_ + transform.delta_number_bits) >> 16U};
            auto const emitted{value_ & ((std::uint32_t{1} << number_bits) - 1U)};
            bits.append_unchecked(emitted, number_bits);
            auto const index{static_cast<std::int64_t>(value_ >> number_bits) + transform.delta_find_state};
            value_ = table_->states[static_cast<std::size_t>(index)];
        }

        template <typename BitWriter>
        void flush(BitWriter& bits) const
        {
            if (!table_->run_length_symbol)
            {
                bits.append(value_, table_->table_log);
            }
        }

        struct encoded_bits
        {
            std::uint32_t value;
            unsigned count;
        };

        template <bool NarrowIndex = false>
        [[nodiscard]] auto encode_bits(unsigned symbol) -> encoded_bits
        {
            if (table_->run_length_symbol)
            {
                if (symbol != *table_->run_length_symbol)
                    throw entropy_error{"symbol does not match Zstandard run-length FSE table"};
                return {0U, 0U};
            }
            auto const transform{table_->transforms[symbol]};
            auto const number_bits{(value_ + transform.delta_number_bits) >> 16U};
            auto const emitted{value_ & ((std::uint32_t{1} << number_bits) - 1U)};
            if constexpr (NarrowIndex)
            {
                // Valid FSE tables map states into [0, 2^table_log).
                // Unsigned addition represents negative deltas modulo 2^32;
                // the mathematical result is nonnegative and at most 511.
                auto const index{(value_ >> number_bits) +
                    static_cast<std::uint32_t>(transform.delta_find_state)};
                value_ = table_->states[index];
            }
            else
            {
                auto const index{static_cast<std::int64_t>(value_ >> number_bits) + transform.delta_find_state};
                value_ = table_->states[static_cast<std::size_t>(index)];
            }
            return {emitted, number_bits};
        }

    private:
        fse_compression_table const* table_{};
        std::uint32_t value_{};
    };

    [[nodiscard]] inline auto optimal_fse_table_log(unsigned maximum_log, std::size_t size,
        unsigned maximum_symbol) -> unsigned
    {
        auto const maximum_source_bits{static_cast<unsigned>(std::bit_width(size - 1U)) - 1U};
        auto const reduced_source_bits{maximum_source_bits > 2U ? maximum_source_bits - 2U : 0U};
        auto const minimum_source_bits{static_cast<unsigned>(std::bit_width(size))};
        auto const minimum_symbol_bits{maximum_symbol == 0U ? 2U :
            static_cast<unsigned>(std::bit_width(maximum_symbol)) + 1U};
        auto const minimum_bits{std::min(minimum_source_bits, minimum_symbol_bits)};
        auto table_log{std::min(maximum_log, reduced_source_bits)};
        table_log = std::max(table_log, minimum_bits);
        return std::clamp(table_log, 5U, 12U);
    }

    inline void normalize_counts_secondary(normalized_counts& result,
        std::span<unsigned const> counts, std::size_t total, std::int16_t low_probability_count)
    {
        constexpr std::int16_t unassigned{-2};
        auto distributed{0U};
        auto const low_threshold{static_cast<unsigned>(total >> result.table_log)};
        auto low_one{static_cast<unsigned>((total * 3U) >> (result.table_log + 1U))};
        for (unsigned symbol{}; symbol <= result.maximum_symbol; ++symbol)
        {
            if (counts[symbol] == 0U)
            {
                result.values[symbol] = 0;
            }
            else if (counts[symbol] <= low_threshold)
            {
                result.values[symbol] = low_probability_count;
                ++distributed;
                total -= counts[symbol];
            }
            else if (counts[symbol] <= low_one)
            {
                result.values[symbol] = 1;
                ++distributed;
                total -= counts[symbol];
            }
            else
            {
                result.values[symbol] = unassigned;
            }
        }
        auto to_distribute{(1U << result.table_log) - distributed};
        if (to_distribute == 0U)
        {
            return;
        }
        if (total / to_distribute > low_one)
        {
            low_one = static_cast<unsigned>((total * 3U) / (to_distribute * 2U));
            for (unsigned symbol{}; symbol <= result.maximum_symbol; ++symbol)
            {
                if (result.values[symbol] == unassigned && counts[symbol] <= low_one)
                {
                    result.values[symbol] = 1;
                    ++distributed;
                    total -= counts[symbol];
                }
            }
            to_distribute = (1U << result.table_log) - distributed;
        }
        if (distributed == result.maximum_symbol + 1U)
        {
            unsigned largest{};
            for (unsigned symbol{1U}; symbol <= result.maximum_symbol; ++symbol)
            {
                if (counts[symbol] > counts[largest]) largest = symbol;
            }
            result.values[largest] = static_cast<std::int16_t>(
                static_cast<int>(result.values[largest]) + static_cast<int>(to_distribute));
            return;
        }
        if (total == 0U)
        {
            for (unsigned symbol{}; to_distribute != 0U;
                symbol = (symbol + 1U) % (result.maximum_symbol + 1U))
            {
                if (result.values[symbol] > 0)
                {
                    --to_distribute;
                    ++result.values[symbol];
                }
            }
            return;
        }

        auto const step_log{62U - result.table_log};
        auto const middle{(std::uint64_t{1} << (step_log - 1U)) - 1U};
        auto const step{((std::uint64_t{1} << step_log) * to_distribute + middle) / total};
        auto temporary_total{middle};
        for (unsigned symbol{}; symbol <= result.maximum_symbol; ++symbol)
        {
            if (result.values[symbol] == unassigned)
            {
                auto const end{temporary_total + counts[symbol] * step};
                auto const start_value{temporary_total >> step_log};
                auto const end_value{end >> step_log};
                auto const weight{end_value - start_value};
                if (weight == 0U)
                {
                    throw entropy_error{"cannot normalize Zstandard FSE counts"};
                }
                result.values[symbol] = static_cast<std::int16_t>(weight);
                temporary_total = end;
            }
        }
    }

    [[nodiscard]] inline auto normalize_counts(std::span<unsigned const> counts, std::size_t total,
        unsigned maximum_symbol, unsigned table_log, bool use_low_probability_count) -> normalized_counts
    {
        static constexpr std::array<std::uint32_t, 8> rounding_thresholds{
            0U, 473195U, 504333U, 520860U, 550000U, 700000U, 750000U, 830000U};
        normalized_counts result{};
        result.maximum_symbol = maximum_symbol;
        result.table_log = table_log;
        auto const low_probability_count{static_cast<std::int16_t>(use_low_probability_count ? -1 : 1)};
        auto const scale{62U - table_log};
        auto const step{(std::uint64_t{1} << 62U) / total};
        auto const value_step{std::uint64_t{1} << (scale - 20U)};
        auto remaining{static_cast<int>(1U << table_log)};
        unsigned largest{};
        std::int16_t largest_probability{};
        auto const low_threshold{static_cast<unsigned>(total >> table_log)};
        for (unsigned symbol{}; symbol <= maximum_symbol; ++symbol)
        {
            if (counts[symbol] == 0U)
            {
                result.values[symbol] = 0;
            }
            else if (counts[symbol] <= low_threshold)
            {
                result.values[symbol] = low_probability_count;
                --remaining;
            }
            else
            {
                auto probability{static_cast<std::int16_t>((counts[symbol] * step) >> scale)};
                if (probability < 8)
                {
                    auto const rest_to_beat{value_step * rounding_thresholds[static_cast<std::size_t>(probability)]};
                    probability = static_cast<std::int16_t>(probability +
                        static_cast<std::int16_t>(counts[symbol] * step -
                            (static_cast<std::uint64_t>(probability) << scale) > rest_to_beat));
                }
                if (probability > largest_probability)
                {
                    largest_probability = probability;
                    largest = symbol;
                }
                result.values[symbol] = probability;
                remaining -= probability;
            }
        }
        if (-remaining >= (result.values[largest] >> 1))
        {
            normalize_counts_secondary(result, counts, total, low_probability_count);
        }
        else
        {
            result.values[largest] = static_cast<std::int16_t>(result.values[largest] + remaining);
        }
        return result;
    }

    inline void write_normalized_counts(
        normalized_counts const& counts, std::vector<std::uint8_t>& output)
    {
        output.clear();
        std::uint32_t bit_stream{counts.table_log - 5U};
        auto bit_count{4};
        auto remaining{static_cast<int>((1U << counts.table_log) + 1U)};
        auto threshold{static_cast<int>(1U << counts.table_log)};
        auto number_bits{static_cast<int>(counts.table_log + 1U)};
        unsigned symbol{};
        bool previous_zero{};
        auto flush_sixteen = [&]
        {
            output.push_back(static_cast<std::uint8_t>(bit_stream));
            output.push_back(static_cast<std::uint8_t>(bit_stream >> 8U));
            bit_stream >>= 16U;
            bit_count -= 16;
        };

        while (symbol <= counts.maximum_symbol && remaining > 1)
        {
            if (previous_zero)
            {
                auto start{symbol};
                while (symbol <= counts.maximum_symbol && counts.values[symbol] == 0) ++symbol;
                while (symbol >= start + 24U)
                {
                    start += 24U;
                    bit_stream += 0xFFFFU << bit_count;
                    output.push_back(static_cast<std::uint8_t>(bit_stream));
                    output.push_back(static_cast<std::uint8_t>(bit_stream >> 8U));
                    bit_stream >>= 16U;
                }
                while (symbol >= start + 3U)
                {
                    start += 3U;
                    bit_stream += 3U << bit_count;
                    bit_count += 2;
                }
                bit_stream += (symbol - start) << bit_count;
                bit_count += 2;
                if (bit_count > 16) flush_sixteen();
            }
            auto count{static_cast<int>(counts.values[symbol++])};
            auto const maximum{(2 * threshold - 1) - remaining};
            remaining -= count < 0 ? -count : count;
            ++count;
            if (count >= threshold) count += maximum;
            bit_stream += static_cast<std::uint32_t>(count) << bit_count;
            bit_count += number_bits;
            bit_count -= static_cast<int>(count < maximum);
            previous_zero = count == 1;
            while (remaining < threshold)
            {
                --number_bits;
                threshold >>= 1;
            }
            if (bit_count > 16) flush_sixteen();
        }
        if (remaining != 1)
        {
            throw entropy_error{"invalid normalized Zstandard FSE counts"};
        }
        output.push_back(static_cast<std::uint8_t>(bit_stream));
        if (bit_count > 8) output.push_back(static_cast<std::uint8_t>(bit_stream >> 8U));
    }

    [[nodiscard]] inline auto write_normalized_counts(normalized_counts const& counts)
        -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> output;
        write_normalized_counts(counts, output);
        return output;
    }

    inline void compress_fse_bytes(std::span<std::uint8_t const> input,
        fse_compression_table const& table, forward_bit_writer& bits,
        std::vector<std::uint8_t>& output)
    {
        output.clear();
        if (input.size() <= 2U)
        {
            return;
        }
        auto position{input.size()};
        fse_compression_state state_one{table};
        fse_compression_state state_two{table};
        bits.reset(output);
        if ((input.size() & 1U) != 0U)
        {
            state_one.initialize(input[--position]);
            state_two.initialize(input[--position]);
            state_one.encode(bits, input[--position]);
        }
        else
        {
            state_two.initialize(input[--position]);
            state_one.initialize(input[--position]);
        }
        auto remaining{input.size() - 2U};
        if ((remaining & 2U) != 0U)
        {
            state_two.encode(bits, input[--position]);
            state_one.encode(bits, input[--position]);
        }
        while (position != 0U)
        {
            state_two.encode(bits, input[--position]);
            state_one.encode(bits, input[--position]);
            if (position != 0U)
            {
                state_two.encode(bits, input[--position]);
                state_one.encode(bits, input[--position]);
            }
        }
        state_two.flush(bits);
        state_one.flush(bits);
        bits.finish_into(output);
    }

    [[nodiscard]] inline auto compress_fse_bytes(std::span<std::uint8_t const> input,
        fse_compression_table const& table) -> std::vector<std::uint8_t>
    {
        forward_bit_writer bits;
        std::vector<std::uint8_t> output;
        compress_fse_bytes(input, table, bits, output);
        return output;
    }

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

    [[nodiscard]] inline auto literal_length_symbol(std::size_t length) -> std::uint8_t
    {
        static constexpr std::array<std::uint8_t, 64> codes{
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
            16, 16, 17, 17, 18, 18, 19, 19,
            20, 20, 20, 20, 21, 21, 21, 21,
            22, 22, 22, 22, 22, 22, 22, 22,
            23, 23, 23, 23, 23, 23, 23, 23,
            24, 24, 24, 24, 24, 24, 24, 24,
            24, 24, 24, 24, 24, 24, 24, 24
        };
        if (length < codes.size())
        {
            return codes[length];
        }
        auto const code{static_cast<unsigned>(std::bit_width(length)) - 1U + 19U};
        if (code >= literal_length_base.size())
        {
            throw entropy_error{"literal length cannot be represented by Zstandard"};
        }
        return static_cast<std::uint8_t>(code);
    }

    [[nodiscard]] inline auto match_length_symbol(std::size_t length) -> std::uint8_t
    {
        static constexpr std::array<std::uint8_t, 128> codes{
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
            32, 32, 33, 33, 34, 34, 35, 35, 36, 36, 36, 36, 37, 37, 37, 37,
            38, 38, 38, 38, 38, 38, 38, 38, 39, 39, 39, 39, 39, 39, 39, 39,
            40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40,
            41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41,
            42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
            42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42
        };
        if (length < 3U)
        {
            throw entropy_error{"match length is below the Zstandard minimum"};
        }
        auto const base{length - 3U};
        if (base < codes.size())
        {
            return codes[base];
        }
        auto const code{static_cast<unsigned>(std::bit_width(base)) - 1U + 36U};
        if (code >= match_length_base.size())
        {
            throw entropy_error{"match length cannot be represented by Zstandard"};
        }
        return static_cast<std::uint8_t>(code);
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

    [[nodiscard]] constexpr auto raw_literals_size(std::size_t literal_size) noexcept
        -> std::size_t
    {
        return literal_size + (literal_size < 32U ? 1U : literal_size < 4096U ? 2U : 3U);
    }

    inline void append_sequence_count(std::vector<std::uint8_t>& output, std::size_t count)
    {
        if (count < 128U)
        {
            output.push_back(static_cast<std::uint8_t>(count));
        }
        else if (count < 0x7F00U)
        {
            output.push_back(static_cast<std::uint8_t>((count >> 8U) + 0x80U));
            output.push_back(static_cast<std::uint8_t>(count));
        }
        else
        {
            auto const encoded{count - 0x7F00U};
            output.push_back(0xFFU);
            output.push_back(static_cast<std::uint8_t>(encoded));
            output.push_back(static_cast<std::uint8_t>(encoded >> 8U));
        }
    }

    struct huffman_code
    {
        std::uint16_t value{};
        std::uint8_t number_bits{};
    };

    // Mirrors the storage contract of ZSTD_match_t and ZSTD_optimal_t.  The
    // optimal parser reuses these tables for each 4 KiB shortest-path window;
    // keeping them in the compressor workspace means its hot loop never grows
    // a vector or allocates after construction.
    using optimal_match = binary_tree_match_candidate;

    struct optimal_path
    {
        std::int32_t price{};
        std::uint32_t offset_base{};
        std::uint32_t match_length{};
        std::uint32_t literal_length{};
        std::array<std::uint32_t, 3> repeat_offsets{};
    };

    static_assert(sizeof(optimal_match) == 8U);
    static_assert(sizeof(optimal_path) == 28U);

    struct optimal_parse_workspace
    {
        static constexpr std::size_t path_window{std::size_t{1} << 12U};
        static constexpr std::size_t table_size{path_window + 3U};

        void reserve()
        {
            matches.resize(table_size);
            paths.resize(table_size);
        }

        std::vector<optimal_match> matches;
        std::vector<optimal_path> paths;
        std::array<std::uint32_t, 256> literal_frequencies{};
        std::array<std::uint32_t, 36> literal_length_frequencies{};
        std::array<std::uint32_t, 53> match_length_frequencies{};
        std::array<std::uint32_t, 32> offset_frequencies{};
        // Statistics change only between path solves. Cache symbol-price
        // contributions when refreshing base prices, not for every candidate length.
        std::array<std::uint32_t, 53> match_length_prices{};
        std::array<std::uint32_t, 32> offset_prices{};
        std::uint32_t literal_sum{};
        std::uint32_t literal_length_sum{};
        std::uint32_t match_length_sum{};
        std::uint32_t offset_sum{};
        std::uint32_t literal_base_price{};
        std::uint32_t literal_length_base_price{};
        std::uint32_t match_length_base_price{};
        std::uint32_t offset_base_price{};
        bool initialized{};
        bool predefined_prices{};
        std::array<std::uint32_t, 3> repeat_offsets{1U, 4U, 8U};
    };

    inline constexpr std::uint32_t optimal_bitcost_multiplier{256U};

    [[nodiscard]] constexpr auto optimal_bit_weight(std::uint32_t frequency) noexcept -> std::uint32_t
    {
        // ZSTD_bitWeight(stat) is highbit(stat + 1), not bit_width(stat).
        auto const width{static_cast<std::uint32_t>(std::bit_width(frequency + 1U))};
        return (width - 1U) * optimal_bitcost_multiplier;
    }

    [[nodiscard]] constexpr auto optimal_fractional_weight(std::uint32_t frequency) noexcept -> std::uint32_t
    {
        auto const stat{frequency + 1U};
        auto const high{static_cast<unsigned>(std::bit_width(stat) - 1)};
        return high * optimal_bitcost_multiplier + ((stat << 8U) >> high);
    }

    [[nodiscard]] constexpr auto optimal_weight(std::uint32_t frequency,
        bool accurate) noexcept -> std::uint32_t
    {
        return accurate ? optimal_fractional_weight(frequency) : optimal_bit_weight(frequency);
    }

    inline void set_optimal_base_prices(optimal_parse_workspace& workspace, bool accurate) noexcept
    {
        workspace.literal_base_price = optimal_weight(workspace.literal_sum, accurate);
        workspace.literal_length_base_price = optimal_weight(workspace.literal_length_sum, accurate);
        workspace.match_length_base_price = optimal_weight(workspace.match_length_sum, accurate);
        workspace.offset_base_price = optimal_weight(workspace.offset_sum, accurate);
        for (std::size_t index{}; index < workspace.match_length_prices.size(); ++index)
            workspace.match_length_prices[index] = match_length_bits[index] * optimal_bitcost_multiplier +
                workspace.match_length_base_price - optimal_weight(workspace.match_length_frequencies[index], accurate);
        for (std::size_t index{}; index < workspace.offset_prices.size(); ++index)
            workspace.offset_prices[index] = static_cast<std::uint32_t>(index) * optimal_bitcost_multiplier +
                workspace.offset_base_price - optimal_weight(workspace.offset_frequencies[index], accurate);
    }

    template <std::size_t Size>
    [[nodiscard]] inline auto scale_optimal_statistics(
        std::array<std::uint32_t, Size>& frequencies,
        std::uint32_t sum, unsigned target_log) noexcept -> std::uint32_t
    {
        auto const factor{sum >> target_log};
        if (factor <= 1U) return sum;
        auto const shift{std::bit_width(factor) - 1};
        std::uint32_t scaled_sum{};
        for (auto& frequency : frequencies)
        {
            frequency = 1U + (frequency >> shift);
            scaled_sum += frequency;
        }
        return scaled_sum;
    }

    inline void initialize_optimal_prices(optimal_parse_workspace& workspace,
        std::span<std::uint8_t const> input, bool accurate) noexcept
    {
        static constexpr std::array<std::uint32_t, 36> base_literal_lengths{
            4U, 2U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U,
            1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U,
            1U, 1U, 1U, 1U};
        static constexpr std::array<std::uint32_t, 32> base_offsets{
            6U, 2U, 1U, 1U, 2U, 3U, 4U, 4U, 4U, 3U, 2U, 1U, 1U, 1U, 1U, 1U,
            1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U};
        workspace.predefined_prices = input.size() <= 8U;
        if (workspace.initialized)
        {
            workspace.literal_sum = scale_optimal_statistics(
                workspace.literal_frequencies, workspace.literal_sum, 12U);
            workspace.literal_length_sum = scale_optimal_statistics(
                workspace.literal_length_frequencies, workspace.literal_length_sum, 11U);
            workspace.match_length_sum = scale_optimal_statistics(
                workspace.match_length_frequencies, workspace.match_length_sum, 11U);
            workspace.offset_sum = scale_optimal_statistics(
                workspace.offset_frequencies, workspace.offset_sum, 11U);
            set_optimal_base_prices(workspace, accurate);
            return;
        }
        workspace.literal_frequencies.fill(0U);
        for (auto const value : input) ++workspace.literal_frequencies[value];
        workspace.literal_sum = 0U;
        for (auto& frequency : workspace.literal_frequencies)
        {
            frequency = (frequency == 0U ? 0U : 1U) + (frequency >> 8U);
            workspace.literal_sum += frequency;
        }
        workspace.literal_length_frequencies = base_literal_lengths;
        workspace.offset_frequencies = base_offsets;
        workspace.match_length_frequencies.fill(1U);
        workspace.literal_length_sum = std::accumulate(base_literal_lengths.begin(),
            base_literal_lengths.end(), std::uint32_t{});
        workspace.offset_sum = std::accumulate(base_offsets.begin(), base_offsets.end(), std::uint32_t{});
        workspace.match_length_sum = static_cast<std::uint32_t>(workspace.match_length_frequencies.size());
        workspace.initialized = true;
        set_optimal_base_prices(workspace, accurate);
    }

    [[nodiscard]] inline auto optimal_literal_cost(std::span<std::uint8_t const> literals,
        optimal_parse_workspace const& workspace, bool accurate) noexcept -> std::uint32_t
    {
        if (literals.empty()) return 0U;
        if (workspace.predefined_prices)
            return static_cast<std::uint32_t>(literals.size() * 6U * optimal_bitcost_multiplier);
        auto price{workspace.literal_base_price * static_cast<std::uint32_t>(literals.size())};
        auto const maximum{workspace.literal_base_price - optimal_bitcost_multiplier};
        for (auto const literal : literals)
            price -= std::min(optimal_weight(workspace.literal_frequencies[literal], accurate), maximum);
        return price;
    }

    [[nodiscard]] inline auto optimal_literal_length_price(std::size_t length,
        optimal_parse_workspace const& workspace, bool accurate) -> std::uint32_t
    {
        if (workspace.predefined_prices)
            return optimal_weight(static_cast<std::uint32_t>(length), accurate);
        auto const code{literal_length_symbol(length)};
        return literal_length_bits[code] * optimal_bitcost_multiplier +
            workspace.literal_length_base_price -
            optimal_weight(workspace.literal_length_frequencies[code], accurate);
    }

    [[nodiscard]] inline auto optimal_match_price(std::uint32_t offset_base_value,
        std::size_t match_length, optimal_parse_workspace const& workspace,
        bool accurate) -> std::uint32_t
    {
        auto const offset_code{static_cast<unsigned>(std::bit_width(offset_base_value) - 1)};
        auto const match_code{match_length_symbol(match_length)};
        if (workspace.predefined_prices)
            return optimal_weight(static_cast<std::uint32_t>(match_length - 3U), accurate) +
                (16U + offset_code) * optimal_bitcost_multiplier;
        return workspace.offset_prices[offset_code] + workspace.match_length_prices[match_code] +
            optimal_bitcost_multiplier / 5U;
    }

    inline constexpr std::int32_t optimal_max_price{1 << 30};

    [[nodiscard]] constexpr auto resolve_optimal_offset(
        std::array<std::uint32_t, 3> const& repeat_offsets,
        std::uint32_t offset_base_value, bool no_literals) noexcept -> std::uint32_t
    {
        if (offset_base_value >= 4U) return offset_base_value - 3U;
        auto const repeat_code{offset_base_value - 1U + static_cast<std::uint32_t>(no_literals)};
        if (repeat_code == 0U) return repeat_offsets[0];
        if (repeat_code == 3U) return repeat_offsets[0] - 1U;
        return repeat_offsets[repeat_code];
    }

    constexpr void update_optimal_repeat_offsets(std::array<std::uint32_t, 3>& repeat_offsets,
        std::uint32_t offset_base_value, bool no_literals) noexcept
    {
        if (offset_base_value >= 4U)
        {
            repeat_offsets = {offset_base_value - 3U, repeat_offsets[0], repeat_offsets[1]};
            return;
        }
        auto const repeat_code{offset_base_value - 1U + static_cast<std::uint32_t>(no_literals)};
        if (repeat_code == 0U) return;
        auto const current_offset{resolve_optimal_offset(repeat_offsets, offset_base_value, no_literals)};
        if (repeat_code >= 2U) repeat_offsets[2] = repeat_offsets[1];
        repeat_offsets[1] = repeat_offsets[0];
        repeat_offsets[0] = current_offset;
    }

    // Parser history advances even for partitions emitted as raw/RLE. Decoder
    // history advances only for compressed partitions. The caller restores its
    // saved decoder history if entropy encoding falls back to raw/RLE.
    inline void reconcile_partition_repeats(std::span<parsed_sequence> sequences,
        std::array<std::uint32_t, 3>& encoder_repeats,
        std::array<std::uint32_t, 3>& decoder_repeats) noexcept
    {
        for (auto& sequence : sequences)
        {
            auto const original_base{sequence.repeat_code != 0U ?
                sequence.repeat_code : sequence.offset + 3U};
            auto emitted_base{original_base};
            auto const no_literals{sequence.literal_length == 0U};
            if (sequence.repeat_code != 0U)
            {
                auto const encoder_offset{resolve_optimal_offset(encoder_repeats,
                    original_base, no_literals)};
                auto const decoder_offset{resolve_optimal_offset(decoder_repeats,
                    original_base, no_literals)};
                if (encoder_offset != decoder_offset)
                {
                    sequence.offset = encoder_offset;
                    sequence.repeat_code = 0U;
                    emitted_base = encoder_offset + 3U;
                }
            }
            update_optimal_repeat_offsets(encoder_repeats, original_base, no_literals);
            update_optimal_repeat_offsets(decoder_repeats, emitted_base, no_literals);
        }
    }

    inline void update_optimal_statistics(optimal_parse_workspace& workspace,
        std::span<std::uint8_t const> input, std::size_t literal_position,
        std::span<parsed_sequence const> sequences, bool accurate) noexcept
    {
        if (workspace.predefined_prices) return;
        for (auto const& sequence : sequences)
        {
            for (auto const literal : input.subspan(literal_position, sequence.literal_length))
                workspace.literal_frequencies[literal] += 2U;
            workspace.literal_sum += 2U * sequence.literal_length;

            auto const literal_code{literal_length_symbol(sequence.literal_length)};
            ++workspace.literal_length_frequencies[literal_code];
            ++workspace.literal_length_sum;

            auto const offset_base_value{sequence.repeat_code != 0U ?
                static_cast<std::uint32_t>(sequence.repeat_code) : sequence.offset + 3U};
            auto const offset_code{static_cast<std::size_t>(std::bit_width(offset_base_value)) - 1U};
            ++workspace.offset_frequencies[offset_code];
            ++workspace.offset_sum;

            auto const match_code{match_length_symbol(sequence.match_length)};
            ++workspace.match_length_frequencies[match_code];
            ++workspace.match_length_sum;
            literal_position += sequence.literal_length + sequence.match_length;
        }
        set_optimal_base_prices(workspace, accurate);
    }

    inline void reset_optimal_paths(optimal_parse_workspace& workspace) noexcept
    {
        for (auto& path : workspace.paths)
        {
            path.price = optimal_max_price;
            path.match_length = 0U;
            path.literal_length = 0U;
        }
    }

    [[nodiscard]] inline auto relax_optimal_literal(optimal_parse_workspace& workspace,
        std::size_t position, std::int32_t additional_price) noexcept -> bool
    {
        if (position + 1U >= workspace.paths.size() ||
            workspace.paths[position].price == optimal_max_price)
            return false;
        auto const candidate{workspace.paths[position].price + additional_price};
        auto& next{workspace.paths[position + 1U]};
        if (candidate > next.price) return false;
        next = workspace.paths[position];
        ++next.literal_length;
        next.price = candidate;
        return true;
    }

    template <bool Checked = true>
    [[nodiscard]] inline auto relax_optimal_match(optimal_parse_workspace& workspace,
        std::size_t position, std::size_t match_length, std::uint32_t offset_base_value,
        std::int32_t additional_price) noexcept -> bool
    {
        if constexpr (Checked)
        {
            if (match_length == 0U || position + match_length >= workspace.paths.size() ||
                workspace.paths[position].price == optimal_max_price)
                return false;
        }
        else
        {
            // The production parser clips each candidate to window_size-local
            // and enters relaxation only from a reachable, initialized path.
            assert(match_length != 0U && position + match_length < workspace.paths.size());
            assert(workspace.paths[position].price != optimal_max_price);
        }
        auto const candidate{workspace.paths[position].price + additional_price};
        auto& next{workspace.paths[position + match_length]};
        if (candidate >= next.price) return false;
        next = workspace.paths[position];
        next.offset_base = offset_base_value;
        next.match_length = static_cast<std::uint32_t>(match_length);
        next.literal_length = 0U;
        update_optimal_repeat_offsets(next.repeat_offsets, offset_base_value,
            workspace.paths[position].literal_length == 0U);
        next.price = candidate;
        return true;
    }

    [[nodiscard]] inline auto reconstruct_optimal_sequences(optimal_parse_workspace const& workspace,
        std::size_t end, std::vector<parsed_sequence>& sequences) -> std::size_t
    {
        sequences.clear();
        if (end >= workspace.paths.size()) return 0U;
        auto cursor{end};
        auto trailing_literals{workspace.paths[cursor].literal_length};
        while (workspace.paths[cursor].match_length != 0U)
        {
            auto const& stretch{workspace.paths[cursor]};
            if (stretch.literal_length + stretch.match_length > cursor || stretch.offset_base == 0U)
            {
                sequences.clear();
                return 0U;
            }
            auto const match_end{cursor - stretch.literal_length};
            auto const match_begin{match_end - stretch.match_length};
            auto const repeat_code{stretch.offset_base < 4U ?
                static_cast<std::uint8_t>(stretch.offset_base) : std::uint8_t{}};
            sequences.push_back(parsed_sequence{workspace.paths[match_begin].literal_length,
                stretch.match_length, repeat_code == 0U ? stretch.offset_base - 3U : 0U, repeat_code});
            cursor = match_begin;
        }
        std::reverse(sequences.begin(), sequences.end());
        return trailing_literals;
    }

    [[nodiscard]] inline auto solve_optimal_path(optimal_parse_workspace& workspace,
        std::span<std::uint8_t const> input,
        std::span<std::span<binary_tree_match_candidate const> const> candidates,
        std::vector<parsed_sequence>& sequences, bool accurate) -> std::size_t
    {
        auto const size{std::min({input.size(), candidates.size(),
            optimal_parse_workspace::path_window})};
        reset_optimal_paths(workspace);
        workspace.paths[0] = {static_cast<std::int32_t>(optimal_literal_length_price(0U,
            workspace, accurate)), 0U, 0U, 0U, {1U, 4U, 8U}};
        for (std::size_t position{}; position < size; ++position)
        {
            auto const& current{workspace.paths[position]};
            if (current.price == optimal_max_price) continue;
            auto const next_length{static_cast<std::size_t>(current.literal_length) + 1U};
            auto const literal_increment{static_cast<std::int32_t>(optimal_literal_cost(
                input.subspan(position, 1U), workspace, accurate)) +
                static_cast<std::int32_t>(optimal_literal_length_price(next_length, workspace, accurate)) -
                static_cast<std::int32_t>(optimal_literal_length_price(current.literal_length, workspace, accurate))};
            (void)relax_optimal_literal(workspace, position, literal_increment);
            for (auto const candidate : candidates[position])
            {
                if (candidate.length < 3U || position + candidate.length > size) continue;
                auto const match_price{static_cast<std::int32_t>(optimal_match_price(
                    candidate.offset_base, candidate.length, workspace, accurate)) +
                    static_cast<std::int32_t>(optimal_literal_length_price(0U, workspace, accurate)) -
                    static_cast<std::int32_t>(optimal_literal_length_price(
                        current.literal_length, workspace, accurate))};
                (void)relax_optimal_match(workspace, position, candidate.length,
                    candidate.offset_base, match_price);
            }
        }
        return reconstruct_optimal_sequences(workspace, size, sequences);
    }

    template <bool AlignedLiterals>
    struct basic_compression_workspace
    {
        void reserve(std::size_t block_size, std::size_t maximum_sequences)
        {
            auto const entropy_scratch_size{2U * block_size + 64U};
            literals.resize(block_size);
            raw_literals.reserve(entropy_scratch_size);
            literal_codes.resize(maximum_sequences);
            offset_codes.resize(maximum_sequences);
            match_codes.resize(maximum_sequences);
            huffman_body.reserve(entropy_scratch_size);
            huffman_repeat_body.reserve(entropy_scratch_size);
            compressed_weights.reserve(512U);
            huffman_output.reserve(entropy_scratch_size);
            for (auto& description : table_descriptions)
            {
                description.reserve(512U);
            }
            weight_description.reserve(512U);
            weight_bitstream.reserve(512U);
            auto const stream_input_size{std::max(std::min(block_size, std::size_t{255U}),
                (block_size + huffman_streams.size() - 1U) / huffman_streams.size())};
            auto const stream_size{(stream_input_size * 11U + 7U) / 8U + 8U};
            for (auto& stream : huffman_streams)
            {
                stream.reserve(stream_size);
            }
            optimal_sequences.reserve(maximum_sequences);
        }

        using literal_allocator = std::conditional_t<AlignedLiterals,
            cache_aligned_allocator<std::uint8_t>, std::allocator<std::uint8_t>>;
        std::vector<std::uint8_t, literal_allocator> literals;
        std::vector<std::uint8_t> raw_literals;
        std::vector<std::uint8_t> literal_codes;
        std::vector<std::uint8_t> offset_codes;
        std::vector<std::uint8_t> match_codes;
        forward_bit_writer sequence_bits;
        wide_sequence_bit_writer wide_sequence_bits;
        std::vector<std::uint8_t> huffman_body;
        std::vector<std::uint8_t> huffman_repeat_body;
        std::vector<std::uint8_t> compressed_weights;
        std::vector<std::uint8_t> huffman_output;
        std::array<std::vector<std::uint8_t>, 3> table_descriptions;
        forward_bit_writer weight_bits;
        std::vector<std::uint8_t> weight_description;
        std::vector<std::uint8_t> weight_bitstream;
        std::array<std::vector<std::uint8_t>, 4> huffman_streams;
        std::array<fse_compression_table, 3> sequence_tables;
        std::array<sequence_table_history, 3> sequence_histories;
        fse_compression_table huffman_weight_table;
        normalized_counts huffman_weight_counts;
        bool huffman_weight_table_valid{};
        std::array<std::uint32_t, 256> huffman_tree_frequencies{};
        std::array<std::uint8_t, 256> huffman_tree_lengths{};
        std::size_t huffman_tree_literal_size{};
        std::uint8_t huffman_tree_maximum_symbol{};
        unsigned huffman_tree_log{};
        bool huffman_tree_valid{};
        bool huffman_tree_optimal_depth{};
        std::array<std::uint32_t, 256> huffman_repeat_frequencies{};
        std::array<std::uint8_t, 256> huffman_repeat_lengths{};
        std::uint8_t huffman_repeat_maximum_symbol{};
        std::size_t huffman_repeat_literal_size{};
        unsigned huffman_repeat_log{};
        bool huffman_repeat_valid{};
        std::array<huffman_code, 256> huffman_repeat_codes{};
        std::array<std::uint8_t, 256> huffman_repeat_code_lengths{};
        std::uint8_t huffman_repeat_codes_maximum_symbol{};
        unsigned huffman_repeat_codes_log{};
        bool huffman_repeat_codes_valid{};
        fast_match_scratch fast_latest;
        optimal_parse_workspace optimal;
        std::vector<parsed_sequence> optimal_sequences;
        symbol_count_workspace histogram_workspace;

        void reset_entropy_history() noexcept
        {
            for (auto& history : sequence_histories)
            {
                history.valid = false;
            }
            huffman_repeat_valid = false;
            huffman_repeat_codes_valid = false;
        }
    };

    using compression_workspace = basic_compression_workspace<false>;

    // The optimal strategies use the same 4 KiB bounded forward shortest-path
    // search as zstd's btopt/btultra parsers.  Match candidates are consumed
    // immediately: `matches` is one reusable per-position scratch row, while
    // `paths` stores the dynamic-programming frontier.  This keeps the parser
    // allocation-free after compressor construction.
    template <typename Matcher, bool AlignedLiterals>
    [[nodiscard]] inline auto parse_optimal_block(Matcher& matcher,
        std::span<std::uint8_t const> input, std::size_t block_begin,
        std::size_t block_size, basic_compression_workspace<AlignedLiterals>& workspace,
        parsed_block result, bool accurate, std::size_t sufficient_length) -> parsed_block
    {
        result.sequences.clear();
        result.input_begin = block_begin;
        result.trailing_literal_position = block_begin;
        result.trailing_literal_length = block_size;
        if (block_size < 8U || block_begin > input.size() || block_size > input.size() - block_begin)
            return result;

        auto const block_end{block_begin + block_size};
        auto const parse_end{block_end - 8U};
        auto& optimal{workspace.optimal};
        auto const minimum_match{matcher.optimal_minimum_match()};
        if constexpr (requires { matcher.begin_optimal_block(block_begin, block_end); })
            matcher.begin_optimal_block(block_begin, block_end);
        else
            matcher.begin_optimal_block(block_begin);
        initialize_optimal_prices(optimal, input.subspan(block_begin, block_size), accurate);
        auto anchor{block_begin};
        auto position{block_begin};
#ifdef SPH_ZSTDPP_TRACE_OPTIMAL
        std::fprintf(stderr, "B %zu\n", block_size);
        std::fprintf(stderr, "F %u %u %u %u R %u %u %u\n",
            optimal.literal_sum, optimal.literal_length_sum, optimal.match_length_sum,
            optimal.offset_sum, optimal.repeat_offsets[0], optimal.repeat_offsets[1],
            optimal.repeat_offsets[2]);
#endif

        while (position < parse_end)
        {
            // Match endpoints can reach the block end; only the starting
            // position of a new hash lookup needs eight readable bytes.
            auto const window_size{std::min(optimal_parse_workspace::path_window, block_end - position)};
            auto const initial_literals{position - anchor};
            auto const first_no_literals{initial_literals == 0U};
            auto const first_candidates{matcher.collect_optimal_candidates(input,
                position, block_end, workspace, optimal.repeat_offsets, first_no_literals, sufficient_length)};
#ifdef SPH_ZSTDPP_TRACE_OPTIMAL
            if (position - block_begin < 16U)
            {
                std::fprintf(stderr, "C %zu", position - block_begin);
                for (auto const candidate : first_candidates)
                    std::fprintf(stderr, " %u:%u", candidate.offset_base, candidate.length);
                std::fputc('\n', stderr);
            }
#endif
            if (first_candidates.empty())
            {
                ++position;
                continue;
            }

            // Frequencies do not change until this path series is emitted.
            auto const zero_literal_price{static_cast<std::int32_t>(
                optimal_literal_length_price(0U, optimal, accurate))};
            auto const one_literal_increment{accurate ? static_cast<std::int32_t>(
                optimal_literal_length_price(1U, optimal, accurate)) - zero_literal_price : 0};
            auto selected_size{std::size_t{1U}};
            std::size_t frontier_end{};
            bool direct_match{};
            std::size_t direct_local{};
            std::size_t direct_length{};
            std::uint32_t direct_offset_base{};
            // Clear each newly reachable entry once per series. Unreached
            // entries may retain old paths, but must never enter a relaxation.
            std::size_t initialized_end{};
            auto initialize_through = [&](std::size_t end)
            {
                while (initialized_end < end)
                {
                    auto& path{optimal.paths[++initialized_end]};
                    path.price = optimal_max_price;
                    path.match_length = 0U;
                    path.literal_length = 0U;
                }
            };
            optimal.paths[0] = {static_cast<std::int32_t>(optimal_literal_length_price(
                initial_literals, optimal, accurate)), 0U, 0U,
                static_cast<std::uint32_t>(initial_literals), optimal.repeat_offsets};

            for (std::size_t local{};
                local < window_size && (local == 0U || local <= frontier_end); ++local)
            {
                auto const& current{optimal.paths[local]};
                if (current.price == optimal_max_price) continue;
                initialize_through(local + 1U);
                auto const absolute_position{position + local};
                auto const next_length{static_cast<std::size_t>(current.literal_length) + 1U};
                auto const literal_increment{static_cast<std::int32_t>(optimal_literal_cost(
                    input.subspan(absolute_position, 1U), optimal, accurate)) +
                    static_cast<std::int32_t>(optimal_literal_length_price(next_length, optimal, accurate)) -
                    static_cast<std::int32_t>(optimal_literal_length_price(current.literal_length, optimal, accurate))};
                auto const previous_match{optimal.paths[local + 1U]};
                auto const replaced{relax_optimal_literal(optimal, local, literal_increment)};
                // Ultra preserves an overwritten match when extending it by
                // one literal is cheaper than extending the replacing run.
                // Its repeat history was already updated by match relaxation.
                if (accurate && replaced && previous_match.literal_length == 0U &&
                    previous_match.match_length != 0U && local + 2U <= window_size)
                {
                    if (one_literal_increment < 0)
                    {
                        initialize_through(local + 2U);
                        auto const next_literal_cost{static_cast<std::int32_t>(optimal_literal_cost(
                            input.subspan(absolute_position + 1U, 1U), optimal, accurate))};
                        auto const with_one_literal{previous_match.price + next_literal_cost + one_literal_increment};
                        auto const with_more_literals{optimal.paths[local + 1U].price + next_literal_cost +
                            static_cast<std::int32_t>(optimal_literal_length_price(next_length + 1U, optimal, accurate)) -
                            static_cast<std::int32_t>(optimal_literal_length_price(next_length, optimal, accurate))};
                        if (with_one_literal < with_more_literals &&
                            with_one_literal < optimal.paths[local + 2U].price)
                        {
                            auto& next{optimal.paths[local + 2U]};
                            next = previous_match;
                            next.literal_length = 1U;
                            next.price = with_one_literal;
                            frontier_end = std::max(frontier_end, local + 2U);
                            selected_size = frontier_end;
                        }
                    }
                }

                // Reference btopt terminates this shortest-path series at the
                // current frontier. It may relax the trailing literal, but it
                // never starts another tree search from that endpoint.
                if (local != 0U && local == frontier_end) break;
                if (absolute_position > parse_end) continue;

                // Reference btopt's optLevel 0 path skips an expensive match
                // lookup when advancing one literal already reaches a nearly
                // equivalent (or better) price.  Ultra keeps every lookup.
                if (local != 0U && !accurate && local + 1U < window_size &&
                    optimal.paths[local + 1U].price <= current.price +
                        static_cast<std::int32_t>(optimal_bitcost_multiplier / 2U))
                {
                    continue;
                }
                std::size_t longest_candidate{minimum_match - 1U};
                auto const no_literals{current.literal_length == 0U};
                auto const candidates{local == 0U ? first_candidates :
                    matcher.collect_optimal_candidates(input, absolute_position,
                        block_end, workspace, current.repeat_offsets, no_literals, sufficient_length)};
                // Candidates increase in length. Reference btopt commits the
                // longest sufficient match, not the first one above the threshold.
                if (!candidates.empty() && (candidates.back().length > sufficient_length ||
                    (local != 0U && (local + candidates.back().length >= optimal_parse_workspace::path_window ||
                        absolute_position + candidates.back().length == block_end))))
                {
                    direct_match = true;
                    direct_local = local;
                    direct_length = candidates.back().length;
                    direct_offset_base = candidates.back().offset_base;
                    break;
                }
                for (auto const candidate : candidates)
                {
                    auto const match_length{std::min<std::size_t>(candidate.length, window_size - local)};
                    if (match_length <= longest_candidate) continue;
                    initialize_through(local + match_length);
                    auto const first_length{longest_candidate + 1U};
                    longest_candidate = match_length;
                    frontier_end = std::max(frontier_end, local + match_length);
                    selected_size = frontier_end;
                    for (auto length{match_length};; --length)
                    {
                        auto const match_increment{static_cast<std::int32_t>(optimal_match_price(
                            candidate.offset_base, length, optimal, accurate)) +
                            zero_literal_price};
                        auto const improved{relax_optimal_match<false>(optimal, local, length,
                            candidate.offset_base, match_increment)};
                        if ((!accurate && !improved) || length == first_length) break;
                    }
                }
            }

            auto& selected{workspace.optimal_sequences};
            if (direct_match)
            {
                auto const literal_length{reconstruct_optimal_sequences(
                    optimal, direct_local, selected)};
                auto const repeat_code{direct_offset_base < 4U ?
                    static_cast<std::uint8_t>(direct_offset_base) : std::uint8_t{}};
                selected.push_back(parsed_sequence{literal_length, direct_length,
                    repeat_code == 0U ? direct_offset_base - 3U : 0U, repeat_code});
                optimal.repeat_offsets = optimal.paths[direct_local].repeat_offsets;
                update_optimal_repeat_offsets(optimal.repeat_offsets, direct_offset_base,
                    optimal.paths[direct_local].literal_length == 0U);
#ifdef SPH_ZSTDPP_TRACE_OPTIMAL
                for (auto const& sequence : selected)
                {
                    auto const offset_base_value{sequence.repeat_code != 0U ?
                        static_cast<std::uint32_t>(sequence.repeat_code) : sequence.offset + 3U};
                    std::fprintf(stderr, "S %u %u %u\n", sequence.literal_length,
                        sequence.match_length, offset_base_value);
                }
#endif
                result.sequences.insert(result.sequences.end(), selected.begin(), selected.end());
                update_optimal_statistics(optimal, input, anchor, selected, accurate);
                position += direct_local + direct_length;
                anchor = position;
                continue;
            }
            auto const trailing{reconstruct_optimal_sequences(optimal, selected_size, selected)};
            optimal.repeat_offsets = optimal.paths[selected_size].repeat_offsets;
            if (!selected.empty())
            {
#ifdef SPH_ZSTDPP_TRACE_OPTIMAL
                for (auto const& sequence : selected)
                {
                    auto const offset_base_value{sequence.repeat_code != 0U ?
                        static_cast<std::uint32_t>(sequence.repeat_code) : sequence.offset + 3U};
                    std::fprintf(stderr, "S %u %u %u\n", sequence.literal_length,
                        sequence.match_length, offset_base_value);
                }
#endif
                result.sequences.insert(result.sequences.end(), selected.begin(), selected.end());
                update_optimal_statistics(optimal, input, anchor, selected, accurate);
                anchor = position + selected_size - trailing;
            }
            position += selected_size;
        }
        result.trailing_literal_position = anchor;
        result.trailing_literal_length = block_end - anchor;
#ifdef SPH_ZSTDPP_TRACE_OPTIMAL
        std::fprintf(stderr, "E %zu\n", result.trailing_literal_length);
#endif
        return result;
    }

    enum class sequence_table_mode : std::uint8_t
    {
        predefined = 0U,
        run_length = 1U,
        compressed = 2U,
        repeat = 3U
    };

    struct selected_sequence_table
    {
        sequence_table_mode mode{};
        fse_compression_table const* table{};
    };

    inline constexpr std::array<unsigned, 256> inverse_probability_log256{
        0,2048,1792,1642,1536,1453,1386,1329,1280,1236,1197,1162,1130,1100,1073,1047,
        1024,1001,980,960,941,923,906,889,874,859,844,830,817,804,791,779,
        768,756,745,734,724,714,704,694,685,676,667,658,650,642,633,626,
        618,610,603,595,588,581,574,567,561,554,548,542,535,529,523,517,
        512,506,500,495,489,484,478,473,468,463,458,453,448,443,438,434,
        429,424,420,415,411,407,402,398,394,390,386,382,377,373,370,366,
        362,358,354,350,347,343,339,336,332,329,325,322,318,315,311,308,
        305,302,298,295,292,289,286,282,279,276,273,270,267,264,261,258,
        256,253,250,247,244,241,239,236,233,230,228,225,222,220,217,215,
        212,209,207,204,202,199,197,194,192,190,187,185,182,180,178,175,
        173,171,168,166,164,162,159,157,155,153,151,149,146,144,142,140,
        138,136,134,132,130,128,126,123,121,119,117,115,114,112,110,108,
        106,104,102,100,98,96,94,93,91,89,87,85,83,82,80,78,
        76,74,73,71,69,67,66,64,62,61,59,57,55,54,52,50,
        49,47,46,44,42,41,39,37,36,34,33,31,30,28,26,25,
        23,22,20,19,17,16,14,13,11,10,8,7,5,4,2,1};

    // Reference zstd's entropy estimate, expressed in the same fixed-point units
    // (1/256 bit) used by its inverse-probability table. This is setup-time work,
    // not part of the sequence writer hot loop.
    [[nodiscard]] inline auto entropy_cost(std::span<std::uint32_t const> counts,
        unsigned maximum_symbol, std::size_t total) -> std::size_t
    {
        std::size_t cost{};
        for (unsigned symbol{}; symbol <= maximum_symbol; ++symbol)
        {
            auto const count{counts[symbol]};
            if (count == 0U)
            {
                continue;
            }
            auto probability{static_cast<unsigned>((256U * count) / total)};
            if (probability == 0U)
            {
                probability = 1U;
            }
            auto const inverse{static_cast<std::size_t>(inverse_probability_log256[probability])};
            cost += static_cast<std::size_t>(count) * inverse;
        }
        return cost >> 8U;
    }

    [[nodiscard]] inline auto cross_entropy_cost(std::span<std::int16_t const> norm,
        unsigned accuracy_log, std::span<std::uint32_t const> counts,
        unsigned maximum_symbol) -> std::size_t
    {
        std::size_t cost{};
        auto const shift{8U - accuracy_log};
        for (unsigned symbol{}; symbol <= maximum_symbol; ++symbol)
        {
            auto const normalized{norm[symbol] == -1 ? 1U :
                static_cast<unsigned>(norm[symbol])};
            auto const probability{normalized << shift};
            auto const inverse{static_cast<std::size_t>(inverse_probability_log256[probability])};
            cost += static_cast<std::size_t>(counts[symbol]) * inverse;
        }
        return cost >> 8U;
    }

    [[nodiscard]] inline auto repeat_fse_cost(fse_compression_table const& previous_table,
        normalized_counts const& previous,
        std::span<std::uint32_t const> counts, unsigned maximum_symbol) -> std::optional<std::size_t>
    {
        if (maximum_symbol > previous.maximum_symbol)
        {
            return std::nullopt;
        }
        auto const table_size{std::uint32_t{1} << previous.table_log};
        std::size_t cost{};
        for (unsigned symbol{}; symbol <= maximum_symbol; ++symbol)
        {
            if (counts[symbol] == 0U)
            {
                continue;
            }
            if (previous.values[symbol] == 0)
            {
                return std::nullopt;
            }
            auto const delta{previous_table.transforms[symbol].delta_number_bits};
            auto const minimum_bits{delta >> 16U};
            auto const threshold{(minimum_bits + 1U) << 16U};
            auto const delta_from_threshold{threshold - (delta + table_size)};
            auto const normalized_delta{(delta_from_threshold << 8U) >> previous.table_log};
            auto const bit_cost{((minimum_bits + 1U) << 8U) - normalized_delta};
            cost += static_cast<std::size_t>(counts[symbol]) * bit_cost;
        }
        return cost >> 8U;
    }

    // Reference ZSTD_estimateBlockSize_symbolType floors each symbol stream to
    // bytes separately. Actual FSE bitstream length is not the splitter's cost.
    [[nodiscard]] inline auto estimate_sequence_symbol_size(
        std::span<std::uint8_t const> codes, selected_sequence_table const& selected,
        normalized_counts const& selected_counts,
        std::span<std::int16_t const> default_counts, unsigned default_log,
        std::span<std::uint8_t const> additional_bits,
        symbol_count_workspace& histogram) -> std::size_t
    {
        if (codes.empty()) return 0U;
        auto const& counts{count_symbols(codes, histogram)};
        auto const maximum_symbol{*std::max_element(codes.begin(), codes.end())};
        std::size_t bits{};
        if (selected.mode == sequence_table_mode::predefined)
        {
            bits = cross_entropy_cost(default_counts, default_log, counts, maximum_symbol);
        }
        else if (selected.mode != sequence_table_mode::run_length)
        {
            auto const cost{repeat_fse_cost(*selected.table, selected_counts, counts, maximum_symbol)};
            if (!cost) return codes.size() * 10U;
            bits = *cost;
        }
        for (auto const code : codes)
            bits += additional_bits.empty() ? code : additional_bits[code];
        return bits >> 3U;
    }

    struct literal_table_costs
    {
        std::size_t new_body_size{};
        std::size_t description_size{};
        // Absent when no committed table can represent the current alphabet.
        std::optional<std::size_t> repeat_body_size;
    };

    struct estimated_literal_selection
    {
        sequence_table_mode mode{sequence_table_mode::predefined};
        std::size_t description_size{};
    };

    // Mirrors ZSTD_buildBlockEntropyStats_literals' decision rules. The builder
    // writes tentative tables only; selecting a partition must not commit them.
    template <typename BuildCosts>
    [[nodiscard]] inline auto select_estimated_literal_table(std::size_t literal_size,
        std::size_t largest_count, bool repeat_fully_valid, bool compression_disabled,
        BuildCosts&& build_costs) -> estimated_literal_selection
    {
        auto const minimum_size{repeat_fully_valid ? 6U : 63U};
        if (compression_disabled || literal_size <= minimum_size) return {};
        if (largest_count == literal_size) return {sequence_table_mode::run_length, 0U};
        if (largest_count <= (literal_size >> 7U) + 4U) return {};
        auto const costs{build_costs()};
        if (costs.repeat_body_size && *costs.repeat_body_size < literal_size &&
            (*costs.repeat_body_size <= costs.description_size + costs.new_body_size ||
                costs.description_size + 12U >= literal_size))
            return {sequence_table_mode::repeat, 0U};
        if (costs.new_body_size + costs.description_size >= literal_size) return {};
        return {sequence_table_mode::compressed, costs.description_size};
    }

    [[nodiscard]] inline auto estimate_literal_section_size(sequence_table_mode mode,
        std::size_t literal_size, std::span<std::uint32_t const> counts,
        std::span<huffman_code const> codes, std::size_t description_size,
        bool write_entropy) noexcept -> std::size_t
    {
        // These intentionally omit raw/RLE literal headers: the reference's
        // partition heuristic estimates them this way, unlike the real writer.
        if (mode == sequence_table_mode::predefined) return literal_size;
        if (mode == sequence_table_mode::run_length) return 1U;
        assert(codes.size() >= counts.size());
        std::size_t bits{};
        for (std::size_t symbol{}; symbol < counts.size(); ++symbol)
            bits += static_cast<std::size_t>(counts[symbol]) * codes[symbol].number_bits;
        auto const header_size{3U + static_cast<unsigned>(literal_size >= 1024U) +
            static_cast<unsigned>(literal_size >= 16384U)};
        return (bits >> 3U) + header_size + (literal_size >= 256U ? 6U : 0U) +
            (write_entropy ? description_size : 0U);
    }

    [[nodiscard]] constexpr auto estimate_sequence_section_size(std::size_t sequence_count,
        std::size_t literal_length_bytes, std::size_t offset_bytes,
        std::size_t match_length_bytes, std::size_t description_size,
        bool write_entropy) noexcept -> std::size_t
    {
        auto const header_size{2U + static_cast<unsigned>(sequence_count >= 128U) +
            static_cast<unsigned>(sequence_count >= 0x7F00U)};
        return literal_length_bytes + offset_bytes + match_length_bytes + header_size +
            (write_entropy ? description_size : 0U);
    }

    [[nodiscard]] inline auto make_default_normalized_counts(
        std::span<std::int16_t const> values,
        unsigned table_log) -> normalized_counts
    {
        normalized_counts result{};
        result.maximum_symbol = static_cast<unsigned>(values.size() - 1U);
        result.table_log = table_log;
        std::ranges::copy(values, result.values.begin());
        return result;
    }

    [[nodiscard]] inline auto default_fse_compression_table(
        std::span<std::int16_t const> counts) -> fse_compression_table const&
    {
        if (counts.data() == literal_length_default_norm.data())
        {
            static auto const table{build_fse_compression_table(
                make_default_normalized_counts(literal_length_default_norm, 6U))};
            return table;
        }
        if (counts.data() == offset_default_norm.data())
        {
            static auto const table{build_fse_compression_table(
                make_default_normalized_counts(offset_default_norm, 5U))};
            return table;
        }
        if (counts.data() == match_length_default_norm.data())
        {
            static auto const table{build_fse_compression_table(
                make_default_normalized_counts(match_length_default_norm, 6U))};
            return table;
        }
        throw entropy_error{"unknown predefined FSE compression table"};
    }

    template <bool CarefulSelection = false, bool DominantPrefix = false,
        std::size_t AlphabetSize = 256U>
    [[nodiscard]] inline auto select_sequence_table(
        std::span<std::uint8_t const> codes,
        std::span<std::int16_t const> default_counts,
        unsigned default_log,
        unsigned maximum_log,
        bool predefined_allowed,
        std::vector<std::uint8_t>& description,
        fse_compression_table& workspace_table,
        symbol_count_workspace& histogram_workspace,
        sequence_table_history& history) -> selected_sequence_table
    {
        description.clear();
        auto const& counts{DominantPrefix ? count_symbols_dominant_prefix(codes, histogram_workspace) :
            count_symbols<128U, AlphabetSize>(codes, histogram_workspace)};
        unsigned maximum_symbol{};
        for (unsigned symbol{}; symbol < AlphabetSize; ++symbol)
        {
            if (counts[symbol] != 0U)
            {
                maximum_symbol = symbol;
            }
        }
        auto const most_frequent{*std::max_element(
            counts.begin(), counts.begin() + static_cast<std::ptrdiff_t>(maximum_symbol + 1U))};
        if (most_frequent == codes.size())
        {
            history.valid = false;
            if (predefined_allowed && codes.size() <= 2U)
            {
                return {sequence_table_mode::predefined,
                    &default_fse_compression_table(default_counts)};
            }
            description.push_back(codes.front());
            workspace_table = make_run_length_fse_table(codes.front());
            return {sequence_table_mode::run_length, &workspace_table};
        }

        if (predefined_allowed && !CarefulSelection)
        {
            // ZSTD_fast's inexpensive table-choice heuristic.
            auto const dynamic_minimum{((std::size_t{1} << default_log) * 9U) >> 3U};
            if (codes.size() < dynamic_minimum ||
                most_frequent < (codes.size() >> (default_log - 1U)))
            {
                history.valid = false;
                return {sequence_table_mode::predefined,
                    &default_fse_compression_table(default_counts)};
            }
        }

        auto adjusted_size{codes.size()};
        if (counts[codes.back()] > 1U)
        {
            auto adjusted_counts{counts};
            --adjusted_counts[codes.back()];
            --adjusted_size;
            auto const table_log{optimal_fse_table_log(maximum_log, codes.size(), maximum_symbol)};
            auto const normalized{normalize_counts(adjusted_counts, adjusted_size, maximum_symbol,
                table_log, adjusted_size >= 2048U)};
            write_normalized_counts(normalized, description);
            if constexpr (CarefulSelection)
            {
                auto const basic_cost{cross_entropy_cost(default_counts, default_log,
                    counts, maximum_symbol)};
                auto const compressed_cost{entropy_cost(counts, maximum_symbol, codes.size()) +
                    description.size() * 8U};
                auto const repeat_cost{history.valid ? repeat_fse_cost(
                    history.table, history.counts, counts, maximum_symbol) : std::nullopt};
                if (basic_cost <= compressed_cost &&
                    (!repeat_cost || basic_cost <= *repeat_cost))
                {
                    description.clear();
                    history.valid = false;
                    return {sequence_table_mode::predefined,
                        &default_fse_compression_table(default_counts)};
                }
                if (codes.size() >= 1U && history.valid && repeat_cost &&
                    *repeat_cost <= compressed_cost)
                {
                    description.clear();
                    return {sequence_table_mode::repeat, &history.table};
                }
            }
            workspace_table = build_fse_compression_table(normalized);
            history.counts = normalized;
            history.table = workspace_table;
            history.valid = true;
            return {sequence_table_mode::compressed, &workspace_table};
        }
        auto const table_log{optimal_fse_table_log(maximum_log, codes.size(), maximum_symbol)};
        auto const normalized{normalize_counts(counts, adjusted_size, maximum_symbol,
            table_log, adjusted_size >= 2048U)};
        write_normalized_counts(normalized, description);
        if constexpr (CarefulSelection)
        {
            auto const basic_cost{cross_entropy_cost(default_counts, default_log,
                counts, maximum_symbol)};
            auto const compressed_cost{entropy_cost(counts, maximum_symbol, codes.size()) +
                description.size() * 8U};
            auto const repeat_cost{history.valid ? repeat_fse_cost(
                history.table, history.counts, counts, maximum_symbol) : std::nullopt};
            if (basic_cost <= compressed_cost &&
                (!repeat_cost || basic_cost <= *repeat_cost))
            {
                description.clear();
                history.valid = false;
                return {sequence_table_mode::predefined,
                    &default_fse_compression_table(default_counts)};
            }
            if (codes.size() >= 1U && history.valid && repeat_cost &&
                *repeat_cost <= compressed_cost)
            {
                description.clear();
                return {sequence_table_mode::repeat, &history.table};
            }
        }
        workspace_table = build_fse_compression_table(normalized);
        history.counts = normalized;
        history.table = workspace_table;
        history.valid = true;
        return {sequence_table_mode::compressed, &workspace_table};
    }

    template <bool AlignedLiterals, bool SelectBeforeEmit = false, bool RankCanonical = false,
        bool DeferredCodes = false>
    [[nodiscard]] inline auto encode_huffman_literals(std::span<std::uint8_t const> literals,
        basic_compression_workspace<AlignedLiterals>& workspace, std::vector<std::uint8_t>& output,
        bool optimal_depth = false, std::size_t minimum_gain = 0U) -> bool;

    // Keep the entropy/table selection shared when a block chooses its bit writer.
    template <bool Wide, bool Grouped, bool AlignedLiterals, typename ParsedView>
    inline void write_sequence_bitstream(ParsedView const& parsed,
        basic_compression_workspace<AlignedLiterals>& workspace,
        selected_sequence_table const& literal_table,
        selected_sequence_table const& offset_table,
        selected_sequence_table const& match_selection,
        std::vector<std::uint8_t>& output)
    {
        fse_compression_state literal_state{*literal_table.table};
        fse_compression_state offset_state{*offset_table.table};
        fse_compression_state match_state{*match_selection.table};
        auto const last_index{parsed.sequences.size() - 1U};
        auto const& last{parsed.sequences[last_index]};
        auto const last_ll{workspace.literal_codes[last_index]};
        auto const last_of{workspace.offset_codes[last_index]};
        auto const last_ml{workspace.match_codes[last_index]};
        auto const last_offset{last.repeat_code != 0U ?
            static_cast<std::uint32_t>(last.repeat_code) : last.offset + 3U};
        literal_state.initialize(last_ll);
        offset_state.initialize(last_of);
        match_state.initialize(last_ml);

        auto& bits{[&]() -> auto&
        {
            if constexpr (Wide) return workspace.wide_sequence_bits;
            else return workspace.sequence_bits;
        }()};
        bits.reset(output);
        bits.append(last.literal_length, literal_length_bits[last_ll]);
        bits.append(last.match_length - 3U, match_length_bits[last_ml]);
        bits.append(last_offset, last_of);
        for (std::size_t index{last_index}; index-- > 0U;)
        {
            auto const& sequence{parsed.sequences[index]};
            auto const ll{workspace.literal_codes[index]};
            auto const of{workspace.offset_codes[index]};
            auto const ml{workspace.match_codes[index]};
            if constexpr (Grouped)
            {
                auto const of_bits{offset_state.template encode_bits<true>(of)};
                auto const ml_bits{match_state.template encode_bits<true>(ml)};
                auto const ll_bits{literal_state.template encode_bits<true>(ll)};
                bits.append_unchecked(of_bits.value | (ml_bits.value << of_bits.count) |
                    (ll_bits.value << (of_bits.count + ml_bits.count)),
                    of_bits.count + ml_bits.count + ll_bits.count);
            }
            else
            {
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
                offset_state.encode_inlined(bits, of);
                match_state.encode_inlined(bits, ml);
                literal_state.encode_inlined(bits, ll);
#else
                offset_state.encode(bits, of);
                match_state.encode(bits, ml);
                literal_state.encode(bits, ll);
#endif
            }
            bits.append(sequence.literal_length, literal_length_bits[ll]);
            bits.append(sequence.match_length - 3U, match_length_bits[ml]);
            auto const encoded_offset{sequence.repeat_code != 0U ?
                static_cast<std::uint32_t>(sequence.repeat_code) : sequence.offset + 3U};
            bits.append(encoded_offset, of);
        }
        match_state.flush(bits);
        offset_state.flush(bits);
        literal_state.flush(bits);
        bits.finish_into(output);
    }

    template <bool CarefulSelection = false, bool GroupFse = false, bool CollectedLiterals = false,
        bool OptimalHuffmanDepth = false, unsigned LiteralGainLog = (OptimalHuffmanDepth ? 7U : 6U),
        bool DeferredCodes = false, bool WideSequenceBits = false,
        bool PackedLengthBits = false, bool GroupedStateWrites = GroupFse,
        bool AdaptiveGroupedWrites = false, bool AdaptiveWideWrites = false,
        bool AlignedLiterals,
        typename ParsedView = parsed_block_view>
    inline void encode_sequences_block(std::span<std::uint8_t const> input,
        std::type_identity_t<ParsedView> const& parsed, basic_compression_workspace<AlignedLiterals>& workspace,
        std::vector<std::uint8_t>& output)
    {
        static_assert(!WideSequenceBits || GroupedStateWrites);
        static_assert(!AdaptiveWideWrites || AdaptiveGroupedWrites);
        static_assert(!PackedLengthBits || GroupedStateWrites);
        output.clear();
        if (parsed.sequences.empty())
        {
            return;
        }

#ifdef SPH_ZSTDPP_TRACE_PHASES
        auto entropy_phase_start{std::chrono::steady_clock::now()};
        ++entropy_trace.blocks;
        entropy_trace.sequences += parsed.sequences.size();
#endif
        auto& literals{workspace.literals};
        auto& literal_codes{workspace.literal_codes};
        auto& offset_codes{workspace.offset_codes};
        auto& match_codes{workspace.match_codes};
        auto const block_end{parsed.trailing_literal_position + parsed.trailing_literal_length};
        auto const block_size{block_end - parsed.input_begin};
        if (literals.size() < block_size || literal_codes.size() < parsed.sequences.size())
        {
            workspace.reserve(block_size, parsed.sequences.size());
        }
        auto* literal_output{literals.data()};
        auto const* const literal_storage_end{literals.data() + literals.size()};
        auto const* const input_end{input.data() + input.size()};
        auto literal_position{parsed.input_begin};
        for (std::size_t index{}; index < parsed.sequences.size(); ++index)
        {
            auto const& sequence{parsed.sequences[index]};
            if constexpr (!CollectedLiterals)
            {
                constexpr std::size_t short_literal_copy_size{16U};
                if (sequence.literal_length <= short_literal_copy_size &&
                    literal_output + short_literal_copy_size <= literal_storage_end &&
                    input.data() + literal_position + short_literal_copy_size <= input_end)
                {
                    std::memcpy(literal_output, input.data() + literal_position,
                        short_literal_copy_size);
                }
                else
                {
                    std::memcpy(literal_output, input.data() + literal_position,
                        sequence.literal_length);
                }
            }
            literal_output += sequence.literal_length;
            literal_position += sequence.literal_length + sequence.match_length;
            auto const offset_base_value{sequence.repeat_code != 0U ?
                static_cast<std::uint32_t>(sequence.repeat_code) :
                sequence.offset + 3U};
            auto const offset_code{static_cast<unsigned>(std::bit_width(offset_base_value)) - 1U};
            if (offset_code >= offset_default_norm.size())
            {
                output.clear();
                return;
            }
            literal_codes[index] = literal_length_symbol(sequence.literal_length);
            offset_codes[index] = static_cast<std::uint8_t>(offset_code);
            match_codes[index] = match_length_symbol(sequence.match_length);
        }
        if constexpr (!CollectedLiterals)
            std::memcpy(literal_output, input.data() + parsed.trailing_literal_position,
                parsed.trailing_literal_length);
        auto const literal_size{static_cast<std::size_t>(literal_output - literals.data()) +
            parsed.trailing_literal_length};
        auto const literal_bytes{std::span<std::uint8_t const>{literals}.first(literal_size)};
        auto const literal_code_bytes{std::span<std::uint8_t const>{literal_codes}.first(
            parsed.sequences.size())};
        auto const offset_code_bytes{std::span<std::uint8_t const>{offset_codes}.first(
            parsed.sequences.size())};
        auto const match_code_bytes{std::span<std::uint8_t const>{match_codes}.first(
            parsed.sequences.size())};

#ifdef SPH_ZSTDPP_TRACE_PHASES
        entropy_trace.gather_ns += entropy_trace_elapsed(entropy_phase_start);
        entropy_trace.literal_bytes += literal_size;
        entropy_phase_start = std::chrono::steady_clock::now();
#endif
        auto& huffman_literals{workspace.huffman_output};
        // ZSTD_minGain compares the Huffman payload, excluding its header,
        // with the uncompressed literal count (not the raw-section size).
        auto const minimum_literal_gain{(literal_size >> LiteralGainLog) + 2U};
#ifdef SPH_ZSTDPP_TRACE_PHASES
        if (literal_size >= 40960U &&
            (parsed.sequences.empty() || literal_size / parsed.sequences.size() >= 20U))
            ++entropy_trace.huffman_sample_eligible_blocks;
#endif
        // Rank construction benefits the fast path without parser-time literal
        // gathering. Keep other strategies' measured code generation unchanged.
        bool skip_huffman{};
        if constexpr (GroupFse && !CollectedLiterals)
            skip_huffman = skip_huffman_from_samples(literal_bytes, parsed.sequences.size(), workspace.histogram_workspace);
        // Collected fast literals also benefit from rank-based canonical codes
        // and the associated short-input histogram policy.
        if (!skip_huffman && encode_huffman_literals<AlignedLiterals, !CarefulSelection, GroupFse, DeferredCodes>(literal_bytes, workspace, huffman_literals,
            OptimalHuffmanDepth, minimum_literal_gain))
        {
            output.swap(huffman_literals);
        }
        else
        {
            // Raw literals do not replace the decoder's previous Huffman table.
            auto& raw_literals{workspace.raw_literals};
            raw_literals.clear();
            append_raw_literals_header(raw_literals, literal_bytes.size());
            raw_literals.insert(raw_literals.end(), literal_bytes.begin(), literal_bytes.end());
            output.swap(raw_literals);
        }
#ifdef SPH_ZSTDPP_TRACE_PHASES
        entropy_trace.literals_ns += entropy_trace_elapsed(entropy_phase_start);
        entropy_phase_start = std::chrono::steady_clock::now();
#endif
        auto const literal_table{select_sequence_table<CarefulSelection, GroupFse,
            CarefulSelection ? 256U : literal_length_base.size()>(literal_code_bytes,
            literal_length_default_norm, 6U, 9U, true, workspace.table_descriptions[0],
            workspace.sequence_tables[0], workspace.histogram_workspace,
            workspace.sequence_histories[0])};
        auto const offset_table{select_sequence_table<CarefulSelection, GroupFse,
            CarefulSelection ? 256U : offset_base.size()>(offset_code_bytes,
            offset_default_norm, 5U, 8U,
            // Code generation rejects symbols outside the default alphabet.
            // Skip the redundant scan in the measured grouped fast path;
            // preserve other strategies' code layout until validated there.
            GroupFse || *std::max_element(offset_code_bytes.begin(), offset_code_bytes.end()) <= 28U,
            workspace.table_descriptions[1], workspace.sequence_tables[1],
            workspace.histogram_workspace, workspace.sequence_histories[1])};
        auto const match_length_table{select_sequence_table<CarefulSelection, GroupFse,
            CarefulSelection ? 256U : match_length_base.size()>(match_code_bytes,
            match_length_default_norm, 6U, 9U, true, workspace.table_descriptions[2],
            workspace.sequence_tables[2], workspace.histogram_workspace,
            workspace.sequence_histories[2])};

        append_sequence_count(output, parsed.sequences.size());
        output.push_back(static_cast<std::uint8_t>(
            (static_cast<unsigned>(literal_table.mode) << 6U) |
            (static_cast<unsigned>(offset_table.mode) << 4U) |
            (static_cast<unsigned>(match_length_table.mode) << 2U)));
        for (auto const& description : workspace.table_descriptions)
        {
            output.insert(output.end(), description.begin(), description.end());
        }

#ifdef SPH_ZSTDPP_TRACE_PHASES
        entropy_trace.tables_ns += entropy_trace_elapsed(entropy_phase_start);
        entropy_phase_start = std::chrono::steady_clock::now();
#endif
        if constexpr (AdaptiveWideWrites)
        {
            if (literal_size <= parsed.sequences.size() * 8U)
                write_sequence_bitstream<true, true>(parsed, workspace, literal_table,
                    offset_table, match_length_table, output);
            else
                write_sequence_bitstream<false, false>(parsed, workspace, literal_table,
                    offset_table, match_length_table, output);
#ifdef SPH_ZSTDPP_TRACE_PHASES
            entropy_trace.bits_ns += entropy_trace_elapsed(entropy_phase_start);
#endif
            return;
        }
        fse_compression_state literal_state{*literal_table.table};
        fse_compression_state offset_state{*offset_table.table};
        fse_compression_state match_state{*match_length_table.table};
        auto const last_index{parsed.sequences.size() - 1U};
        auto const& last{parsed.sequences[last_index]};
        auto const last_literal_code{literal_codes[last_index]};
        auto const last_offset_code{offset_codes[last_index]};
        auto const last_match_code{match_codes[last_index]};
        auto const last_offset_base{last.repeat_code != 0U ?
            static_cast<std::uint32_t>(last.repeat_code) : last.offset + 3U};
        literal_state.initialize(last_literal_code);
        offset_state.initialize(last_offset_code);
        match_state.initialize(last_match_code);

        auto& bits{[&]() -> auto&
        {
            if constexpr (WideSequenceBits) return workspace.wide_sequence_bits;
            else return workspace.sequence_bits;
        }()};
        bits.reset(output);
        bits.append(last.literal_length, literal_length_bits[last_literal_code]);
        bits.append(last.match_length - 3U, match_length_bits[last_match_code]);
        bits.append(last_offset_base, last_offset_code);
        auto const use_grouped{AdaptiveGroupedWrites &&
            literal_size <= parsed.sequences.size() * 8U};
        for (std::size_t index{last_index}; index-- > 0U;)
        {
            auto const& sequence{parsed.sequences[index]};
            auto const literal_code{literal_codes[index]};
            auto const offset_code{offset_codes[index]};
            auto const match_code{match_codes[index]};
            auto const encoded_offset{sequence.repeat_code != 0U ?
                static_cast<std::uint32_t>(sequence.repeat_code) : sequence.offset + 3U};
            if constexpr (GroupedStateWrites)
            {
                auto const of{offset_state.template encode_bits<true>(offset_code)};
                auto const ml{match_state.template encode_bits<true>(match_code)};
                auto const ll{literal_state.template encode_bits<true>(literal_code)};
                // OF/ML/LL table logs cap the combined emission at 26 bits.
                bits.append_unchecked(of.value | (ml.value << of.count) |
                    (ll.value << (of.count + ml.count)), of.count + ml.count + ll.count);
            }
            else if constexpr (AdaptiveGroupedWrites)
            {
                if (use_grouped)
                {
                    auto const of{offset_state.template encode_bits<true>(offset_code)};
                    auto const ml{match_state.template encode_bits<true>(match_code)};
                    auto const ll{literal_state.template encode_bits<true>(literal_code)};
                    bits.append_unchecked(of.value | (ml.value << of.count) |
                        (ll.value << (of.count + ml.count)), of.count + ml.count + ll.count);
                }
                else
                {
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
                    if constexpr (!CarefulSelection)
                    {
                        offset_state.encode_inlined(bits, offset_code);
                        match_state.encode_inlined(bits, match_code);
                        literal_state.encode_inlined(bits, literal_code);
                    }
                    else
#endif
                    {
                        offset_state.encode(bits, offset_code);
                        match_state.encode(bits, match_code);
                        literal_state.encode(bits, literal_code);
                    }
                }
            }
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
            else if constexpr (!CarefulSelection)
            {
                // GCC13 leaves the scalar state transition out of line in the
                // fast/double-fast/greedy writer. Lazy and optimal strategies
                // retain ordinary inlining: forcing them regresses throughput.
                offset_state.encode_inlined(bits, offset_code);
                match_state.encode_inlined(bits, match_code);
                literal_state.encode_inlined(bits, literal_code);
            }
#endif
            else
            {
                offset_state.encode(bits, offset_code);
                match_state.encode(bits, match_code);
                literal_state.encode(bits, literal_code);
            }
            if constexpr (PackedLengthBits)
            {
                auto const ll_bits{literal_length_bits[literal_code]};
                auto const ml_bits{match_length_bits[match_code]};
                auto const count{static_cast<unsigned>(ll_bits) + ml_bits};
                if (count != 0U)
                {
                    auto const ll_mask{(1U << ll_bits) - 1U};
                    auto const ml_mask{(1U << ml_bits) - 1U};
                    auto const packed{(sequence.literal_length & ll_mask) |
                        (((sequence.match_length - 3U) & ml_mask) << ll_bits)};
                    bits.append_unchecked(packed, count);
                }
            }
            else
            {
                bits.append(sequence.literal_length, literal_length_bits[literal_code]);
                bits.append(sequence.match_length - 3U, match_length_bits[match_code]);
            }
            bits.append(encoded_offset, offset_code);
        }
        match_state.flush(bits);
        offset_state.flush(bits);
        literal_state.flush(bits);
        bits.finish_into(output);
#ifdef SPH_ZSTDPP_TRACE_PHASES
        entropy_trace.bits_ns += entropy_trace_elapsed(entropy_phase_start);
#endif
    }

    template <bool CarefulSelection = false, bool GroupFse = false, bool CollectedLiterals = false,
        bool OptimalHuffmanDepth = false, unsigned LiteralGainLog = (OptimalHuffmanDepth ? 7U : 6U),
        bool DeferredCodes = false, bool WideSequenceBits = false,
        bool PackedLengthBits = false, bool GroupedStateWrites = GroupFse,
        bool AdaptiveGroupedWrites = false, bool AdaptiveWideWrites = false,
        bool AlignedLiterals>
    inline void encode_sequences_block(std::span<std::uint8_t const> input,
        compact_parsed_block const& parsed, basic_compression_workspace<AlignedLiterals>& workspace,
        std::vector<std::uint8_t>& output)
    {
        encode_sequences_block<CarefulSelection, GroupFse, CollectedLiterals,
            OptimalHuffmanDepth, LiteralGainLog, DeferredCodes, WideSequenceBits,
            PackedLengthBits, GroupedStateWrites, AdaptiveGroupedWrites, AdaptiveWideWrites,
            AlignedLiterals, compact_parsed_block_view>(
                input, compact_parsed_block_view{parsed}, workspace, output);
    }

    [[nodiscard]] inline auto encode_sequences_block(
        std::span<std::uint8_t const> input, parsed_block const& parsed) -> std::vector<std::uint8_t>
    {
        compression_workspace workspace;
        std::vector<std::uint8_t> output;
        encode_sequences_block(input, parsed, workspace, output);
        return output;
    }

    struct huffman_tree_node
    {
        std::uint32_t count{};
        std::uint16_t parent{};
        std::uint8_t symbol{};
        std::uint8_t number_bits{};
    };

    struct huffman_rank_position
    {
        std::uint16_t base{};
        std::uint16_t current{};
    };

    [[nodiscard]] constexpr auto huffman_count_bucket(std::uint32_t count) noexcept -> std::uint32_t
    {
        constexpr std::uint32_t logarithmic_buckets_begin{158U};
        constexpr std::uint32_t distinct_count_cutoff{
            logarithmic_buckets_begin + (std::bit_width(logarithmic_buckets_begin) - 1U)};
        return count < distinct_count_cutoff ? count :
            static_cast<std::uint32_t>(std::bit_width(count)) - 1U + logarithmic_buckets_begin;
    }

    inline void sort_huffman_bucket(
        huffman_tree_node* nodes,
        int low,
        int high)
    {
        constexpr int insertion_sort_threshold{8};
        if (high - low < insertion_sort_threshold)
        {
            for (auto index{low + 1}; index <= high; ++index)
            {
                auto const key{nodes[index]};
                auto preceding{index - 1};
                while (preceding >= low && nodes[preceding].count < key.count)
                {
                    nodes[preceding + 1] = nodes[preceding];
                    --preceding;
                }
                nodes[preceding + 1] = key;
            }
            return;
        }
        while (low < high)
        {
            auto const pivot{nodes[high].count};
            auto partition{low - 1};
            for (auto index{low}; index < high; ++index)
            {
                if (nodes[index].count > pivot)
                {
                    ++partition;
                    std::swap(nodes[partition], nodes[index]);
                }
            }
            ++partition;
            std::swap(nodes[partition], nodes[high]);
            if (partition - low < high - partition)
            {
                sort_huffman_bucket(nodes, low, partition - 1);
                low = partition + 1;
            }
            else
            {
                sort_huffman_bucket(nodes, partition + 1, high);
                high = partition - 1;
            }
        }
    }

    inline void sort_huffman_nodes(
        std::span<huffman_tree_node> nodes,
        std::span<std::uint32_t const> counts)
    {
        constexpr std::size_t rank_count{192U};
        constexpr std::size_t logarithmic_buckets_begin{158U};
        constexpr std::size_t distinct_count_cutoff{
            logarithmic_buckets_begin + (std::bit_width(logarithmic_buckets_begin) - 1U)};
        std::array<huffman_rank_position, rank_count> ranks{};
        for (auto const count : counts)
        {
            ++ranks[huffman_count_bucket(count)].base;
        }
        for (auto rank{rank_count - 1U}; rank != 0U; --rank)
        {
            ranks[rank - 1U].base = static_cast<std::uint16_t>(
                ranks[rank - 1U].base + ranks[rank].base);
            ranks[rank - 1U].current = ranks[rank - 1U].base;
        }
        for (std::size_t symbol{}; symbol < counts.size(); ++symbol)
        {
            auto const rank{huffman_count_bucket(counts[symbol]) + 1U};
            auto const position{ranks[rank].current++};
            nodes[position].count = counts[symbol];
            nodes[position].symbol = static_cast<std::uint8_t>(symbol);
        }
        for (auto rank{distinct_count_cutoff}; rank < rank_count - 1U; ++rank)
        {
            auto const size{ranks[rank].current - ranks[rank].base};
            if (size > 1)
            {
                sort_huffman_bucket(nodes.data() + ranks[rank].base, 0, size - 1);
            }
        }
    }

    [[nodiscard]] inline auto limit_huffman_height(
        huffman_tree_node* nodes,
        std::uint32_t last_non_null,
        std::uint32_t target_bits) -> std::uint32_t
    {
        auto const largest_bits{static_cast<std::uint32_t>(nodes[last_non_null].number_bits)};
        if (largest_bits <= target_bits)
        {
            return largest_bits;
        }

        auto total_cost{0};
        auto const base_cost{1U << (largest_bits - target_bits)};
        auto index{static_cast<int>(last_non_null)};
        while (nodes[index].number_bits > target_bits)
        {
            total_cost += static_cast<int>(base_cost -
                (1U << (largest_bits - nodes[index].number_bits)));
            nodes[index].number_bits = static_cast<std::uint8_t>(target_bits);
            --index;
        }
        while (nodes[index].number_bits == target_bits)
        {
            --index;
        }
        total_cost >>= largest_bits - target_bits;

        constexpr std::uint32_t no_symbol{0xF0F0F0F0U};
        std::array<std::uint32_t, 13> last_rank{};
        last_rank.fill(no_symbol);
        auto current_bits{target_bits};
        for (auto position{index}; position >= 0; --position)
        {
            if (nodes[position].number_bits >= current_bits)
            {
                continue;
            }
            current_bits = nodes[position].number_bits;
            last_rank[target_bits - current_bits] = static_cast<std::uint32_t>(position);
        }

        while (total_cost > 0)
        {
            auto bits_to_decrease{static_cast<std::uint32_t>(std::bit_width(
                static_cast<std::uint32_t>(total_cost)))};
            for (; bits_to_decrease > 1U; --bits_to_decrease)
            {
                auto const high_position{last_rank[bits_to_decrease]};
                auto const low_position{last_rank[bits_to_decrease - 1U]};
                if (high_position == no_symbol)
                {
                    continue;
                }
                if (low_position == no_symbol ||
                    nodes[high_position].count <= 2U * nodes[low_position].count)
                {
                    break;
                }
            }
            while (last_rank[bits_to_decrease] == no_symbol)
            {
                ++bits_to_decrease;
            }
            total_cost -= static_cast<int>(1U << (bits_to_decrease - 1U));
            ++nodes[last_rank[bits_to_decrease]].number_bits;
            if (last_rank[bits_to_decrease - 1U] == no_symbol)
            {
                last_rank[bits_to_decrease - 1U] = last_rank[bits_to_decrease];
            }
            if (last_rank[bits_to_decrease] == 0U)
            {
                last_rank[bits_to_decrease] = no_symbol;
            }
            else
            {
                --last_rank[bits_to_decrease];
                if (nodes[last_rank[bits_to_decrease]].number_bits != target_bits - bits_to_decrease)
                {
                    last_rank[bits_to_decrease] = no_symbol;
                }
            }
        }
        while (total_cost < 0)
        {
            if (last_rank[1] == no_symbol)
            {
                while (nodes[index].number_bits == target_bits)
                {
                    --index;
                }
                --nodes[index + 1].number_bits;
                last_rank[1] = static_cast<std::uint32_t>(index + 1);
            }
            else
            {
                --nodes[last_rank[1] + 1U].number_bits;
                ++last_rank[1];
            }
            ++total_cost;
        }
        return target_bits;
    }

    [[nodiscard]] inline auto build_reference_huffman_lengths(
        std::span<std::uint32_t const> frequencies,
        std::size_t literal_count, unsigned requested_depth = 0U)
        -> std::pair<std::array<std::uint8_t, 256>, unsigned>
    {
        constexpr int first_parent{256};
        std::array<huffman_tree_node, 513> storage{};
        auto* const nodes{storage.data() + 1U};
        sort_huffman_nodes(std::span<huffman_tree_node>{nodes, frequencies.size()}, frequencies);

        auto last_non_null{static_cast<int>(frequencies.size() - 1U)};
        while (nodes[last_non_null].count == 0U)
        {
            --last_non_null;
        }
        auto low_symbol{last_non_null};
        auto const root{first_parent + low_symbol - 1};
        auto low_node{first_parent};
        auto next_node{first_parent};
        nodes[next_node].count = nodes[low_symbol].count + nodes[low_symbol - 1].count;
        nodes[low_symbol].parent = nodes[low_symbol - 1].parent = static_cast<std::uint16_t>(next_node);
        ++next_node;
        low_symbol -= 2;
        for (auto index{next_node}; index <= root; ++index)
        {
            nodes[index].count = 1U << 30U;
        }
        nodes[-1].count = 1U << 31U;
        while (next_node <= root)
        {
            auto const first{nodes[low_symbol].count < nodes[low_node].count ? low_symbol-- : low_node++};
            auto const second{nodes[low_symbol].count < nodes[low_node].count ? low_symbol-- : low_node++};
            nodes[next_node].count = nodes[first].count + nodes[second].count;
            nodes[first].parent = nodes[second].parent = static_cast<std::uint16_t>(next_node);
            ++next_node;
        }
        nodes[root].number_bits = 0U;
        for (auto index{root - 1}; index >= first_parent; --index)
        {
            nodes[index].number_bits = static_cast<std::uint8_t>(nodes[nodes[index].parent].number_bits + 1U);
        }
        for (int index{}; index <= last_non_null; ++index)
        {
            nodes[index].number_bits = static_cast<std::uint8_t>(nodes[nodes[index].parent].number_bits + 1U);
        }

        auto target_bits{requested_depth};
        if (target_bits == 0U)
        {
            auto const maximum_bits_from_size{static_cast<unsigned>(std::bit_width(literal_count - 1U)) - 2U};
            auto const minimum_bits{std::min(
                static_cast<unsigned>(std::bit_width(literal_count)),
                static_cast<unsigned>(std::bit_width(frequencies.size() - 1U)) + 1U)};
            target_bits = std::clamp(maximum_bits_from_size, std::max(5U, minimum_bits), 11U);
        }
        assert(target_bits <= 11U && (std::size_t{1} << target_bits) >=
            static_cast<std::size_t>(last_non_null + 1));
        target_bits = limit_huffman_height(nodes, static_cast<std::uint32_t>(last_non_null), target_bits);

        std::array<std::uint8_t, 256> lengths{};
        for (std::size_t index{}; index < frequencies.size(); ++index)
        {
            lengths[nodes[index].symbol] = nodes[index].number_bits;
        }
        return {lengths, target_bits};
    }

    // Returns false when a wide alphabet cannot represent its weights using FSE
    // and the direct four-bit representation is unavailable. Only scratch state
    // is changed; committed Huffman/FSE sequence histories remain untouched.
    template <bool AlignedLiterals>
    [[nodiscard]] inline auto write_huffman_tree_description(
        std::span<std::uint8_t const> lengths, unsigned table_log,
        basic_compression_workspace<AlignedLiterals>& workspace, std::vector<std::uint8_t>& output) -> bool
    {
        output.clear();
        assert(lengths.size() >= 2U && lengths.size() <= 256U);
        auto const explicit_weights{lengths.size() - 1U};
        std::array<std::uint8_t, 256> weights{};
        std::array<unsigned, 12> counts{};
        unsigned maximum_weight{};
        for (std::size_t symbol{}; symbol < explicit_weights; ++symbol)
        {
            weights[symbol] = lengths[symbol] == 0U ? std::uint8_t{0} :
                static_cast<std::uint8_t>(table_log + 1U - lengths[symbol]);
            ++counts[weights[symbol]];
            maximum_weight = std::max(maximum_weight, static_cast<unsigned>(weights[symbol]));
        }
        auto const largest{*std::max_element(counts.begin(), counts.end())};
        if (explicit_weights > 1U && largest != explicit_weights && largest != 1U)
        {
            auto const log{optimal_fse_table_log(6U, explicit_weights, maximum_weight)};
            auto const normalized{normalize_counts(counts, explicit_weights, maximum_weight, log, false)};
            write_normalized_counts(normalized, workspace.weight_description);
            workspace.huffman_weight_table = build_fse_compression_table(normalized);
            workspace.huffman_weight_counts = normalized;
            workspace.huffman_weight_table_valid = true;
            compress_fse_bytes(std::span<std::uint8_t const>{weights}.first(explicit_weights),
                workspace.huffman_weight_table, workspace.weight_bits, workspace.weight_bitstream);
            auto const size{workspace.weight_description.size() + workspace.weight_bitstream.size()};
            if (!workspace.weight_bitstream.empty() && size > 1U && size < explicit_weights / 2U)
            {
                output.push_back(static_cast<std::uint8_t>(size));
                output.insert(output.end(), workspace.weight_description.begin(), workspace.weight_description.end());
                output.insert(output.end(), workspace.weight_bitstream.begin(), workspace.weight_bitstream.end());
                return true;
            }
        }
        if (explicit_weights > 128U) return false;
        output.push_back(static_cast<std::uint8_t>(127U + explicit_weights));
        for (std::size_t symbol{}; symbol < explicit_weights; symbol += 2U)
            output.push_back(static_cast<std::uint8_t>((weights[symbol] << 4U) | weights[symbol + 1U]));
        return true;
    }

    template <bool AlignedLiterals>
    [[nodiscard]] inline auto optimal_huffman_depth(std::span<std::uint32_t const> frequencies,
        std::size_t literal_count, basic_compression_workspace<AlignedLiterals>& workspace) -> unsigned
    {
        unsigned cardinality{};
        for (auto const count : frequencies) cardinality += count != 0U ? 1U : 0U;
        assert(cardinality >= 2U);
        // Reference deliberately starts one bit above log2 for power-of-two alphabets.
        auto const minimum{static_cast<unsigned>(std::bit_width(cardinality))};
        unsigned best_depth{11U};
        auto best_size{std::numeric_limits<std::size_t>::max() - 1U};
        for (auto depth{minimum}; depth <= 11U; ++depth)
        {
            auto const tree{build_reference_huffman_lengths(frequencies, literal_count, depth)};
            if (tree.second < depth && depth > minimum) break;
            if (!write_huffman_tree_description(
                std::span<std::uint8_t const>{tree.first}.first(frequencies.size()), tree.second,
                workspace, workspace.huffman_body)) continue;
            std::size_t bits{};
            for (std::size_t symbol{}; symbol < frequencies.size(); ++symbol)
                bits += static_cast<std::size_t>(frequencies[symbol]) * tree.first[symbol];
            auto const size{(bits >> 3U) + workspace.huffman_body.size()};
            if (size > best_size + 1U) break;
            if (size < best_size)
            {
                best_size = size;
                best_depth = depth;
            }
        }
        return best_depth;
    }

    struct literal_partition_estimate
    {
        estimated_literal_selection selection;
        std::size_t section_size{};
    };

    template <bool AlignedLiterals>
    [[nodiscard]] inline auto estimate_partition_literals(std::span<std::uint8_t const> literals,
        basic_compression_workspace<AlignedLiterals>& workspace, bool repeat_fully_valid = false,
        bool compression_disabled = false) -> std::optional<literal_partition_estimate>
    {
        auto const& counts{count_symbols(literals, workspace.histogram_workspace)};
        auto const largest{*std::max_element(counts.begin(), counts.end())};
        std::array<huffman_code, 256> candidate_codes{};
        bool tree_written{true};
        auto const selection{select_estimated_literal_table(literals.size(), largest,
            repeat_fully_valid, compression_disabled, [&]
            {
                std::size_t alphabet{counts.size()};
                while (alphabet > 1U && counts[alphabet - 1U] == 0U) --alphabet;
                auto const frequencies{std::span<std::uint32_t const>{counts}.first(alphabet)};
                auto const depth{optimal_huffman_depth(frequencies, literals.size(), workspace)};
                auto const tree{build_reference_huffman_lengths(frequencies, literals.size(), depth)};
                tree_written = write_huffman_tree_description(
                    std::span<std::uint8_t const>{tree.first}.first(alphabet), tree.second,
                    workspace, workspace.huffman_body);
                std::size_t new_bits{};
                std::size_t repeat_bits{};
                bool repeat_supported{workspace.huffman_repeat_valid};
                for (std::size_t symbol{}; symbol < alphabet; ++symbol)
                {
                    candidate_codes[symbol].number_bits = tree.first[symbol];
                    new_bits += static_cast<std::size_t>(counts[symbol]) * tree.first[symbol];
                    auto const repeat_length{workspace.huffman_repeat_lengths[symbol]};
                    repeat_bits += static_cast<std::size_t>(counts[symbol]) * repeat_length;
                    if (counts[symbol] != 0U && repeat_length == 0U) repeat_supported = false;
                }
                return literal_table_costs{new_bits >> 3U, workspace.huffman_body.size(),
                    repeat_supported ? std::optional<std::size_t>{repeat_bits >> 3U} : std::nullopt};
            })};
        if (!tree_written) return std::nullopt;
        if (selection.mode == sequence_table_mode::repeat)
            for (std::size_t symbol{}; symbol < candidate_codes.size(); ++symbol)
                candidate_codes[symbol].number_bits = workspace.huffman_repeat_lengths[symbol];
        return literal_partition_estimate{selection, estimate_literal_section_size(selection.mode,
            literals.size(), counts, candidate_codes, selection.description_size,
            selection.mode == sequence_table_mode::compressed)};
    }

    template <bool AlignedLiterals>
    [[nodiscard]] inline auto estimate_partition_size(std::span<std::uint8_t const> input,
        parsed_block_view const& parsed, basic_compression_workspace<AlignedLiterals>& workspace)
        -> std::optional<std::size_t>
    {
        auto literal_size{std::size_t{0}};
        auto position{parsed.input_begin};
        for (std::size_t index{}; index < parsed.sequences.size(); ++index)
        {
            auto const& sequence{parsed.sequences[index]};
            std::memcpy(workspace.literals.data() + literal_size, input.data() + position,
                sequence.literal_length);
            literal_size += sequence.literal_length;
            position += static_cast<std::size_t>(sequence.literal_length) + sequence.match_length;
            workspace.literal_codes[index] = literal_length_symbol(sequence.literal_length);
            workspace.match_codes[index] = match_length_symbol(sequence.match_length);
            auto const base{sequence.repeat_code != 0U ? sequence.repeat_code : sequence.offset + 3U};
            workspace.offset_codes[index] = static_cast<std::uint8_t>(std::bit_width(base) - 1);
        }
        std::memcpy(workspace.literals.data() + literal_size,
            input.data() + parsed.trailing_literal_position, parsed.trailing_literal_length);
        literal_size += parsed.trailing_literal_length;
        auto const literals{estimate_partition_literals(
            std::span<std::uint8_t const>{workspace.literals}.first(literal_size), workspace)};
        if (!literals) return std::nullopt;
        auto const count{parsed.sequences.size()};
        if (count == 0U)
            return literals->section_size + estimate_sequence_section_size(0U, 0U, 0U, 0U, 0U, true) + 3U;

        // Reference compares every trial with the same prevCBlock entropy state.
        // Selection changes the scratch histories, never the committed histories.
        auto histories{workspace.sequence_histories};
        std::array<std::span<std::uint8_t const>, 3> const codes{
            std::span<std::uint8_t const>{workspace.literal_codes}.first(count),
            std::span<std::uint8_t const>{workspace.offset_codes}.first(count),
            std::span<std::uint8_t const>{workspace.match_codes}.first(count)};
        std::array<std::span<std::int16_t const>, 3> const defaults{
            literal_length_default_norm, offset_default_norm, match_length_default_norm};
        std::array<std::span<std::uint8_t const>, 3> const extra_bits{
            literal_length_bits, std::span<std::uint8_t const>{}, match_length_bits};
        std::array<unsigned, 3> const default_logs{6U, 5U, 6U};
        std::array<unsigned, 3> const maximum_logs{9U, 8U, 9U};
        std::array<std::size_t, 3> sizes{};
        std::size_t descriptions{};
        for (std::size_t stream{}; stream < codes.size(); ++stream)
        {
            auto const basic_allowed{stream != 1U ||
                *std::max_element(codes[stream].begin(), codes[stream].end()) <= 28U};
            auto const selected{select_sequence_table<true>(codes[stream], defaults[stream],
                default_logs[stream], maximum_logs[stream], basic_allowed,
                workspace.table_descriptions[stream], workspace.sequence_tables[stream],
                workspace.histogram_workspace, histories[stream])};
            sizes[stream] = estimate_sequence_symbol_size(codes[stream], selected,
                histories[stream].counts, defaults[stream], default_logs[stream],
                extra_bits[stream], workspace.histogram_workspace);
            descriptions += workspace.table_descriptions[stream].size();
        }
        return literals->section_size + estimate_sequence_section_size(count,
            sizes[0], sizes[1], sizes[2], descriptions, true) + 3U;
    }

    inline void encode_huffman_stream(std::span<std::uint8_t const> input,
        std::array<huffman_code, 256> const& codes,
        std::vector<std::uint8_t>& output)
    {
#ifdef SPH_ZSTDPP_TRACE_PHASES
        auto const huffman_emit_start{std::chrono::steady_clock::now()};
#endif
        // Reserve eleven-bit codes plus padding for an overlapping word store.
        // compression_workspace preallocates this bound before processing.
        output.resize((input.size() * 11U + 7U) / 8U + 8U);
        auto* cursor{output.data()};
        std::uint64_t container{};
        unsigned count{};
        auto append_symbol = [&](std::size_t position)
        {
            auto const code{codes[input[position]]};
            if (code.number_bits == 0U || code.number_bits > 11U)
            {
                throw entropy_error{"invalid Zstandard Huffman encoding code"};
            }
            container |= static_cast<std::uint64_t>(code.value) << count;
            count += code.number_bits;
        };
        auto position{input.size()};
        for (; position >= 4U; position -= 4U)
        {
            // At most seven carried bits plus four eleven-bit codes: 51 bits.
            append_symbol(position - 1U);
            append_symbol(position - 2U);
            append_symbol(position - 3U);
            append_symbol(position - 4U);
            auto completed{container};
            if constexpr (std::endian::native == std::endian::big)
                completed = std::byteswap(completed);
            std::memcpy(cursor, &completed, sizeof(completed));
            auto const completed_bytes{count / 8U};
            cursor += completed_bytes;
            container >>= completed_bytes * 8U;
            count &= 7U;
        }
        while (position-- > 0U)
        {
            append_symbol(position);
        }
        container |= std::uint64_t{1} << count;
        auto const tail_bytes{(count + 8U) / 8U};
        for (unsigned index{}; index < tail_bytes; ++index)
        {
            *cursor++ = static_cast<std::uint8_t>(container);
            container >>= 8U;
        }
        output.resize(static_cast<std::size_t>(cursor - output.data()));
#ifdef SPH_ZSTDPP_TRACE_PHASES
        entropy_trace.huffman_emit_ns += entropy_trace_elapsed(huffman_emit_start);
#endif
    }

    /**
     * Builds a length-limited canonical Huffman tree. FSE-compressed weights
     * support the full byte alphabet; direct weights support symbols up to 128.
     */
    template <bool AlignedLiterals, bool SelectBeforeEmit, bool RankCanonical, bool DeferredCodes>
    [[nodiscard]] inline auto encode_huffman_literals(std::span<std::uint8_t const> literals,
        basic_compression_workspace<AlignedLiterals>& workspace, std::vector<std::uint8_t>& output,
        bool optimal_depth, std::size_t minimum_gain) -> bool
    {
        output.clear();
        if (literals.size() < 16U)
        {
            return false;
        }
#ifdef SPH_ZSTDPP_TRACE_PHASES
        auto huffman_phase_start{std::chrono::steady_clock::now()};
#endif
        // Small fast-strategy literal blocks may be cheaper with one histogram
        // than with four histograms and their reduction pass.
        auto const& frequencies{count_symbols<RankCanonical ? 8192U : 1500U>(
            literals, workspace.histogram_workspace)};
        std::uint8_t maximum_symbol{};
        std::size_t distinct_symbols{};
        std::uint32_t largest_frequency{};
        for (std::size_t symbol{}; symbol < frequencies.size(); ++symbol)
        {
            if (frequencies[symbol] != 0U)
            {
                ++distinct_symbols;
                largest_frequency = std::max(largest_frequency, frequencies[symbol]);
                maximum_symbol = static_cast<std::uint8_t>(symbol);
            }
        }
        if (distinct_symbols < 2U || largest_frequency <= (literals.size() >> 7U) + 4U)
        {
#ifdef SPH_ZSTDPP_TRACE_PHASES
            entropy_trace.huffman_histogram_ns += entropy_trace_elapsed(huffman_phase_start);
            ++entropy_trace.huffman_histogram_reject_blocks;
            entropy_trace.huffman_histogram_reject_bytes += literals.size();
#endif
            return false;
        }
#ifdef SPH_ZSTDPP_TRACE_PHASES
        entropy_trace.huffman_histogram_ns += entropy_trace_elapsed(huffman_phase_start);
        huffman_phase_start = std::chrono::steady_clock::now();
#endif

        std::array<std::uint8_t, 256> lengths{};
        unsigned table_log{};
        auto const frequency_span{std::span<std::uint32_t const>{frequencies}.first(
            static_cast<std::size_t>(maximum_symbol) + 1U)};
        bool repeat_symbols_valid{workspace.huffman_repeat_valid};
        if (repeat_symbols_valid)
        {
            for (std::size_t symbol{}; symbol < frequency_span.size(); ++symbol)
            {
                if (frequency_span[symbol] != 0U && workspace.huffman_repeat_lengths[symbol] == 0U)
                {
                    repeat_symbols_valid = false;
                    break;
                }
            }
        }
        auto const repeat_tree{repeat_symbols_valid &&
            workspace.huffman_repeat_maximum_symbol == maximum_symbol &&
            workspace.huffman_repeat_literal_size == literals.size() &&
            std::equal(frequencies.begin(), frequencies.begin() + maximum_symbol + 1U,
                workspace.huffman_repeat_frequencies.begin())};
        auto const same_tree{workspace.huffman_tree_valid &&
            workspace.huffman_tree_optimal_depth == optimal_depth &&
            workspace.huffman_tree_maximum_symbol == maximum_symbol &&
            workspace.huffman_tree_literal_size == literals.size() &&
            std::equal(frequencies.begin(), frequencies.begin() + maximum_symbol + 1U,
                workspace.huffman_tree_frequencies.begin())};
        if (repeat_tree)
        {
            lengths = workspace.huffman_repeat_lengths;
            table_log = workspace.huffman_repeat_log;
        }
        else if (same_tree)
        {
            lengths = workspace.huffman_tree_lengths;
            table_log = workspace.huffman_tree_log;
        }
        else
        {
            auto const depth{optimal_depth ? optimal_huffman_depth(frequency_span, literals.size(), workspace) : 0U};
            auto const built{build_reference_huffman_lengths(frequency_span, literals.size(), depth)};
            lengths = built.first;
            table_log = built.second;
            workspace.huffman_tree_frequencies = frequencies;
            workspace.huffman_tree_lengths = lengths;
            workspace.huffman_tree_maximum_symbol = maximum_symbol;
            workspace.huffman_tree_literal_size = literals.size();
            workspace.huffman_tree_log = table_log;
            workspace.huffman_tree_valid = true;
            workspace.huffman_tree_optimal_depth = optimal_depth;
        }
#ifdef SPH_ZSTDPP_TRACE_PHASES
        entropy_trace.huffman_tree_ns += entropy_trace_elapsed(huffman_phase_start);
        huffman_phase_start = std::chrono::steady_clock::now();
#endif

        std::array<std::uint8_t, 256> weights{};
        std::array<huffman_code, 256> codes{};
        auto const assign_codes = [&]() -> bool {
        std::size_t table_position{};
        if constexpr (RankCanonical)
        {
            std::array<std::size_t, 12> rank_counts{};
            std::array<std::size_t, 12> rank_codes{};
            for (std::size_t symbol{}; symbol <= maximum_symbol; ++symbol)
                ++rank_counts[lengths[symbol]];
            for (unsigned weight{1}; weight <= table_log; ++weight)
            {
                auto const code_length{table_log + 1U - weight};
                auto const run_length{std::size_t{1} << (weight - 1U)};
                rank_codes[code_length] = table_position >> (table_log - code_length);
                table_position += rank_counts[code_length] * run_length;
            }
            for (std::size_t symbol{}; symbol <= maximum_symbol; ++symbol)
            {
                auto const code_length{lengths[symbol]};
                if (code_length == 0U) continue;
                weights[symbol] = static_cast<std::uint8_t>(table_log + 1U - code_length);
                codes[symbol] = huffman_code{
                    static_cast<std::uint16_t>(rank_codes[code_length]++), code_length};
            }
        }
        else
        {
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
        }
        if (table_position != (std::size_t{1} << table_log))
        {
            return false;
        }
        return true;
        };
        if constexpr (DeferredCodes)
        {
            // Selection needs weights and a valid tree, but not canonical codes.
            std::size_t table_position{};
            for (std::size_t symbol{}; symbol <= maximum_symbol; ++symbol)
            {
                auto const length{lengths[symbol]};
                if (length == 0U || length > table_log) continue;
                auto const weight{table_log + 1U - length};
                weights[symbol] = static_cast<std::uint8_t>(weight);
                table_position += std::size_t{1} << (weight - 1U);
            }
            if (table_position != (std::size_t{1} << table_log)) return false;
        }
        else
        {
            // Preserve the original disabled-policy expression, not a lambda call.
            std::size_t table_position{};
            if constexpr (RankCanonical)
            {
                std::array<std::size_t, 12> rank_counts{};
                std::array<std::size_t, 12> rank_codes{};
                for (std::size_t symbol{}; symbol <= maximum_symbol; ++symbol)
                    ++rank_counts[lengths[symbol]];
                for (unsigned weight{1}; weight <= table_log; ++weight)
                {
                    auto const code_length{table_log + 1U - weight};
                    auto const run_length{std::size_t{1} << (weight - 1U)};
                    rank_codes[code_length] = table_position >> (table_log - code_length);
                    table_position += rank_counts[code_length] * run_length;
                }
                for (std::size_t symbol{}; symbol <= maximum_symbol; ++symbol)
                {
                    auto const code_length{lengths[symbol]};
                    if (code_length == 0U) continue;
                    weights[symbol] = static_cast<std::uint8_t>(table_log + 1U - code_length);
                    codes[symbol] = huffman_code{
                        static_cast<std::uint16_t>(rank_codes[code_length]++), code_length};
                }
            }
            else
            {
                for (unsigned weight{1}; weight <= table_log; ++weight)
                {
                    auto const code_length{table_log + 1U - weight};
                    auto const run_length{std::size_t{1} << (weight - 1U)};
                    for (std::size_t symbol{}; symbol <= maximum_symbol; ++symbol)
                    {
                        if (lengths[symbol] != code_length) continue;
                        weights[symbol] = static_cast<std::uint8_t>(weight);
                        codes[symbol] = huffman_code{
                            static_cast<std::uint16_t>(table_position >> (table_log - code_length)),
                            static_cast<std::uint8_t>(code_length)};
                        table_position += run_length;
                    }
                }
            }
            if (table_position != (std::size_t{1} << table_log)) return false;
        }

        auto const explicit_weights{static_cast<std::size_t>(maximum_symbol)};
        auto& body{workspace.huffman_body};
        auto& compressed_weights{workspace.compressed_weights};
        body.clear();
        compressed_weights.clear();
        if (!repeat_tree)
        {
            std::array<unsigned, 12> weight_counts{};
            unsigned maximum_weight{};
            for (std::size_t symbol{}; symbol < explicit_weights; ++symbol)
            {
                ++weight_counts[weights[symbol]];
                maximum_weight = std::max(maximum_weight, static_cast<unsigned>(weights[symbol]));
            }
            auto const most_frequent_weight{*std::max_element(weight_counts.begin(),
                weight_counts.begin() + static_cast<std::ptrdiff_t>(maximum_weight + 1U))};
            if (explicit_weights > 2U && most_frequent_weight != explicit_weights && most_frequent_weight != 1U)
            {
                auto const weight_table_log{optimal_fse_table_log(6U, explicit_weights, maximum_weight)};
                auto const normalized{normalize_counts(weight_counts, explicit_weights, maximum_weight,
                    weight_table_log, false)};
                auto& description{workspace.weight_description};
                auto& stream{workspace.weight_bitstream};
                write_normalized_counts(normalized, description);
                auto const same_table{workspace.huffman_weight_table_valid &&
                    workspace.huffman_weight_counts.maximum_symbol == normalized.maximum_symbol &&
                    workspace.huffman_weight_counts.table_log == normalized.table_log &&
                    std::equal(workspace.huffman_weight_counts.values.begin(),
                        workspace.huffman_weight_counts.values.begin() + normalized.maximum_symbol + 1U,
                        normalized.values.begin())};
                if (!same_table)
                {
                    workspace.huffman_weight_table = build_fse_compression_table(normalized);
                    workspace.huffman_weight_counts = normalized;
                    workspace.huffman_weight_table_valid = true;
                }
                compress_fse_bytes(
                    std::span<std::uint8_t const>{weights}.first(explicit_weights),
                    workspace.huffman_weight_table, workspace.weight_bits, stream);
                if (!stream.empty())
                {
                    compressed_weights.assign(description.begin(), description.end());
                    compressed_weights.insert(compressed_weights.end(), stream.begin(), stream.end());
                }
            }
        }
        if (!repeat_tree)
        {
            if (compressed_weights.size() > 1U && compressed_weights.size() < explicit_weights / 2U)
            {
                body.push_back(static_cast<std::uint8_t>(compressed_weights.size()));
                body.insert(body.end(), compressed_weights.begin(), compressed_weights.end());
            }
            else
            {
                if (explicit_weights > 128U) return false;
                body.push_back(static_cast<std::uint8_t>(127U + explicit_weights));
                for (std::size_t symbol{}; symbol < explicit_weights; symbol += 2U)
                {
                    auto const high{static_cast<std::uint8_t>(weights[symbol] << 4U)};
                    auto const low{symbol + 1U < explicit_weights ? weights[symbol + 1U] : std::uint8_t{0}};
                    body.push_back(static_cast<std::uint8_t>(high | low));
                }
            }
        }

        auto const four_streams{literals.size() >= 256U};
#ifdef SPH_ZSTDPP_TRACE_PHASES
        entropy_trace.huffman_weights_ns += entropy_trace_elapsed(huffman_phase_start);
        huffman_phase_start = std::chrono::steady_clock::now();
#endif
        bool prefer_repeat{};
        std::size_t predicted_new_body{};
        if (SelectBeforeEmit && repeat_symbols_valid && !repeat_tree)
        {
            auto const segment_size{four_streams ? (literals.size() + 3U) / 4U : literals.size()};
            auto const jump_size{four_streams ? 6U : 0U};
            auto const stream_count{four_streams ? 4U : 1U};
            std::size_t total_new_bits{};
            std::size_t total_repeat_bits{};
            for (std::size_t symbol{}; symbol <= maximum_symbol; ++symbol)
            {
                total_new_bits += static_cast<std::size_t>(frequencies[symbol]) * lengths[symbol];
                total_repeat_bits += static_cast<std::size_t>(frequencies[symbol]) *
                    workspace.huffman_repeat_lengths[symbol];
            }
            // Each stream contributes floor(bits/8)+1 bytes. Splitting the
            // total into four streams introduces at most three rounding bytes.
            auto const new_lower{body.size() + jump_size + total_new_bits / 8U + 1U};
            auto const new_upper{new_lower + stream_count - 1U};
            auto const repeat_lower{jump_size + total_repeat_bits / 8U + 1U};
            auto const repeat_upper{repeat_lower + stream_count - 1U};
            auto const bounded_segments{!four_streams || (table_log <= 11U &&
                workspace.huffman_repeat_log <= 11U && segment_size <= (65535U * 8U - 1U) / 11U)};
            auto const need_exact{!bounded_segments ||
                !(repeat_upper < new_lower || repeat_lower >= new_upper)};
            std::size_t new_size{need_exact ? body.size() + jump_size : new_lower};
            std::size_t repeat_size{need_exact ? jump_size : repeat_upper};
            bool repeat_size_valid{true};
            for (std::size_t stream{}; need_exact && stream < stream_count; ++stream)
            {
                auto const begin{std::min(literals.size(), stream * segment_size)};
                auto const end{std::min(literals.size(), (stream + 1U) * segment_size)};
                std::size_t new_bits{}, repeat_bits{};
                for (auto symbol : literals.subspan(begin, end - begin))
                {
                    new_bits += lengths[symbol];
                    repeat_bits += workspace.huffman_repeat_lengths[symbol];
                }
                // One end-marker bit, followed by rounding to a whole byte.
                auto const new_bytes{new_bits / 8U + 1U};
                auto const repeat_bytes{repeat_bits / 8U + 1U};
                if (four_streams && stream < 3U)
                {
                    if (new_bytes > std::numeric_limits<std::uint16_t>::max()) return false;
                    repeat_size_valid &= repeat_bytes <= std::numeric_limits<std::uint16_t>::max();
                }
                new_size += new_bytes;
                repeat_size += repeat_bytes;
            }
            std::size_t repeat_table_position{};
            for (std::size_t symbol{}; symbol <= workspace.huffman_repeat_maximum_symbol; ++symbol)
            {
                auto const length{workspace.huffman_repeat_lengths[symbol]};
                if (length != 0U)
                    repeat_table_position += std::size_t{1} << (workspace.huffman_repeat_log - length);
            }
            auto const old_c_size{(total_repeat_bits + 7U) / 8U + (four_streams ? 6U : 0U)};
            prefer_repeat = repeat_size_valid &&
                repeat_table_position == (std::size_t{1} << workspace.huffman_repeat_log) &&
                old_c_size < literals.size() && repeat_size < new_size;
            predicted_new_body = new_size;
        }
#ifdef SPH_ZSTDPP_TRACE_PHASES
        entropy_trace.huffman_selection_ns += entropy_trace_elapsed(huffman_phase_start);
        huffman_phase_start = std::chrono::steady_clock::now();
        if (prefer_repeat && !repeat_tree) ++entropy_trace.huffman_discarded_weights;
#endif
        if constexpr (DeferredCodes)
        {
            if (!prefer_repeat && !assign_codes()) return false;
        }
        if (!prefer_repeat && !four_streams)
        {
            auto& stream{workspace.huffman_streams[0]};
            encode_huffman_stream(literals, codes, stream);
            body.insert(body.end(), stream.begin(), stream.end());
        }
        else if (!prefer_repeat)
        {
            auto const segment_size{(literals.size() + 3U) / 4U};
            auto& streams{workspace.huffman_streams};
            for (std::size_t stream{}; stream < 4U; ++stream)
            {
                auto const begin{std::min(literals.size(), stream * segment_size)};
                auto const end{std::min(literals.size(), (stream + 1U) * segment_size)};
                encode_huffman_stream(literals.subspan(begin, end - begin), codes, streams[stream]);
            }
            if (streams[0].size() > std::numeric_limits<std::uint16_t>::max() ||
                streams[1].size() > std::numeric_limits<std::uint16_t>::max() ||
                streams[2].size() > std::numeric_limits<std::uint16_t>::max())
            {
                return false;
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

        bool emit_repeat{repeat_tree};
        if (SelectBeforeEmit ? prefer_repeat : (repeat_symbols_valid && !repeat_tree))
        {
            std::array<huffman_code, 256> repeat_codes{};
            auto const cached_codes{workspace.huffman_repeat_codes_valid &&
                workspace.huffman_repeat_codes_maximum_symbol == workspace.huffman_repeat_maximum_symbol &&
                workspace.huffman_repeat_codes_log == workspace.huffman_repeat_log &&
                std::equal(workspace.huffman_repeat_code_lengths.begin(),
                    workspace.huffman_repeat_code_lengths.end(), workspace.huffman_repeat_lengths.begin())};
            std::size_t repeat_table_position{cached_codes ?
                (std::size_t{1} << workspace.huffman_repeat_log) : 0U};
            if (cached_codes)
            {
                for (std::size_t symbol{}; symbol <= workspace.huffman_repeat_maximum_symbol; ++symbol)
                    repeat_codes[symbol] = workspace.huffman_repeat_codes[symbol];
            }
            else
            {
                // Canonical codes start at the table position for each length.
                // Count ranks first rather than rescanning all symbols per weight.
                std::array<std::size_t, 12> rank_counts{};
                std::array<std::size_t, 12> rank_codes{};
                for (std::size_t symbol{}; symbol <= workspace.huffman_repeat_maximum_symbol; ++symbol)
                    ++rank_counts[workspace.huffman_repeat_lengths[symbol]];
                for (unsigned weight{1}; weight <= workspace.huffman_repeat_log; ++weight)
                {
                    auto const code_length{workspace.huffman_repeat_log + 1U - weight};
                    rank_codes[code_length] = repeat_table_position >>
                        (workspace.huffman_repeat_log - code_length);
                    repeat_table_position += rank_counts[code_length] << (weight - 1U);
                }
                for (std::size_t symbol{}; symbol <= workspace.huffman_repeat_maximum_symbol; ++symbol)
                {
                    auto const code_length{workspace.huffman_repeat_lengths[symbol]};
                    if (code_length == 0U)
                        continue;
                    auto const code{huffman_code{static_cast<std::uint16_t>(rank_codes[code_length]++),
                        static_cast<std::uint8_t>(code_length)}};
                    repeat_codes[symbol] = code;
                    workspace.huffman_repeat_codes[symbol] = code;
                    workspace.huffman_repeat_code_lengths[symbol] = code.number_bits;
                }
            }
            if (repeat_table_position == (std::size_t{1} << workspace.huffman_repeat_log))
            {
                if (!cached_codes)
                {
                    workspace.huffman_repeat_codes_maximum_symbol = workspace.huffman_repeat_maximum_symbol;
                    workspace.huffman_repeat_codes_log = workspace.huffman_repeat_log;
                    workspace.huffman_repeat_codes_valid = true;
                }
                auto& repeat_body{workspace.huffman_repeat_body};
                repeat_body.clear();
                if (!four_streams)
                {
                    auto& stream{workspace.huffman_streams[0]};
                    encode_huffman_stream(literals, repeat_codes, stream);
                    repeat_body.insert(repeat_body.end(), stream.begin(), stream.end());
                }
                else
                {
                    auto const segment_size{(literals.size() + 3U) / 4U};
                    auto& streams{workspace.huffman_streams};
                    for (std::size_t stream{}; stream < 4U; ++stream)
                    {
                        auto const begin{std::min(literals.size(), stream * segment_size)};
                        auto const end{std::min(literals.size(), (stream + 1U) * segment_size)};
                        encode_huffman_stream(literals.subspan(begin, end - begin), repeat_codes, streams[stream]);
                    }
                    if (streams[0].size() <= std::numeric_limits<std::uint16_t>::max() &&
                        streams[1].size() <= std::numeric_limits<std::uint16_t>::max() &&
                        streams[2].size() <= std::numeric_limits<std::uint16_t>::max())
                    {
                        for (std::size_t stream{}; stream < 3U; ++stream)
                        {
                            repeat_body.push_back(static_cast<std::uint8_t>(streams[stream].size()));
                            repeat_body.push_back(static_cast<std::uint8_t>(streams[stream].size() >> 8U));
                        }
                        for (auto const& stream : streams)
                        {
                            repeat_body.insert(repeat_body.end(), stream.begin(), stream.end());
                        }
                    }
                }
                std::size_t old_bits{};
                for (std::size_t symbol{}; symbol <= maximum_symbol; ++symbol)
                {
                    old_bits += static_cast<std::size_t>(frequencies[symbol]) *
                        workspace.huffman_repeat_lengths[symbol];
                }
                auto const old_c_size{(old_bits + 7U) / 8U + (four_streams ? 6U : 0U)};
                std::size_t new_c_size{};
                if (four_streams)
                {
                    auto const& streams{workspace.huffman_streams};
                    new_c_size = 6U + streams[0].size() + streams[1].size() +
                        streams[2].size() + streams[3].size();
                }
                else
                {
                    new_c_size = workspace.huffman_streams[0].size();
                }
                auto const h_size{(SelectBeforeEmit ? predicted_new_body : body.size()) - new_c_size};
                auto const repeat_profitable{old_c_size < literals.size() &&
                    (old_c_size <= h_size + new_c_size || h_size + 12U >= literals.size())};
                if (repeat_profitable && repeat_body.size() < (SelectBeforeEmit ? predicted_new_body : body.size()))
                {
                    body.swap(repeat_body);
                    emit_repeat = true;
                }
            }
        }
#ifdef SPH_ZSTDPP_TRACE_PHASES
        entropy_trace.huffman_streams_ns += entropy_trace_elapsed(huffman_phase_start);
#endif

        auto const regenerated_size{literals.size()};
        auto const compressed_size{body.size()};
        // Reject before committing the candidate table. A subsequent block can
        // still reuse the last table actually emitted in this frame.
        if (minimum_gain != 0U && (regenerated_size <= minimum_gain ||
            compressed_size >= regenerated_size - minimum_gain))
            return false;
        if (regenerated_size < 1024U && compressed_size < 1024U)
        {
            auto const header{static_cast<std::uint32_t>((regenerated_size << 4U) |
                (compressed_size << 14U) | (four_streams ? 4U : 0U) | (emit_repeat ? 3U : 2U))};
            output = {static_cast<std::uint8_t>(header), static_cast<std::uint8_t>(header >> 8U),
                static_cast<std::uint8_t>(header >> 16U)};
        }
        else if (regenerated_size < 16'384U && compressed_size < 16'384U)
        {
            auto const header{static_cast<std::uint32_t>((regenerated_size << 4U) |
                (compressed_size << 18U) | (emit_repeat ? 11U : 10U))};
            output = {static_cast<std::uint8_t>(header), static_cast<std::uint8_t>(header >> 8U),
                static_cast<std::uint8_t>(header >> 16U), static_cast<std::uint8_t>(header >> 24U)};
        }
        else if (regenerated_size < 262'144U && compressed_size < 262'144U)
        {
            auto const header{static_cast<std::uint32_t>((regenerated_size << 4U) |
                ((compressed_size & 0x3FFU) << 22U) | (emit_repeat ? 15U : 14U))};
            output = {static_cast<std::uint8_t>(header), static_cast<std::uint8_t>(header >> 8U),
                static_cast<std::uint8_t>(header >> 16U), static_cast<std::uint8_t>(header >> 24U),
                static_cast<std::uint8_t>(compressed_size >> 10U)};
        }
        else
        {
            return false;
        }
        output.insert(output.end(), body.begin(), body.end());
        if (!emit_repeat)
        {
            workspace.huffman_repeat_frequencies = frequencies;
            workspace.huffman_repeat_lengths = lengths;
            workspace.huffman_repeat_maximum_symbol = maximum_symbol;
            workspace.huffman_repeat_literal_size = literals.size();
            workspace.huffman_repeat_log = table_log;
            workspace.huffman_repeat_valid = true;
        }
#ifdef SPH_ZSTDPP_TRACE_PHASES
        if (emit_repeat) ++entropy_trace.huffman_repeat_blocks;
        else ++entropy_trace.huffman_new_blocks;
#endif
        return true;
    }

    [[nodiscard]] inline auto encode_huffman_literals(std::span<std::uint8_t const> literals)
        -> std::optional<std::vector<std::uint8_t>>
    {
        compression_workspace workspace;
        std::vector<std::uint8_t> output;
        if (!encode_huffman_literals(literals, workspace, output))
        {
            return std::nullopt;
        }
        return output;
    }

    /**
     * Encodes one profitable match as a compressed block. Each sequence alphabet
     * uses Zstandard's one-symbol FSE/RLE mode, so there are no ANS state bits;
     * only the literal-length, offset, and match-length extra bits are emitted.
     * This is a complete interoperable entropy path and a stepping stone to
     * multi-sequence normalized FSE tables.
     */
    template <bool AlignedLiterals>
    inline void encode_single_match_block(std::span<std::uint8_t const> input,
        match const& selected, basic_compression_workspace<AlignedLiterals>& workspace,
        std::vector<std::uint8_t>& output)
    {
        if (selected.position + selected.length > input.size() || selected.length < 3U)
        {
            throw entropy_error{"invalid match selected by Zstandard compressor"};
        }
        auto const trailing_size{input.size() - selected.position - selected.length};
        auto const literal_size{selected.position + trailing_size};
        auto const literal_symbol{literal_length_symbol(selected.position)};
        auto const match_symbol{match_length_symbol(selected.length)};
        auto const selected_offset_symbol{find_offset_symbol(selected.offset)};

        auto& literals{workspace.literals};
        auto& raw_literals{workspace.raw_literals};
        auto& huffman_output{workspace.huffman_output};
        literals.clear();
        raw_literals.clear();
        huffman_output.clear();
        output.clear();
        if (literals.capacity() < literal_size || raw_literals.capacity() < literal_size + 3U ||
            output.capacity() < input.size())
        {
            workspace.reserve(input.size(), 1U);
        }
        literals.resize(literal_size);
        auto* literal_output{literals.data()};
        std::memcpy(literal_output, input.data(), selected.position);
        std::memcpy(literal_output + selected.position,
            input.data() + selected.position + selected.length, trailing_size);

        append_raw_literals_header(raw_literals, literals.size());
        auto const header_size{raw_literals.size()};
        raw_literals.resize(header_size + literals.size());
        std::memcpy(raw_literals.data() + header_size, literals.data(), literals.size());
        if (encode_huffman_literals(literals, workspace, huffman_output) &&
            huffman_output.size() < raw_literals.size())
        {
            output.resize(huffman_output.size());
            std::memcpy(output.data(), huffman_output.data(), huffman_output.size());
        }
        else
        {
            output.resize(raw_literals.size());
            std::memcpy(output.data(), raw_literals.data(), raw_literals.size());
        }

        output.push_back(1U); // one sequence
        output.push_back(0x54U); // LL, OF, and ML all use one-symbol FSE/RLE tables
        output.push_back(literal_symbol);
        output.push_back(selected_offset_symbol);
        output.push_back(match_symbol);

        workspace.sequence_bits.reset(output);
        workspace.sequence_bits.append(static_cast<std::uint32_t>(selected.position - literal_length_base[literal_symbol]),
            literal_length_bits[literal_symbol]);
        workspace.sequence_bits.append(static_cast<std::uint32_t>(selected.length - match_length_base[match_symbol]),
            match_length_bits[match_symbol]);
        workspace.sequence_bits.append(static_cast<std::uint32_t>(selected.offset - offset_base[selected_offset_symbol]),
            offset_bits[selected_offset_symbol]);
        workspace.sequence_bits.finish_into(output);
    }

    [[nodiscard]] inline auto encode_single_match_block(
        std::span<std::uint8_t const> input, match const& selected) -> std::vector<std::uint8_t>
    {
        compression_workspace workspace;
        workspace.reserve(input.size(), 1U);
        std::vector<std::uint8_t> output;
        output.reserve(input.size());
        encode_single_match_block(input, selected, workspace, output);
        return output;
    }
}
