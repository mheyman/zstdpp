#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include <sph/zstd++/zstd_decompress.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <source_location>

namespace
{
    void require(bool condition, std::source_location where = std::source_location::current())
    {
        if (!condition)
        {
            std::fprintf(stderr, "%s:%u: validation failed\n", where.file_name(),
                where.line());
            std::abort();
        }
    }
}

std::size_t validate_level(int level)
{
    // The 1 MiB level-16 fixture selects the small split blocks used by the
    // benchmark; a 128 KiB source resolves different compression parameters.
    std::vector<std::uint8_t> input((level == 16 ? 1024U : 128U) * 1024U);
    for (std::size_t index{}; index < input.size(); ++index)
        input[index] = static_cast<std::uint8_t>((index / 16U + index % 7U) & 255U);
    std::vector<std::uint8_t> frame(ZSTD_compressBound(input.size()));
    auto const size{ZSTD_compress(frame.data(), frame.size(), input.data(), input.size(), level)};
    require(!ZSTD_isError(size));
    frame.resize(size);
    ZSTD_frameHeader header{};
    require(ZSTD_getFrameHeader(&header, frame.data(), frame.size()) == 0U);
    struct block_info { std::size_t offset; std::uint32_t word; std::uint32_t payload; };
    std::vector<block_info> blocks;
    auto cursor{static_cast<std::size_t>(header.headerSize)};
    for (;;)
    {
        require(cursor + 3U <= frame.size());
        auto const word{std::uint32_t{frame[cursor]} | (std::uint32_t{frame[cursor + 1U]} << 8U) |
            (std::uint32_t{frame[cursor + 2U]} << 16U)};
        auto const payload{word >> 3U};
        require(((word >> 1U) & 3U) == 2U && payload <= frame.size() - cursor - 3U);
        blocks.push_back({cursor, word, payload});
        cursor += 3U + payload;
        if ((word & 1U) != 0U) break;
    }
    require(header.checksumFlag == 0U && cursor == frame.size());
    if (level == 18)
        require(blocks.size() == 1U && blocks[0].payload >= 2048U && blocks[0].payload <= 8192U);
    else
        require(blocks.size() > 1U && std::ranges::all_of(blocks,
            [](auto const& block) { return block.payload < 2048U; }));

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
            std::fprintf(stderr, "decoder acceptance mismatch at level %d in mutation case %zu\n", level, cases);
            std::abort();
        }
        if (accepted) require(std::ranges::equal(destination, reference));
    };
    verify(frame);
    require(std::ranges::equal(destination, input));
    auto changed{frame};
    for (auto const& block : blocks)
    {
        changed = frame;
        for (auto index{block.offset + 3U}; index < block.offset + 3U + block.payload; ++index)
        {
            for (unsigned bit{}; bit < 8U; ++bit)
            {
                changed[index] ^= static_cast<std::uint8_t>(1U << bit);
                verify(changed);
                changed[index] ^= static_cast<std::uint8_t>(1U << bit);
            }
        }
        // Keep subsequent blocks intact while exposing this block's shortened
        // payload to the entropy decoder, rather than the outer size check.
        for (unsigned removed{1U}; removed <= std::min(32U, block.payload); ++removed)
        {
            changed = frame;
            auto const end{block.offset + 3U + block.payload};
            changed.erase(changed.begin() + static_cast<std::ptrdiff_t>(end - removed),
                changed.begin() + static_cast<std::ptrdiff_t>(end));
            auto const shortened{((block.payload - removed) << 3U) | (block.word & 7U)};
            for (unsigned byte{}; byte < 3U; ++byte)
                changed[block.offset + byte] = static_cast<std::uint8_t>(shortened >> (8U * byte));
            verify(changed);
        }
    }
    verify(frame);
    return cases;
}

int main()
{
    auto const cases{validate_level(18) + validate_level(16)};
    std::printf("scheduled decoder validation passed: %zu cases\n", cases);
}
