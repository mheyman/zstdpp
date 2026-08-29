#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace sph::zstd
{
    inline constexpr std::uint64_t unknown_content_size = std::numeric_limits<std::uint64_t>::max();
    inline constexpr std::size_t maximum_block_size = 128U * 1024U;

    enum class compression_strategy : std::uint8_t
    {
        automatic,
        fast,
        double_fast,
        greedy,
        lazy,
        lazy2,
        binary_tree_lazy2,
        binary_tree_optimal,
        binary_tree_ultra,
        binary_tree_ultra2
    };

    enum class frame_format : std::uint8_t
    {
        standard,
        magicless
    };

    /**
     * Compile-time equivalents of the stable reference compressor parameters.
     * Zero retains Zstandard's parameter-dependent default. Fields which are not
     * used by the baseline fast backend are retained here so additional match-finder
     * and entropy strategies can specialize without changing the public template.
     */
    struct compression_parameters
    {
        int compression_level{3};
        std::uint8_t window_log{};
        std::uint8_t hash_log{};
        std::uint8_t chain_log{};
        std::uint8_t search_log{};
        std::uint8_t minimum_match{};
        std::uint32_t target_length{};
        compression_strategy strategy{compression_strategy::automatic};
        std::uint32_t target_compressed_block_size{};
        bool long_distance_matching{};
        std::uint8_t long_distance_hash_log{};
        std::uint16_t long_distance_minimum_match{};
        std::uint8_t long_distance_bucket_size_log{};
        std::uint8_t long_distance_hash_rate_log{};
        bool content_size{true};
        bool checksum{};
        bool dictionary_id{true};
        std::uint16_t worker_count{};
        std::uint32_t job_size{};
        std::uint8_t overlap_log{};
        std::size_t block_size{maximum_block_size};
        std::uint64_t pledged_source_size{unknown_content_size};
        frame_format format{frame_format::standard};

        constexpr auto operator==(compression_parameters const&) const -> bool = default;
    };

    struct decompression_parameters
    {
        std::uint8_t maximum_window_log{27};
        std::size_t maximum_decoded_block_size{maximum_block_size};
        bool ignore_checksum{};
        frame_format format{frame_format::standard};

        constexpr auto operator==(decompression_parameters const&) const -> bool = default;
    };

    enum class stream_status : std::uint8_t
    {
        ready,
        active,
        finished,
        failed
    };

    enum class error_code : std::uint8_t
    {
        invalid_frame,
        unsupported_frame,
        checksum_mismatch,
        source_size_mismatch,
        truncated_input,
        invalid_state
    };

    class zstd_error : public std::runtime_error
    {
    public:
        explicit zstd_error(error_code code, std::string_view message)
            : std::runtime_error{std::string{message}}, code_{code}
        {
        }

        [[nodiscard]] constexpr auto code() const noexcept -> error_code { return code_; }

    private:
        error_code code_;
    };

    template <typename Callback>
    concept output_callback = std::invocable<Callback&, std::span<std::uint8_t const>>;

    namespace detail
    {
        inline constexpr std::uint32_t frame_magic = 0xFD2FB528U;
        inline constexpr std::uint32_t skippable_magic_start = 0x184D2A50U;
        inline constexpr std::uint32_t skippable_magic_end = 0x184D2A5FU;

        [[nodiscard]] constexpr auto read_u32(std::span<std::uint8_t const> bytes) noexcept -> std::uint32_t
        {
            return static_cast<std::uint32_t>(bytes[0]) |
                (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                (static_cast<std::uint32_t>(bytes[2]) << 16U) |
                (static_cast<std::uint32_t>(bytes[3]) << 24U);
        }

        [[nodiscard]] constexpr auto read_u64(std::span<std::uint8_t const> bytes) noexcept -> std::uint64_t
        {
            return static_cast<std::uint64_t>(read_u32(bytes)) |
                (static_cast<std::uint64_t>(read_u32(bytes.subspan(4))) << 32U);
        }

        constexpr void write_u32(std::span<std::uint8_t> destination, std::uint32_t value) noexcept
        {
            destination[0] = static_cast<std::uint8_t>(value);
            destination[1] = static_cast<std::uint8_t>(value >> 8U);
            destination[2] = static_cast<std::uint8_t>(value >> 16U);
            destination[3] = static_cast<std::uint8_t>(value >> 24U);
        }

        class xxhash64
        {
        public:
            constexpr xxhash64() noexcept { reset(); }

            constexpr void reset() noexcept
            {
                total_size_ = 0;
                buffered_size_ = 0;
                accumulator_1_ = prime_1 + prime_2;
                accumulator_2_ = prime_2;
                accumulator_3_ = 0;
                accumulator_4_ = 0U - prime_1;
            }

            constexpr void update(std::span<std::uint8_t const> input) noexcept
            {
                total_size_ += input.size();
                std::size_t input_offset{};

                if (buffered_size_ + input.size() < stripe_size)
                {
                    for (auto byte : input)
                    {
                        buffer_[buffered_size_++] = byte;
                    }
                    return;
                }

                if (buffered_size_ != 0)
                {
                    auto const needed{stripe_size - buffered_size_};
                    for (std::size_t index{}; index < needed; ++index)
                    {
                        buffer_[buffered_size_ + index] = input[index];
                    }
                    process_stripe(buffer_);
                    buffered_size_ = 0;
                    input_offset = needed;
                }

                while (input.size() - input_offset >= stripe_size)
                {
                    process_stripe(input.subspan(input_offset, stripe_size));
                    input_offset += stripe_size;
                }

                while (input_offset < input.size())
                {
                    buffer_[buffered_size_++] = input[input_offset++];
                }
            }

            [[nodiscard]] constexpr auto digest() const noexcept -> std::uint64_t
            {
                std::uint64_t hash{};
                if (total_size_ >= stripe_size)
                {
                    hash = std::rotl(accumulator_1_, 1) + std::rotl(accumulator_2_, 7) +
                        std::rotl(accumulator_3_, 12) + std::rotl(accumulator_4_, 18);
                    hash = merge(hash, accumulator_1_);
                    hash = merge(hash, accumulator_2_);
                    hash = merge(hash, accumulator_3_);
                    hash = merge(hash, accumulator_4_);
                }
                else
                {
                    hash = prime_5;
                }

                hash += total_size_;
                std::size_t offset{};
                auto const remaining{std::span<std::uint8_t const>{buffer_}.first(buffered_size_)};
                while (remaining.size() - offset >= 8U)
                {
                    auto const lane{round(0, read_u64(remaining.subspan(offset, 8)))};
                    hash ^= lane;
                    hash = std::rotl(hash, 27) * prime_1 + prime_4;
                    offset += 8U;
                }
                if (remaining.size() - offset >= 4U)
                {
                    hash ^= static_cast<std::uint64_t>(read_u32(remaining.subspan(offset, 4))) * prime_1;
                    hash = std::rotl(hash, 23) * prime_2 + prime_3;
                    offset += 4U;
                }
                while (offset < remaining.size())
                {
                    hash ^= static_cast<std::uint64_t>(remaining[offset++]) * prime_5;
                    hash = std::rotl(hash, 11) * prime_1;
                }

                hash ^= hash >> 33U;
                hash *= prime_2;
                hash ^= hash >> 29U;
                hash *= prime_3;
                hash ^= hash >> 32U;
                return hash;
            }

        private:
            static constexpr std::size_t stripe_size = 32;
            static constexpr std::uint64_t prime_1 = 11400714785074694791ULL;
            static constexpr std::uint64_t prime_2 = 14029467366897019727ULL;
            static constexpr std::uint64_t prime_3 = 1609587929392839161ULL;
            static constexpr std::uint64_t prime_4 = 9650029242287828579ULL;
            static constexpr std::uint64_t prime_5 = 2870177450012600261ULL;

            [[nodiscard]] static constexpr auto round(std::uint64_t accumulator, std::uint64_t lane) noexcept -> std::uint64_t
            {
                accumulator += lane * prime_2;
                accumulator = std::rotl(accumulator, 31);
                return accumulator * prime_1;
            }

            [[nodiscard]] static constexpr auto merge(std::uint64_t hash, std::uint64_t value) noexcept -> std::uint64_t
            {
                hash ^= round(0, value);
                return hash * prime_1 + prime_4;
            }

            constexpr void process_stripe(std::span<std::uint8_t const> stripe) noexcept
            {
                accumulator_1_ = round(accumulator_1_, read_u64(stripe));
                accumulator_2_ = round(accumulator_2_, read_u64(stripe.subspan(8)));
                accumulator_3_ = round(accumulator_3_, read_u64(stripe.subspan(16)));
                accumulator_4_ = round(accumulator_4_, read_u64(stripe.subspan(24)));
            }

            std::uint64_t total_size_{};
            std::uint64_t accumulator_1_{};
            std::uint64_t accumulator_2_{};
            std::uint64_t accumulator_3_{};
            std::uint64_t accumulator_4_{};
            std::array<std::uint8_t, stripe_size> buffer_{};
            std::size_t buffered_size_{};
        };

        [[nodiscard]] constexpr auto xxhash(std::span<std::uint8_t const> input) noexcept -> std::uint64_t
        {
            xxhash64 hash;
            hash.update(input);
            return hash.digest();
        }
    }
}
