#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include <sph/zstd++/zstd_decompress.h>

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace
{
    void require(bool condition)
    {
        if (!condition) std::abort();
    }
}

int main()
{
    std::vector<std::uint8_t> input(128U * 1024U);
    for (std::size_t index{}; index < input.size(); ++index)
        input[index] = static_cast<std::uint8_t>((index / 16U + index % 7U) & 255U);
    std::vector<std::uint8_t> frame(ZSTD_compressBound(input.size()));
    auto const size{ZSTD_compress(frame.data(), frame.size(), input.data(), input.size(), 18)};
    require(!ZSTD_isError(size));
    frame.resize(size);
    ZSTD_frameHeader header{};
    require(ZSTD_getFrameHeader(&header, frame.data(), frame.size()) == 0U);
    auto const block{static_cast<std::size_t>(header.headerSize)};
    auto const word{std::uint32_t{frame[block]} | (std::uint32_t{frame[block + 1U]} << 8U) |
        (std::uint32_t{frame[block + 2U]} << 16U)};
    auto const payload{word >> 3U};
    require((word & 7U) == 5U && payload >= 2048U && payload <= 8192U);
    require(header.checksumFlag == 0U && block + 3U + payload == frame.size());

    constexpr std::size_t guard{64U};
    std::vector<std::uint8_t> storage(input.size() + 2U * guard, 0xA5U);
    auto const destination{std::span{storage}.subspan(guard, input.size())};
    std::vector<std::uint8_t> reference(input.size());
    std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context{ZSTD_createDCtx(), &ZSTD_freeDCtx};
    require(context != nullptr);
    auto decoder = sph::zstd::zstd_decompress{[](std::span<std::uint8_t const>) {}};
    std::size_t cases{};
    auto verify = [&](std::span<std::uint8_t const> encoded)
    {
        ++cases;
        auto const reference_size{ZSTD_decompressDCtx(context.get(), reference.data(), reference.size(),
            encoded.data(), encoded.size())};
        decoder.reset();
        bool accepted{};
        try { decoder.decompress_frame(encoded, destination); accepted = true; }
        catch (std::exception const&) {}
        require(std::ranges::all_of(std::span{storage}.first(guard), [](auto b) { return b == 0xA5U; }));
        require(std::ranges::all_of(std::span{storage}.last(guard), [](auto b) { return b == 0xA5U; }));
        if (accepted != (!ZSTD_isError(reference_size) && reference_size == input.size()))
        {
            std::fprintf(stderr, "decoder acceptance mismatch in mutation case %zu\n", cases);
            std::abort();
        }
        if (accepted) require(std::ranges::equal(destination, reference));
    };
    verify(frame);
    require(std::ranges::equal(destination, input));
    auto changed{frame};
    for (std::size_t index{block + 3U}; index < frame.size(); ++index)
    {
        for (unsigned bit{}; bit < 8U; ++bit)
        {
            changed[index] ^= static_cast<std::uint8_t>(1U << bit);
            verify(changed);
            changed[index] ^= static_cast<std::uint8_t>(1U << bit);
        }
    }
    // Shorten the declared block too, so the entropy decoder actually sees the
    // truncated payload instead of stopping at the outer frame-size check.
    for (unsigned removed{1U}; removed <= 32U; ++removed)
    {
        changed = frame;
        changed.resize(frame.size() - removed);
        auto const shortened{((payload - removed) << 3U) | 5U};
        for (unsigned byte{}; byte < 3U; ++byte)
            changed[block + byte] = static_cast<std::uint8_t>(shortened >> (8U * byte));
        verify(changed);
    }
    verify(frame);
    std::printf("scheduled decoder validation passed: %zu cases\n", cases);
}
