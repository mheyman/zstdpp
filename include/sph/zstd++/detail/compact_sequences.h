#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

namespace sph::zstd::detail
{
    // Logical values for the fast strategy only; persistent records remain 8 bytes.
    struct fast_parsed_sequence {
        constexpr fast_parsed_sequence() = default;
        constexpr fast_parsed_sequence(std::size_t ll, std::size_t ml, std::size_t off, std::uint8_t rep) noexcept
            : literal_length{static_cast<std::uint32_t>(ll)}, match_length{static_cast<std::uint32_t>(ml) & 0x3FFFFFFFU},
              repeat_code{rep & 3U}, offset{static_cast<std::uint32_t>(off)} {}
        std::uint32_t literal_length{}, match_length{}, repeat_code{}, offset{};
        constexpr auto operator==(fast_parsed_sequence const&) const -> bool = default;
    };
    static_assert(sizeof(fast_parsed_sequence) == 16U);
    // Included within sph::zstd::detail after fast_parsed_sequence.
    struct compact_sequence {
        std::uint32_t offset_base;
        std::uint16_t literals;
        std::uint16_t match_base;
    };
    static_assert(sizeof(compact_sequence) == 8U);

    struct compact_sequence_view {
        compact_sequence const* records{};
        fast_parsed_sequence const* logical{};
        compact_sequence const* exception{};
        bool long_literals{};
        std::size_t count{};

        compact_sequence_view() = default;
        compact_sequence_view(std::span<fast_parsed_sequence const> source) noexcept
            : logical{source.data()}, count{source.size()} {}
        template <std::size_t N>
        compact_sequence_view(std::array<fast_parsed_sequence, N> const& source) noexcept
            : compact_sequence_view{std::span<fast_parsed_sequence const>{source}} {}
        compact_sequence_view(std::vector<fast_parsed_sequence> const& source) noexcept
            : compact_sequence_view{std::span<fast_parsed_sequence const>{source}} {}

        auto size() const noexcept { return count; }
        auto empty() const noexcept { return count == 0U; }
        auto data() const noexcept { return records; }
        auto operator[](std::size_t index) const noexcept -> fast_parsed_sequence {
            if (logical != nullptr) return logical[index];
            auto const& value = records[index];
            auto const extended = records + index == exception;
            return {static_cast<std::uint32_t>(value.literals) + (extended && long_literals ? 65536U : 0U),
                static_cast<std::uint32_t>(value.match_base) + 3U + (extended && !long_literals ? 65536U : 0U),
                value.offset_base >= 4U ? value.offset_base - 3U : 0U,
                static_cast<std::uint8_t>(value.offset_base < 4U ? value.offset_base : 0U)};
        }
        auto subspan(std::size_t offset, std::size_t length) const noexcept {
            assert(offset <= count && length <= count - offset);
            auto result = *this;
            if (records != nullptr) result.records += offset;
            if (logical != nullptr) result.logical += offset;
            result.count = length;
            return result;
        }
        auto first(std::size_t length) const noexcept { return subspan(0U, length); }
        struct iterator {
            compact_sequence_view const* source;
            std::size_t index;
            auto operator*() const noexcept { return (*source)[index]; }
            auto& operator++() noexcept { ++index; return *this; }
            auto operator==(iterator const&) const -> bool = default;
        };
        auto begin() const noexcept { return iterator{this, 0U}; }
        auto end() const noexcept { return iterator{this, count}; }
    };

    struct compact_sequence_store {
        std::vector<compact_sequence> records;
        std::size_t exception_index{static_cast<std::size_t>(-1)};
        bool long_literals{};
        compact_sequence_store() = default;
        compact_sequence_store(std::initializer_list<fast_parsed_sequence> values) {
            reserve(values.size());
            for (auto const& value : values) push_back(value);
        }
        auto size() const noexcept { return records.size(); }
        auto empty() const noexcept { return records.empty(); }
        auto data() const noexcept { return records.data(); }
        void reserve(std::size_t count) { records.reserve(count); }
        void clear() noexcept { records.clear(); exception_index = static_cast<std::size_t>(-1); long_literals = false; }
        operator compact_sequence_view() const noexcept {
            compact_sequence_view view;
            view.records = records.data();
            view.exception = exception_index < records.size() ? records.data() + exception_index : nullptr;
            view.long_literals = long_literals;
            view.count = records.size();
            return view;
        }
        auto operator[](std::size_t index) const noexcept { return static_cast<compact_sequence_view>(*this)[index]; }
        auto front() const noexcept { return (*this)[0U]; }
        auto back() const noexcept { return (*this)[size() - 1U]; }
        void push_back(fast_parsed_sequence const& value) {
            assert(value.match_length >= 3U && value.match_length <= 131072U && value.literal_length <= 131069U);
            auto const match_base = value.match_length - 3U;
            if (value.literal_length > 65535U || match_base > 65535U) {
                assert(exception_index == static_cast<std::size_t>(-1));
                exception_index = size();
                long_literals = value.literal_length > 65535U;
            }
            records.push_back({value.repeat_code != 0U ? value.repeat_code : value.offset + 3U,
                static_cast<std::uint16_t>(value.literal_length), static_cast<std::uint16_t>(match_base)});
        }
        void append(std::vector<fast_parsed_sequence> const& values) {
            for (auto const& value : values) push_back(value);
        }
        void resize(std::size_t count, fast_parsed_sequence const& value) {
            if (count < size()) {
                records.resize(count);
                if (exception_index >= count) exception_index = static_cast<std::size_t>(-1);
            }
            while (size() < count) push_back(value);
        }
        struct iterator {
            compact_sequence_store const* source;
            std::size_t index;
            auto operator*() const noexcept { return (*source)[index]; }
            auto& operator++() noexcept { ++index; return *this; }
            auto operator==(iterator const&) const -> bool = default;
        };
        auto begin() const noexcept { return iterator{this, 0U}; }
        auto end() const noexcept { return iterator{this, size()}; }
        auto operator==(compact_sequence_store const& other) const noexcept -> bool {
            if (size() != other.size()) return false;
            for (std::size_t i{}; i < size(); ++i) if ((*this)[i] != other[i]) return false;
            return true;
        }
    };


    struct compact_parsed_block {
        using sequence_type = fast_parsed_sequence;
        compact_sequence_store sequences;
        std::size_t input_begin{}, trailing_literal_position{}, trailing_literal_length{};
    };
    struct compact_parsed_block_view {
        compact_sequence_view sequences;
        std::size_t input_begin{}, trailing_literal_position{}, trailing_literal_length{};
        compact_parsed_block_view(compact_parsed_block const& block) noexcept
            : sequences{block.sequences}, input_begin{block.input_begin},
              trailing_literal_position{block.trailing_literal_position}, trailing_literal_length{block.trailing_literal_length} {}
    };



}
