#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <vector>

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#include <emmintrin.h>
#endif

#include <sph/zstd++/detail/zstd_entropy.h>

namespace sph::zstd::detail
{
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
    using symbol_counts = std::array<std::uint32_t, 256>;
    using symbol_count_workspace = std::array<symbol_counts, 4>;

    [[nodiscard]] inline auto count_symbols(std::span<std::uint8_t const> input,
        symbol_count_workspace& workspace) noexcept -> symbol_counts const&
    {
        constexpr std::size_t parallel_threshold{1500U};
        if (input.size() < parallel_threshold)
        {
            workspace[0].fill(0U);
            for (auto const symbol : input)
            {
                ++workspace[0][symbol];
            }
            return workspace[0];
        }

        for (auto& counts : workspace)
        {
            counts.fill(0U);
        }
        auto position{std::size_t{0}};
        for (; position + 16U <= input.size(); position += 16U)
        {
            for (std::size_t stripe{}; stripe < 4U; ++stripe)
            {
                auto const begin{position + stripe * 4U};
                ++workspace[0][input[begin]];
                ++workspace[1][input[begin + 1U]];
                ++workspace[2][input[begin + 2U]];
                ++workspace[3][input[begin + 3U]];
            }
        }
        for (; position < input.size(); ++position)
        {
            ++workspace[0][input[position]];
        }
        for (std::size_t symbol{}; symbol < workspace[0].size(); ++symbol)
        {
            workspace[0][symbol] += workspace[1][symbol] + workspace[2][symbol] +
                workspace[3][symbol];
        }
        return workspace[0];
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
            : hash_table_(std::size_t{1} << std::clamp(hash_log, 10U, 25U)),
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
                auto const values{_mm_loadu_si128(reinterpret_cast<__m128i const*>(tags + offset))};
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

        template <typename Hasher>
        [[nodiscard]] auto next(std::uint32_t index, Hasher&& hasher) noexcept -> std::uint32_t
        {
            auto const slot{index & mask};
            auto const current{values_[slot]};
            values_[slot] = static_cast<std::uint32_t>(hasher(index + size));
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

        [[nodiscard]] auto search(std::uint32_t hash, std::uint32_t low_limit,
            std::uint32_t attempts, std::span<std::uint32_t> candidates) noexcept -> std::size_t
        {
            salt_.observe(hash);
            return table_.collect_candidates(hash, row_log_, low_limit, attempts, candidates);
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
              match_length{static_cast<std::uint32_t>(match_length_value)},
              repeat_code{repeat_code_value}, offset{static_cast<std::uint32_t>(offset_value)}
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
        std::vector<parsed_sequence> sequences;
        std::size_t input_begin{};
        std::size_t trailing_literal_position{};
        std::size_t trailing_literal_length{};
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
    public:
        fast_match_state(unsigned window_log, unsigned hash_log, unsigned minimum_match,
            std::size_t target_length)
            : window_log_{window_log}, hash_log_{hash_log}, minimum_match_{minimum_match},
              target_length_{target_length},
              hash_table_(std::size_t{1} << match_parameter<HashLog>::resolve(hash_log)),
              hash_tags_(hash_table_.size())
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

            // A large uniform block has a deterministic fast-parser result:
            // one literal seeds the history and the remainder is an offset-1
            // match.  Avoid walking the hash table in this common case; the
            // caller still applies the normal entropy/block-selection gates.
            if (block_size >= 1024U &&
                (known_rle.has_value() ? *known_rle : is_rle(input.subspan(block_begin, block_size))))
            {
                if constexpr (MinimumMatch >= 7U)
                    result.sequences.push_back(parsed_sequence{2U, block_size - 2U, 1U, 1U});
                else
                    result.sequences.push_back(parsed_sequence{1U, block_size - 1U, 1U, 0U});
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
                auto match_index{hash_table[hash_zero]};
                std::size_t current_index{};
                bool found{};
                bool repeat_match{};
                auto const prefix_index{static_cast<std::uint32_t>(frame_index_base_ + prefix_start + index_bias)};
                do
                {
                    current_index = frame_index_base_ + position + index_bias;
                    auto const previous_tag{hash_tags[hash_zero]};
                    hash_table[hash_zero] = static_cast<std::uint32_t>(current_index);
                    hash_tags[hash_zero] = input[position];

                    if (repeat_one != 0U && equal_four(input, position_two, position_two - repeat_one))
                    {
                        position = position_two;
                        hash_table[hash_one] = static_cast<std::uint32_t>(
                            frame_index_base_ + position_one + index_bias);
                        found = true;
                        repeat_match = true;
                        break;
                    }

                    if (previous_tag == input[position] &&
                        matches(input, position, match_index, prefix_index))
                    {
                        hash_table[hash_one] = static_cast<std::uint32_t>(
                            frame_index_base_ + position_one + index_bias);
                        found = true;
                        break;
                    }

                    match_index = hash_table[hash_one];
                    hash_zero = hash_one;
                    hash_one = hash(input, position_two);
                    position = position_one;
                    position_one = position_two;
                    position_two = position_three;
                    current_index = frame_index_base_ + position + index_bias;
                    auto const previous_tag_one{hash_tags[hash_zero]};
                    hash_table[hash_zero] = static_cast<std::uint32_t>(current_index);
                    hash_tags[hash_zero] = input[position];

                    if (previous_tag_one == input[position] &&
                        matches(input, position, match_index, prefix_index))
                    {
                        if (step <= 4U)
                        {
                            hash_table[hash_one] = static_cast<std::uint32_t>(
                                frame_index_base_ + position_one + index_bias);
                        }
                        found = true;
                        break;
                    }

                    match_index = hash_table[hash_one];
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

                result.sequences.push_back(parsed_sequence{
                    position - anchor, match_length, offset, repeat_code});
                position += match_length;
                anchor = position;

                if (position <= limit)
                {
                    auto const catchup_position{current_index - frame_index_base_ - index_bias + 2U};
                    auto const catchup_hash{hash(input, catchup_position)};
                    hash_table[catchup_hash] = static_cast<std::uint32_t>(current_index + 2U);
                    hash_tags[catchup_hash] = input[catchup_position];
                    auto const trailing_hash{hash(input, position - 2U)};
                    hash_table[trailing_hash] = static_cast<std::uint32_t>(
                        frame_index_base_ + position - 2U + index_bias);
                    hash_tags[trailing_hash] = input[position - 2U];

                    if (repeat_two != 0U)
                    {
                        while (position <= limit && equal_four(input, position, position - repeat_two))
                        {
                            auto const repeat_length{count_matching_bytes(
                                input, position, position - repeat_two, block_end, 4U)};
                            std::swap(repeat_one, repeat_two);
                            auto const repeat_hash{hash(input, position)};
                            hash_table[repeat_hash] = static_cast<std::uint32_t>(
                                frame_index_base_ + position + index_bias);
                            hash_tags[repeat_hash] = input[position];
                            result.sequences.push_back(parsed_sequence{
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
            return result;
        }

    private:
        static constexpr std::size_t hash_read_size{8U};
        static constexpr std::size_t index_bias{2U};
        static constexpr unsigned search_strength{8U};

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
            if constexpr (configured_minimum_match == 5U)
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
            if (biased_match < lowest)
            {
                return false;
            }
            return equal_four(input, position, static_cast<std::size_t>(
                biased_match - frame_index_base_ - index_bias));
        }

        unsigned window_log_{};
        unsigned hash_log_{};
        unsigned minimum_match_{};
        std::size_t target_length_{};
        match_table hash_table_;
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
    public:
        double_fast_match_state(unsigned window_log, unsigned long_hash_log,
            unsigned short_hash_log, unsigned minimum_match)
            : window_log_{window_log}, long_hash_log_{long_hash_log}, short_hash_log_{short_hash_log},
              minimum_match_{minimum_match},
              long_table_(std::size_t{1} << match_parameter<LongHashLog>::resolve(long_hash_log)),
              short_table_(std::size_t{1} << match_parameter<ShortHashLog>::resolve(short_hash_log)),
              long_tags_(long_table_.size()), short_tags_(short_table_.size())
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
                std::ranges::fill(long_table_, 0U);
                std::ranges::fill(short_table_, 0U);
                frame_index_base_ = 0U;
            }
            else
            {
                frame_index_base_ = static_cast<std::uint32_t>(next_base);
            }
            frame_extent_ = 0U;
            repeat_offsets_ = {1U, 4U, 8U};
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

            if (block_size >= 1024U &&
                (known_rle.has_value() ? *known_rle : is_rle(input.subspan(block_begin, block_size))))
            {
                result.sequences.push_back(parsed_sequence{2U, block_size - 2U, 1U, 1U});
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
            auto const lowest_biased{frame_index_base_ + prefix_start + index_bias};
            auto const limit{block_end - hash_read_size};
            auto* const long_table{long_table_.data()};
            auto* const short_table{short_table_.data()};
            auto* const long_tags{long_tags_.data()};
            auto* const short_tags{short_tags_.data()};
            auto const* const input_data{input.data()};
            auto anchor{block_begin};
            auto position{block_begin + static_cast<std::size_t>(block_begin == prefix_start)};
            auto repeat_one{repeat_offsets_[0]};
            auto repeat_two{repeat_offsets_[1]};
            std::size_t saved_one{};
            std::size_t saved_two{};
            auto const maximum_repeat{position - prefix_start};
            if (repeat_two > maximum_repeat) saved_two = repeat_two, repeat_two = 0U;
            if (repeat_one > maximum_repeat) saved_one = repeat_one, repeat_one = 0U;

            while (true)
            {
                std::size_t step{1U};
                auto next_step{position + search_step};
                auto next_position{position + step};
                if (next_position > limit)
                {
                    break;
                }

                auto long_hash_zero{long_hash(input_data + position)};
                auto long_index_zero{long_table[long_hash_zero]};
                std::uint32_t current_biased{};
                std::size_t match_length{};
                std::size_t offset{};
                std::size_t match_position{};
                std::size_t long_hash_one{};
                std::uint8_t repeat_code{};
                bool found{};

                do
                {
                    auto const short_hash_zero{short_hash(input_data + position)};
                    auto const short_index_zero{short_table[short_hash_zero]};
                    auto const previous_long_tag{long_tags[long_hash_zero]};
                    auto const previous_short_tag{short_tags[short_hash_zero]};
                    current_biased = static_cast<std::uint32_t>(
                        frame_index_base_ + position + index_bias);
                    long_table[long_hash_zero] = current_biased;
                    short_table[short_hash_zero] = current_biased;
                    long_tags[long_hash_zero] = short_tags[short_hash_zero] = input[position];

                    if (repeat_one != 0U && equal_four(input_data + position + 1U,
                        input_data + position + 1U - repeat_one))
                    {
                        ++position;
                        match_position = position - repeat_one;
                        match_length = count_match(input_data, position, match_position, block_end, 4U);
                        offset = repeat_one;
                        repeat_code = 1U;
                        found = true;
                        break;
                    }

                    long_hash_one = long_hash(input_data + next_position);
                    if (valid(long_index_zero, lowest_biased) &&
                        previous_long_tag == input[position] &&
                        equal_eight(input_data + position, input_data + unbiased(long_index_zero)))
                    {
                        match_position = unbiased(long_index_zero);
                        match_length = count_match(input_data, position, match_position, block_end, 8U);
                        offset = position - match_position;
                        catch_up(input, position, match_position, anchor, prefix_start, match_length);
                        found = true;
                        break;
                    }

                    auto const long_index_one{long_table[long_hash_one]};
                    if (valid(short_index_zero, lowest_biased) &&
                        previous_short_tag == input[position] &&
                        equal_four(input_data + position, input_data + unbiased(short_index_zero)))
                    {
                        match_position = unbiased(short_index_zero);
                        match_length = count_match(input_data, position, match_position, block_end, 4U);
                        offset = position - match_position;
                        if (long_index_one > lowest_biased &&
                            long_tags[long_hash_one] == input[next_position] &&
                            equal_eight(input_data + next_position, input_data + unbiased(long_index_one)))
                        {
                            auto const next_match_position{unbiased(long_index_one)};
                            auto const next_length{count_match(
                                input_data, next_position, next_match_position, block_end, 8U)};
                            if (next_length > match_length)
                            {
                                position = next_position;
                                match_position = next_match_position;
                                match_length = next_length;
                                offset = position - match_position;
                            }
                        }
                        catch_up(input, position, match_position, anchor, prefix_start, match_length);
                        found = true;
                        break;
                    }

                    if (next_position >= next_step)
                    {
                        ++step;
                        next_step += search_step;
                    }
                    position = next_position;
                    next_position += step;
                    long_hash_zero = long_hash_one;
                    long_index_zero = long_index_one;
                }
                while (next_position <= limit);

                if (!found)
                {
                    break;
                }

                if (repeat_code == 0U)
                {
                    repeat_two = repeat_one;
                    repeat_one = offset;
                    if (step < 4U)
                    {
                        long_table[long_hash_one] = static_cast<std::uint32_t>(
                            frame_index_base_ + next_position + index_bias);
                        long_tags[long_hash_one] = input[next_position];
                    }
                }
                result.sequences.push_back(parsed_sequence{
                    position - anchor, match_length, offset, repeat_code});
                position += match_length;
                anchor = position;

                if (position <= limit)
                {
                    auto const insertion_position{unbiased(current_biased) + 2U};
                    auto const insertion_long_hash{long_hash(input_data + insertion_position)};
                    auto const insertion_short_hash{short_hash(input_data + insertion_position)};
                    long_table[insertion_long_hash] = short_table[insertion_short_hash] =
                        current_biased + 2U;
                    long_tags[insertion_long_hash] = short_tags[insertion_short_hash] =
                        input[insertion_position];
                    auto const position_long_hash{long_hash(input_data + position - 2U)};
                    long_table[position_long_hash] =
                        static_cast<std::uint32_t>(
                            frame_index_base_ + position - 2U + index_bias);
                    long_tags[position_long_hash] = input[position - 2U];
                    auto const position_short_hash{short_hash(input_data + position - 1U)};
                    short_table[position_short_hash] =
                        static_cast<std::uint32_t>(
                            frame_index_base_ + position - 1U + index_bias);
                    short_tags[position_short_hash] = input[position - 1U];

                    while (position <= limit && repeat_two != 0U &&
                        equal_four(input_data + position, input_data + position - repeat_two))
                    {
                        auto const repeat_length{count_match(
                            input_data, position, position - repeat_two, block_end, 4U)};
                        std::swap(repeat_one, repeat_two);
                        auto const repeat_long_hash{long_hash(input_data + position)};
                        auto const repeat_short_hash{short_hash(input_data + position)};
                        short_table[repeat_short_hash] = long_table[repeat_long_hash] =
                            static_cast<std::uint32_t>(frame_index_base_ + position + index_bias);
                        short_tags[repeat_short_hash] = long_tags[repeat_long_hash] = input[position];
                        result.sequences.push_back(parsed_sequence{
                            0U, repeat_length, repeat_one, 1U});
                        position += repeat_length;
                        anchor = position;
                    }
                }
            }

            saved_two = saved_one != 0U && repeat_one != 0U ? saved_one : saved_two;
            repeat_offsets_[0] = repeat_one != 0U ? repeat_one : saved_one;
            repeat_offsets_[1] = repeat_two != 0U ? repeat_two : saved_two;
            result.trailing_literal_position = anchor;
            result.trailing_literal_length = block_end - anchor;
            return result;
        }

    private:
        static constexpr std::size_t hash_read_size{8U};
        static constexpr std::size_t index_bias{2U};
        static constexpr std::size_t search_step{256U};

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
            if constexpr (configured_minimum_match == 5U)
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
            return static_cast<std::size_t>(position - frame_index_base_ - index_bias);
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
        match_table long_table_;
        match_table short_table_;
        row_tag_table long_tags_;
        row_tag_table short_tags_;
        std::uint32_t frame_index_base_{};
        std::uint32_t frame_extent_{};
        std::array<std::size_t, 3> repeat_offsets_{1U, 4U, 8U};
    };

    /** Persistent match state for reference zstd's greedy and lazy parsers. */
    template <unsigned LazyDepth = 0U, bool BinaryTree = false,
        unsigned WindowLog = 0U, unsigned HashLog = 0U, unsigned ChainLog = 0U,
        unsigned SearchLog = 0U, unsigned MinimumMatch = 0U>
    class greedy_match_state
    {
    public:
        greedy_match_state(unsigned window_log, unsigned hash_log, unsigned chain_log,
            unsigned search_log, unsigned minimum_match, unsigned target_length = 0U)
            : window_log_{window_log}, hash_log_{hash_log}, chain_log_{chain_log},
              search_log_{search_log}, minimum_match_{std::clamp(minimum_match, 4U, 6U)},
              target_length_{target_length},
              hash_table_(std::size_t{1} << match_parameter<HashLog>::resolve(hash_log)),
              chain_table_(std::size_t{1} << match_parameter<ChainLog>::resolve(chain_log)),
              chain_tags_(chain_table_.size()),
              chain_mask_(static_cast<std::uint32_t>(chain_table_.size() - 1U)),
              tree_mask_(chain_mask_ >> 1U)
        {
        }

        void reset()
        {
            auto const window_size{std::uint64_t{1} << parameter_value<WindowLog>(window_log_)};
            auto const next_base{static_cast<std::uint64_t>(next_to_update_) + window_size};
            if (next_base + window_size + index_bias >
                std::numeric_limits<std::uint32_t>::max())
            {
                std::ranges::fill(hash_table_, 0U);
                std::ranges::fill(chain_table_, 0U);
                frame_index_base_ = 0U;
            }
            else
            {
                frame_index_base_ = static_cast<std::uint32_t>(next_base);
            }
            repeat_offsets_ = {1U, 4U, 8U};
            next_to_update_ = frame_index_base_ + index_bias;
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

            if constexpr (LazyDepth == 1U)
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
            if constexpr (LazyDepth == 2U && !BinaryTree)
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
            if constexpr (LazyDepth == 0U && !BinaryTree)
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
            auto const limit{block_end - hash_read_size};
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

    private:
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
            if constexpr (BinaryTree)
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
                    if (target_length_ > 3U && best.length >= target_length_)
                    {
                        break;
                    }
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

        void insert_binary_tree_candidate(std::span<std::uint8_t const> input,
            std::uint32_t current, std::size_t block_end, std::uint32_t attempts,
            std::uint32_t tree_low, std::uint32_t window_low)
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
            while (attempts-- != 0U && match_index > window_low)
            {
#ifdef SPH_ZSTDPP_TRACE_BT
                ++binary_tree_trace.unsorted_visits;
#endif
                auto const next_slot{static_cast<std::size_t>(2U * (match_index & tree_mask))};
                auto const match_position{static_cast<std::size_t>(
                    match_index - frame_index_base_ - index_bias)};
                auto const common{std::min(common_smaller, common_larger)};
                auto const length{common + count_matching_bytes<true>(current_base + common,
                    base + match_position + common, base + block_end)};
#ifdef SPH_ZSTDPP_TRACE_BT
                ++binary_tree_trace.match_extensions;
#endif
                if (current_position + length == block_end)
                {
                    break;
                }
                if (base[match_position + length] < base[current_position + length])
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
        }

        [[nodiscard]] auto find_best_binary_tree_match(std::span<std::uint8_t const> input,
            std::size_t position, std::size_t block_end) -> match_result
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
                insert_binary_tree_candidate(input, match_index, block_end, candidates,
                    unsorted_limit, window_low);
                match_index = next_candidate;
                ++candidates;
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
            match_index = hash_table[hash_slot];
            hash_table[hash_slot] = current;

            while (attempts-- != 0U && match_index > window_low)
            {
#ifdef SPH_ZSTDPP_TRACE_BT
                ++binary_tree_trace.main_visits;
#endif
                auto const next_slot{static_cast<std::size_t>(2U * (match_index & tree_mask))};
                auto const match_position{static_cast<std::size_t>(
                    match_index - frame_index_base_ - index_bias)};
                auto const common{std::min(common_smaller, common_larger)};
                auto const length{common + count_matching_bytes<true>(current_base + common,
                    base + match_position + common, base + block_end)};
#ifdef SPH_ZSTDPP_TRACE_BT
                ++binary_tree_trace.match_extensions;
#endif
                if (length > best.length)
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
                    if (position + length == block_end)
                    {
                        break;
                    }
                }

                if (base[match_position + length] < base[position + length])
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
        unsigned target_length_{};
        match_table hash_table_;
        match_table chain_table_;
        row_tag_table chain_tags_;
        std::uint32_t chain_mask_{};
        std::uint32_t tree_mask_{};
        std::uint32_t frame_index_base_{};
        std::uint32_t next_to_update_{index_bias};
        std::array<std::size_t, 3> repeat_offsets_{1U, 4U, 8U};
        static constexpr std::uint32_t skip_threshold{384U};
        static constexpr std::uint32_t maximum_match_start_updates{96U};
        static constexpr std::uint32_t maximum_match_end_updates{32U};
        static constexpr std::uint32_t unsorted_mark{1U};
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
            bit_container_ = 0U;
            bit_count_ = 0U;
        }

        void reset(std::vector<std::uint8_t>& output) noexcept
        {
            bytes_.clear();
            external_output_ = &output;
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
                auto& output{external_output_ != nullptr ? *external_output_ : bytes_};
                auto const previous_size{output.size()};
                output.resize(previous_size + sizeof(std::uint32_t));
                auto completed{static_cast<std::uint32_t>(bit_container_)};
                if constexpr (std::endian::native == std::endian::big)
                {
                    completed = std::byteswap(completed);
                }
                std::memcpy(output.data() + previous_size, &completed, sizeof(completed));
                bit_container_ >>= 32U;
                bit_count_ -= 32U;
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
                auto& output{external_output_ != nullptr ? *external_output_ : bytes_};
                auto const previous_size{output.size()};
                output.resize(previous_size + sizeof(std::uint32_t));
                auto completed{static_cast<std::uint32_t>(bit_container_)};
                if constexpr (std::endian::native == std::endian::big)
                {
                    completed = std::byteswap(completed);
                }
                std::memcpy(output.data() + previous_size, &completed, sizeof(completed));
                bit_container_ >>= 32U;
                bit_count_ -= 32U;
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
        std::uint64_t bit_container_{};
        unsigned bit_count_{};
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

        void encode(forward_bit_writer& bits, unsigned symbol)
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

        void flush(forward_bit_writer& bits) const
        {
            if (!table_->run_length_symbol)
            {
                bits.append(value_, table_->table_log);
            }
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

    struct compression_workspace
    {
        void reserve(std::size_t block_size, std::size_t maximum_sequences)
        {
            literals.resize(block_size);
            raw_literals.reserve(block_size + 3U);
            literal_codes.resize(maximum_sequences);
            offset_codes.resize(maximum_sequences);
            match_codes.resize(maximum_sequences);
            huffman_body.reserve(block_size);
            huffman_repeat_body.reserve(block_size);
            compressed_weights.reserve(512U);
            huffman_output.reserve(block_size);
            for (auto& description : table_descriptions)
            {
                description.reserve(512U);
            }
            weight_description.reserve(512U);
            weight_bitstream.reserve(512U);
            auto const stream_size{block_size / huffman_streams.size() + 8U};
            for (auto& stream : huffman_streams)
            {
                stream.reserve(stream_size);
            }
        }

        std::vector<std::uint8_t> literals;
        std::vector<std::uint8_t> raw_literals;
        std::vector<std::uint8_t> literal_codes;
        std::vector<std::uint8_t> offset_codes;
        std::vector<std::uint8_t> match_codes;
        forward_bit_writer sequence_bits;
        std::vector<std::uint8_t> huffman_body;
        std::vector<std::uint8_t> huffman_repeat_body;
        std::vector<std::uint8_t> compressed_weights;
        std::vector<std::uint8_t> huffman_output;
        std::array<std::vector<std::uint8_t>, 3> table_descriptions;
        forward_bit_writer weight_bits;
        std::vector<std::uint8_t> weight_description;
        std::vector<std::uint8_t> weight_bitstream;
        std::array<forward_bit_writer, 4> huffman_bits;
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

    template <bool CarefulSelection = false>
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
        auto const& counts{count_symbols(codes, histogram_workspace)};
        unsigned maximum_symbol{};
        for (unsigned symbol{}; symbol < counts.size(); ++symbol)
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

    [[nodiscard]] inline auto encode_huffman_literals(std::span<std::uint8_t const> literals,
        compression_workspace& workspace, std::vector<std::uint8_t>& output) -> bool;

    template <bool CarefulSelection = false>
    inline void encode_sequences_block(std::span<std::uint8_t const> input,
        parsed_block const& parsed, compression_workspace& workspace,
        std::vector<std::uint8_t>& output)
    {
        output.clear();
        if (parsed.sequences.empty())
        {
            return;
        }

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
                literal_output += sequence.literal_length;
            literal_position += sequence.literal_length + sequence.match_length;
            auto const offset_base_value{sequence.repeat_code != 0U ?
                static_cast<std::uint32_t>(sequence.repeat_code) :
                static_cast<std::uint32_t>(sequence.offset + 3U)};
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

        auto& huffman_literals{workspace.huffman_output};
        if (encode_huffman_literals(literal_bytes, workspace, huffman_literals) &&
            huffman_literals.size() + 3U <= raw_literals_size(literal_bytes.size()))
        {
            output.swap(huffman_literals);
        }
        else
        {
            workspace.huffman_repeat_valid = false;
            auto& raw_literals{workspace.raw_literals};
            raw_literals.clear();
            append_raw_literals_header(raw_literals, literal_bytes.size());
            raw_literals.insert(raw_literals.end(), literal_bytes.begin(), literal_bytes.end());
            output.swap(raw_literals);
        }
        auto const literal_table{select_sequence_table<CarefulSelection>(literal_code_bytes,
            literal_length_default_norm, 6U, 9U, true, workspace.table_descriptions[0],
            workspace.sequence_tables[0], workspace.histogram_workspace,
            workspace.sequence_histories[0])};
        auto const offset_table{select_sequence_table<CarefulSelection>(offset_code_bytes,
            offset_default_norm, 5U, 8U,
            *std::max_element(offset_code_bytes.begin(), offset_code_bytes.end()) <= 28U,
            workspace.table_descriptions[1], workspace.sequence_tables[1],
            workspace.histogram_workspace, workspace.sequence_histories[1])};
        auto const match_table{select_sequence_table<CarefulSelection>(match_code_bytes,
            match_length_default_norm, 6U, 9U, true, workspace.table_descriptions[2],
            workspace.sequence_tables[2], workspace.histogram_workspace,
            workspace.sequence_histories[2])};

        append_sequence_count(output, parsed.sequences.size());
        output.push_back(static_cast<std::uint8_t>(
            (static_cast<unsigned>(literal_table.mode) << 6U) |
            (static_cast<unsigned>(offset_table.mode) << 4U) |
            (static_cast<unsigned>(match_table.mode) << 2U)));
        for (auto const& description : workspace.table_descriptions)
        {
            output.insert(output.end(), description.begin(), description.end());
        }

        fse_compression_state literal_state{*literal_table.table};
        fse_compression_state offset_state{*offset_table.table};
        fse_compression_state match_state{*match_table.table};
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

        auto& bits{workspace.sequence_bits};
        bits.reset(output);
        bits.append(last.literal_length, literal_length_bits[last_literal_code]);
        bits.append(last.match_length - 3U, match_length_bits[last_match_code]);
        bits.append(last_offset_base, last_offset_code);
        for (std::size_t index{last_index}; index-- > 0U;)
        {
            auto const& sequence{parsed.sequences[index]};
            auto const literal_code{literal_codes[index]};
            auto const offset_code{offset_codes[index]};
            auto const match_code{match_codes[index]};
            auto const encoded_offset{sequence.repeat_code != 0U ?
                static_cast<std::uint32_t>(sequence.repeat_code) : sequence.offset + 3U};
            offset_state.encode(bits, offset_code);
            match_state.encode(bits, match_code);
            literal_state.encode(bits, literal_code);
            bits.append(sequence.literal_length, literal_length_bits[literal_code]);
            bits.append(sequence.match_length - 3U, match_length_bits[match_code]);
            bits.append(encoded_offset, offset_code);
        }
        match_state.flush(bits);
        offset_state.flush(bits);
        literal_state.flush(bits);
        bits.finish_into(output);
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
            auto const size{static_cast<int>(ranks[rank].current - ranks[rank].base)};
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
        std::size_t literal_count) -> std::pair<std::array<std::uint8_t, 256>, unsigned>
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

        auto const maximum_bits_from_size{static_cast<unsigned>(std::bit_width(literal_count - 1U)) - 2U};
        auto const minimum_bits{std::min(
            static_cast<unsigned>(std::bit_width(literal_count)),
            static_cast<unsigned>(std::bit_width(frequencies.size() - 1U)) + 1U)};
        auto target_bits{std::clamp(maximum_bits_from_size, std::max(5U, minimum_bits), 11U)};
        target_bits = limit_huffman_height(nodes, static_cast<std::uint32_t>(last_non_null), target_bits);

        std::array<std::uint8_t, 256> lengths{};
        for (std::size_t index{}; index < frequencies.size(); ++index)
        {
            lengths[nodes[index].symbol] = nodes[index].number_bits;
        }
        return {lengths, target_bits};
    }

    inline void encode_huffman_stream(std::span<std::uint8_t const> input,
        std::array<huffman_code, 256> const& codes, forward_bit_writer& bits,
        std::vector<std::uint8_t>& output)
    {
        output.clear();
        bits.reset(output);
        auto append_symbol = [&](std::size_t position)
        {
            auto const code{codes[input[position]]};
            if (code.number_bits == 0U)
            {
                throw entropy_error{"missing Zstandard Huffman encoding code"};
            }
            // Canonical Huffman codes are already bounded to number_bits by the
            // table builder, so avoid the generic mask/count checks in the hot
            // literal loop.
            bits.append_unchecked(code.value, code.number_bits);
        };
        auto position{input.size()};
        for (; position >= 4U; position -= 4U)
        {
            append_symbol(position - 1U);
            append_symbol(position - 2U);
            append_symbol(position - 3U);
            append_symbol(position - 4U);
        }
        while (position-- > 0U)
        {
            append_symbol(position);
        }
        bits.finish_into(output);
    }

    /**
     * Builds a length-limited canonical Huffman tree and writes a direct-weight
     * table. Direct tables cover alphabets whose highest used byte is at most
     * 128; larger or excessively skewed alphabets cleanly fall back to raw
     * literals until the FSE weight encoder is available.
     */
    [[nodiscard]] inline auto encode_huffman_literals(std::span<std::uint8_t const> literals,
        compression_workspace& workspace, std::vector<std::uint8_t>& output) -> bool
    {
        output.clear();
        if (literals.size() < 16U)
        {
            return false;
        }
        auto const& frequencies{count_symbols(literals, workspace.histogram_workspace)};
        std::uint8_t maximum_symbol{};
        std::size_t distinct_symbols{};
        for (std::size_t symbol{}; symbol < frequencies.size(); ++symbol)
        {
            if (frequencies[symbol] != 0U)
            {
                ++distinct_symbols;
                maximum_symbol = static_cast<std::uint8_t>(symbol);
            }
        }
        if (distinct_symbols < 2U || maximum_symbol > 128U)
        {
            return false;
        }

        std::array<std::uint8_t, 256> lengths{};
        unsigned table_log{};
        auto const frequency_span{std::span<std::uint32_t const>{frequencies}.first(
            static_cast<std::size_t>(maximum_symbol) + 1U)};
        bool repeat_symbols_valid{workspace.huffman_repeat_valid &&
            workspace.huffman_repeat_maximum_symbol <= 128U};
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
            auto const built{build_reference_huffman_lengths(frequency_span, literals.size())};
            lengths = built.first;
            table_log = built.second;
            workspace.huffman_tree_frequencies = frequencies;
            workspace.huffman_tree_lengths = lengths;
            workspace.huffman_tree_maximum_symbol = maximum_symbol;
            workspace.huffman_tree_literal_size = literals.size();
            workspace.huffman_tree_log = table_log;
            workspace.huffman_tree_valid = true;
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
            return false;
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
                body.push_back(static_cast<std::uint8_t>(127U + explicit_weights));
                for (std::size_t symbol{}; symbol < explicit_weights; symbol += 2U)
                {
                    auto const high{static_cast<std::uint8_t>(weights[symbol] << 4U)};
                    auto const low{symbol + 1U < explicit_weights ? weights[symbol + 1U] : std::uint8_t{0}};
                    body.push_back(static_cast<std::uint8_t>(high | low));
                }
            }
        }

        auto const four_streams{literals.size() >= 1024U};
        if (!four_streams)
        {
            auto& stream{workspace.huffman_streams[0]};
            encode_huffman_stream(literals, codes, workspace.huffman_bits[0], stream);
            body.insert(body.end(), stream.begin(), stream.end());
        }
        else
        {
            auto const segment_size{(literals.size() + 3U) / 4U};
            auto& streams{workspace.huffman_streams};
            for (std::size_t stream{}; stream < 4U; ++stream)
            {
                auto const begin{std::min(literals.size(), stream * segment_size)};
                auto const end{std::min(literals.size(), (stream + 1U) * segment_size)};
                encode_huffman_stream(literals.subspan(begin, end - begin), codes,
                    workspace.huffman_bits[stream], streams[stream]);
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
        if (repeat_symbols_valid && !repeat_tree)
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
                for (unsigned weight{1}; weight <= workspace.huffman_repeat_log; ++weight)
                {
                    auto const code_length{workspace.huffman_repeat_log + 1U - weight};
                    auto const run_length{std::size_t{1} << (weight - 1U)};
                    for (std::size_t symbol{}; symbol <= workspace.huffman_repeat_maximum_symbol; ++symbol)
                    {
                        if (workspace.huffman_repeat_lengths[symbol] != code_length)
                            continue;
                        auto const code{huffman_code{
                            static_cast<std::uint16_t>(repeat_table_position >>
                                (workspace.huffman_repeat_log - code_length)),
                            static_cast<std::uint8_t>(code_length)}};
                        repeat_codes[symbol] = code;
                        workspace.huffman_repeat_codes[symbol] = code;
                        workspace.huffman_repeat_code_lengths[symbol] = code.number_bits;
                        repeat_table_position += run_length;
                    }
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
                    encode_huffman_stream(literals, repeat_codes, workspace.huffman_bits[0], stream);
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
                        encode_huffman_stream(literals.subspan(begin, end - begin), repeat_codes,
                            workspace.huffman_bits[stream], streams[stream]);
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
                auto const h_size{body.size() - new_c_size};
                auto const repeat_profitable{old_c_size < literals.size() &&
                    (old_c_size <= h_size + new_c_size || h_size + 12U >= literals.size())};
                if (repeat_profitable && repeat_body.size() < body.size())
                {
                    body.swap(repeat_body);
                    emit_repeat = true;
                }
            }
        }

        auto const regenerated_size{literals.size()};
        auto const compressed_size{body.size()};
        if (!four_streams && regenerated_size < 1024U && compressed_size < 1024U)
        {
            auto const header{static_cast<std::uint32_t>((regenerated_size << 4U) |
                (compressed_size << 14U) | (emit_repeat ? 3U : 2U))};
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
    inline void encode_single_match_block(std::span<std::uint8_t const> input,
        match const& selected, compression_workspace& workspace,
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
