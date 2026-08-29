#include <sph/zstd++/zstd_compress.h>
#include <sph/zstd++/zstd_decompress.h>

#include "eval_corpus.h"

#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    auto failures = 0;

    void check(bool condition, std::string_view message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    template <typename Function>
    void check_error(Function&& function, sph::zstd::error_code expected, std::string_view message)
    {
        try
        {
            function();
            check(false, message);
        }
        catch (sph::zstd::zstd_error const& error)
        {
            check(error.code() == expected, message);
        }
    }

    constexpr auto small_blocks = []
    {
        auto parameters = sph::zstd::compression_parameters{};
        parameters.block_size = 17;
        return parameters;
    }();

    constexpr auto checked_content = []
    {
        auto parameters = small_blocks;
        parameters.checksum = true;
        parameters.pledged_source_size = 257;
        return parameters;
    }();

    auto make_input(std::size_t size) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> input(size);
        for (std::size_t index{}; index < input.size(); ++index)
        {
            input[index] = static_cast<std::uint8_t>((index * 37U + index / 11U) & 0xFFU);
        }
        return input;
    }

    auto read_file(std::filesystem::path const& path) -> std::vector<std::uint8_t>
    {
        std::ifstream input{path, std::ios::binary};
        if (!input)
        {
            throw std::runtime_error{"cannot open reference test file: " + path.string()};
        }
        return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    template <sph::zstd::compression_parameters Parameters = {}>
    auto compress(std::span<std::uint8_t const> input) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> encoded;
        auto compressor = sph::zstd::make_zstd_compress<Parameters>(
            [&encoded](std::span<std::uint8_t const> output)
            {
                encoded.insert(encoded.end(), output.begin(), output.end());
            });
        compressor.update(input);
        compressor.finish();
        check(compressor.status() == sph::zstd::stream_status::finished, "compressor reaches finished state");
        check(compressor.source_size() == input.size(), "compressor reports source size");
        check(compressor.encoded_size() == encoded.size(), "compressor reports encoded size");
        return encoded;
    }

    auto reference_decompress(std::span<std::uint8_t const> encoded, std::size_t expected_size)
        -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> decoded(expected_size);
        auto const result{ZSTD_decompress(decoded.data(), decoded.size(), encoded.data(), encoded.size())};
        check(ZSTD_isError(result) == 0U, "reference zstd accepts sph::zstd output");
        if (ZSTD_isError(result) != 0U)
        {
            std::cerr << "  reference error: " << ZSTD_getErrorName(result) << '\n';
        }
        if (ZSTD_isError(result) == 0U)
        {
            check(result == expected_size, "reference zstd reports expected decoded size");
            decoded.resize(result);
        }
        return decoded;
    }

    auto reference_compress(std::span<std::uint8_t const> input, int level) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> encoded(ZSTD_compressBound(input.size()));
        auto const result{ZSTD_compress(encoded.data(), encoded.size(), input.data(), input.size(), level)};
        check(ZSTD_isError(result) == 0U, "reference zstd creates entropy-coded test frame");
        if (ZSTD_isError(result) != 0U)
        {
            throw std::runtime_error{ZSTD_getErrorName(result)};
        }
        encoded.resize(result);
        return encoded;
    }

    auto make_entropy_input() -> std::vector<std::uint8_t>
    {
        constexpr std::string_view paragraph{
            "Header-only compression makes specialization visible to the optimizer. "
            "Huffman coding handles the literals while FSE reconstructs match sequences.\n"};
        std::vector<std::uint8_t> input;
        input.reserve(600U * 1024U);
        for (std::size_t index{}; input.size() < 600U * 1024U; ++index)
        {
            for (auto const character : paragraph)
            {
                input.push_back(static_cast<std::uint8_t>(character));
            }
            input.push_back(static_cast<std::uint8_t>(index));
            input.push_back(static_cast<std::uint8_t>(index >> 8U));
        }
        input.resize(600U * 1024U);
        return input;
    }

    auto make_mixed_entropy_input() -> std::vector<std::uint8_t>
    {
        return sph::zstd::eval::make_mixed_corpus(1024U * 1024U);
    }

    void test_reference_interoperability()
    {
        // Adapted from the reference zstreamtest round-trip and tiny-chunk cases.
        auto const input{make_input(257)};
        auto const encoded{compress<checked_content>(input)};
        check(ZSTD_getFrameContentSize(encoded.data(), encoded.size()) == input.size(),
            "pledged content size is present in the frame header");
        check(reference_decompress(encoded, input.size()) == input,
            "reference zstd round-trips a checksummed frame");

        std::vector<std::uint8_t> decoded;
        auto decompressor = sph::zstd::zstd_decompress{
            [&decoded](std::span<std::uint8_t const> output)
            {
                decoded.insert(decoded.end(), output.begin(), output.end());
            }};
        for (auto byte : encoded)
        {
            decompressor.update(byte);
        }
        decompressor.finish();
        check(decoded == input, "byte-at-a-time decompression round-trips");
        check(decompressor.frame_count() == 1, "decompressor counts completed frames");
        check(decompressor.last_frame().checksum, "decompressor exposes frame checksum flag");
        check(decompressor.last_frame().content_size == input.size(), "decompressor exposes content size");
    }

    void test_rle_blocks()
    {
        std::vector<std::uint8_t> const input(400, 0xA5U);
        auto const encoded{compress<small_blocks>(input)};
        check(encoded.size() < input.size(), "RLE blocks reduce repeated input");
        check(reference_decompress(encoded, input.size()) == input, "reference zstd accepts RLE blocks");
    }

    void test_reference_rle_golden_frame()
    {
        // This unmodified reference corpus frame expands eight maximum-size RLE blocks to 1 MiB.
        auto const encoded{read_file(std::filesystem::path{SPH_ZSTDPP_REFERENCE_TEST_DIR} /
            "golden-decompression" / "rle-first-block.zst")};
        std::vector<std::uint8_t> decoded;
        auto decompressor = sph::zstd::zstd_decompress{
            [&decoded](std::span<std::uint8_t const> output)
            {
                decoded.insert(decoded.end(), output.begin(), output.end());
            }};
        for (auto chunk : std::span<std::uint8_t const>{encoded} | std::views::chunk(7))
        {
            decompressor.update(chunk);
        }
        decompressor.finish();
        check(decoded.size() == 1024U * 1024U, "reference RLE golden frame expands to 1 MiB");
        check(std::ranges::all_of(decoded, [](std::uint8_t byte) { return byte == 0; }),
            "reference RLE golden frame expands to zero bytes");
        check(decompressor.last_frame().checksum, "reference RLE golden frame checksum is validated");
    }

    auto decode_reference_golden(std::string_view file_name) -> std::vector<std::uint8_t>
    {
        auto const encoded{read_file(std::filesystem::path{SPH_ZSTDPP_REFERENCE_TEST_DIR} /
            "golden-decompression" / file_name)};
        std::vector<std::uint8_t> decoded;
        auto decompressor = sph::zstd::zstd_decompress{
            [&decoded](std::span<std::uint8_t const> output)
            {
                decoded.insert(decoded.end(), output.begin(), output.end());
            }};
        decompressor.update(encoded);
        decompressor.finish();
        return decoded;
    }

    void test_reference_zero_sequence_golden_frames()
    {
        // These reference files use compressed-block framing but no FSE match sequences.
        check(decode_reference_golden("empty-block.zst").empty(),
            "reference empty compressed block decodes");
        auto const decoded{decode_reference_golden("zeroSeq_2B.zst")};
        constexpr std::string_view expected{"Hello World!\n"};
        check(std::ranges::equal(decoded, expected, {},
            [](std::uint8_t byte) { return static_cast<char>(byte); }, std::identity{}),
            "reference two-byte zero-sequence block decodes raw literals");
    }

    void test_reference_entropy_frames()
    {
        auto const input{make_entropy_input()};
        for (auto const level : {1, 3, 9})
        {
            auto const encoded{reference_compress(input, level)};
            check(encoded.size() < input.size() / 4U, "reference entropy frame is genuinely compressed");

            std::vector<std::uint8_t> decoded;
            auto decompressor = sph::zstd::zstd_decompress{
                [&decoded](std::span<std::uint8_t const> output)
                {
                    decoded.insert(decoded.end(), output.begin(), output.end());
                }};
            for (auto chunk : std::span<std::uint8_t const>{encoded} | std::views::chunk(13))
            {
                decompressor.update(chunk);
            }
            decompressor.finish();
            check(decoded == input, "Huffman/FSE reference frame decodes at each tested level");
            check(decompressor.decoded_size() == input.size(), "entropy decoder reports decoded size");
        }

        auto const mixed_input{make_mixed_entropy_input()};
        auto const mixed_encoded{reference_compress(mixed_input, 1)};
        std::vector<std::uint8_t> mixed_decoded;
        auto mixed_decompressor = sph::zstd::zstd_decompress{
            [&mixed_decoded](std::span<std::uint8_t const> output)
            {
                mixed_decoded.insert(mixed_decoded.end(), output.begin(), output.end());
            }};
        mixed_decompressor.update(mixed_encoded);
        mixed_decompressor.finish();
        check(mixed_decoded == mixed_input,
            "low-probability FSE normalized counts decode in the mixed evaluation corpus");
    }

    void test_cpp_match_compression()
    {
        constexpr std::string_view pattern{"fast-match-finder entropy sequence "};
        std::vector<std::uint8_t> input;
        input.reserve(300U * 1024U);
        while (input.size() < 300U * 1024U)
        {
            input.insert(input.end(), pattern.begin(), pattern.end());
        }
        input.resize(300U * 1024U);
        auto const encoded{compress(input)};
        check(encoded.size() < input.size() / 3U, "fast match finder materially reduces repetitive input");
        check(reference_decompress(encoded, input.size()) == input,
            "reference zstd accepts sph::zstd sequence entropy blocks");

        std::vector<std::uint8_t> decoded;
        auto decompressor = sph::zstd::zstd_decompress{
            [&decoded](std::span<std::uint8_t const> output)
            {
                decoded.insert(decoded.end(), output.begin(), output.end());
            }};
        decompressor.update(encoded);
        decompressor.finish();
        check(decoded == input, "sph::zstd decodes its own sequence entropy blocks");
    }

    void test_cpp_huffman_literal_compression()
    {
        std::vector<std::uint8_t> input{1, 2, 3, 4};
        input.reserve(6000U);
        std::uint32_t random{0x12345678U};
        while (input.size() < 5000U)
        {
            random = random * 1664525U + 1013904223U;
            input.push_back(static_cast<std::uint8_t>('a' + (random >> 29U)));
        }
        input.insert(input.end(), input.begin(), input.begin() + 1000);

        auto const encoded{compress(input)};
        check(encoded.size() < 4000U, "Huffman literals reduce a skewed direct-weight alphabet");
        check(reference_decompress(encoded, input.size()) == input,
            "reference zstd accepts sph::zstd Huffman literal output");

        std::vector<std::uint8_t> decoded;
        auto decompressor = sph::zstd::zstd_decompress{
            [&decoded](std::span<std::uint8_t const> output)
            {
                decoded.insert(decoded.end(), output.begin(), output.end());
            }};
        decompressor.update(encoded);
        decompressor.finish();
        check(decoded == input, "sph::zstd decodes its own Huffman literal output");
    }

    void test_invalid_match_history()
    {
        // One sequence requests a three-byte match at offset four before any output exists.
        std::array<std::uint8_t, 16> const encoded{
            0x28, 0xB5, 0x2F, 0xFD, 0x00, 0x00, // frame and window header
            0x3D, 0x00, 0x00,                   // final compressed block, seven-byte payload
            0x00,                               // zero raw literals
            0x01, 0x54, 0x00, 0x02, 0x00,       // one sequence; RLE LL/OF/ML codes
            0x07                                // offset extra bits 11 and end marker
        };
        auto sink = [](std::span<std::uint8_t const>) {};
        auto decompressor = sph::zstd::zstd_decompress{sink};
        check_error([&decompressor, &encoded] { decompressor.update(encoded); },
            sph::zstd::error_code::invalid_frame,
            "match offsets outside the bounded history window are rejected");
    }

    void test_concatenated_and_skippable_frames()
    {
        auto const first_input{make_input(49)};
        std::vector<std::uint8_t> const second_input(31, 0x7CU);
        auto stream{compress<small_blocks>(first_input)};

        std::array<std::uint8_t, 11> const skippable{
            0x50, 0x2A, 0x4D, 0x18, // skippable-frame magic
            0x03, 0x00, 0x00, 0x00, // payload size
            0x11, 0x22, 0x33
        };
        stream.insert(stream.end(), skippable.begin(), skippable.end());
        auto const second_frame{compress<small_blocks>(second_input)};
        stream.insert(stream.end(), second_frame.begin(), second_frame.end());

        std::vector<std::uint8_t> decoded;
        auto decompressor = sph::zstd::zstd_decompress{
            [&decoded](std::span<std::uint8_t const> output)
            {
                decoded.insert(decoded.end(), output.begin(), output.end());
            }};
        decompressor.update(stream);
        decompressor.finish();

        auto expected{first_input};
        expected.insert(expected.end(), second_input.begin(), second_input.end());
        check(decoded == expected, "concatenated frames decode around a skippable frame");
        check(decompressor.frame_count() == 2, "only data frames contribute to frame count");
    }

    void test_error_reporting()
    {
        auto const input{make_input(257)};
        auto encoded{compress<checked_content>(input)};

        auto sink = [](std::span<std::uint8_t const>) {};
        auto truncated = sph::zstd::zstd_decompress{sink};
        truncated.update(std::span<std::uint8_t const>{encoded}.first(encoded.size() - 1U));
        check_error([&truncated] { truncated.finish(); }, sph::zstd::error_code::truncated_input,
            "finish reports truncated content checksum");

        encoded.back() ^= 0x80U;
        auto corrupt = sph::zstd::zstd_decompress{sink};
        check_error([&corrupt, &encoded] { corrupt.update(encoded); }, sph::zstd::error_code::checksum_mismatch,
            "corrupt content checksum is rejected");
    }

    void test_flush_and_reset()
    {
        std::vector<std::uint8_t> encoded;
        auto compressor = sph::zstd::make_zstd_compress<small_blocks>(
            [&encoded](std::span<std::uint8_t const> output)
            {
                encoded.insert(encoded.end(), output.begin(), output.end());
            });
        compressor.update(std::uint8_t{42});
        compressor.flush();
        compressor.update(std::uint8_t{43});
        compressor.finish();
        std::array<std::uint8_t, 2> const expected{42, 43};
        check(reference_decompress(encoded, expected.size()) == std::vector<std::uint8_t>(expected.begin(), expected.end()),
            "flush preserves a valid open frame");

        compressor.reset();
        check(compressor.status() == sph::zstd::stream_status::ready, "reset restores ready state");
    }
}

int main()
{
    try
    {
        test_reference_interoperability();
        test_rle_blocks();
        test_reference_rle_golden_frame();
        test_reference_zero_sequence_golden_frames();
        test_reference_entropy_frames();
        test_cpp_match_compression();
        test_cpp_huffman_literal_compression();
        test_invalid_match_history();
        test_concatenated_and_skippable_frames();
        test_error_reporting();
        test_flush_and_reset();
    }
    catch (std::exception const& error)
    {
        ++failures;
        std::cerr << "UNEXPECTED EXCEPTION: " << error.what() << '\n';
    }

    if (failures == 0)
    {
        std::cout << "All sph-zstd++ tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
