#include <sph/zstd++/detail/zstd_compression.h>
#include <sph/zstd++/zstd_compress.h>
#include <sph/zstd++/zstd_decompress.h>
#include <zstd.h>

extern "C" {
#include "huf.h"
}

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void test_sample_boundaries()
{
    sph::zstd::detail::symbol_count_workspace workspace;
    std::size_t cases{};
    for (std::size_t size : {0U, 4095U, 4096U, 8192U, 40959U, 40960U, 40961U, 67585U, 131072U})
    for (unsigned first_max : {16U, 33U, 34U, 35U, 64U})
    for (unsigned last_max : {16U, 33U, 34U, 35U, 64U})
    for (unsigned last_symbol : {0U, 1U})
    {
        std::vector<std::uint8_t> input(size, 0U);
        if (size >= 8192U)
        {
            auto fill_sample = [&](std::size_t start, unsigned maximum, unsigned symbol)
            {
                for (std::size_t i{}; i < 4096U; ++i)
                    input[start + i] = static_cast<std::uint8_t>((i % 256U) ^ symbol);
                for (unsigned i{1U}; i <= maximum - 16U; ++i)
                    input[start + i] = static_cast<std::uint8_t>(symbol);
            };
            fill_sample(0U, first_max, 0U);
            fill_sample(size - 4096U, last_max, last_symbol);
        }
        for (std::size_t sequences : {std::size_t{}, std::size_t{1U}, size / 20U, size / 20U + 1U})
        {
            for (auto& lane : workspace) lane.fill(0xdeadbeefU);
            bool const expected = size >= 40960U &&
                (sequences == 0U || size / sequences >= 20U) && first_max + last_max <= 68U;
            auto const actual = sph::zstd::detail::skip_huffman_from_samples(input, sequences, workspace);
            if (actual != expected) throw std::runtime_error("literal sample threshold mismatch");
            if (workspace[1][0] != 0xdeadbeefU) throw std::runtime_error("unrelated scratch lane changed");
            ++cases;
        }
    }
    std::cout << cases << " literal-sampling boundary cases passed\n";
}



void test_reference_history()
{
    std::array<HUF_CElt, HUF_CTABLE_SIZE_ST(255)> original{};
    std::array<std::uint64_t, HUF_WORKSPACE_SIZE / sizeof(std::uint64_t)> scratch{};
    std::vector<std::uint8_t> seed(65536U);
    for (std::size_t i{}; i < seed.size(); ++i)
        seed[i] = i % 4U ? 0U : static_cast<std::uint8_t>(i / 4U);
    std::vector<std::uint8_t> output(HUF_COMPRESSBOUND(131072U));
    HUF_repeat initial{HUF_repeat_none};
    auto const built = HUF_compress4X_repeat(output.data(), output.size(), seed.data(), seed.size(),
        255U, 11U, scratch.data(), sizeof(scratch), original.data(), &initial, 0);
    if (HUF_isError(built) || built <= 1U || built >= seed.size())
        throw std::runtime_error("could not build reference history table");
    sph::zstd::detail::symbol_count_workspace histogram{};
    std::size_t cases{};
    for (std::size_t size : {40960U, 40961U, 67585U, 131072U})
    for (unsigned last_symbol : {0U, 1U})
    for (auto mode : {HUF_repeat_none, HUF_repeat_check, HUF_repeat_valid})
    {
        // Highly compressible middle intentionally verifies that sampling,
        // not a full-input entropy decision, matches reference behavior.
        std::vector<std::uint8_t> input(size, 42U);
        for (std::size_t i{}; i < 4096U; ++i)
        {
            input[i] = static_cast<std::uint8_t>(i % 256U);
            input[size - 4096U + i] = static_cast<std::uint8_t>((i % 256U) ^ last_symbol);
        }
        for (unsigned i{1U}; i <= 18U; ++i)
        {
            input[i] = 0U;
            input[size - 4096U + i] = static_cast<std::uint8_t>(last_symbol);
        }
        if (!sph::zstd::detail::skip_huffman_from_samples(input, 1U, histogram))
            throw std::runtime_error("candidate did not reject maximum-sum 68");
        auto table = original;
        auto repeat = mode;
        auto const result = HUF_compress4X_repeat(output.data(), output.size(), input.data(), input.size(),
            255U, 11U, scratch.data(), sizeof(scratch), table.data(), &repeat,
            HUF_flags_suspectUncompressible);
        if (result != 0U || table != original || repeat != mode)
            throw std::runtime_error("reference sampling rejection/history mismatch");
        ++cases;
    }
    std::cout << cases << " reference sampling/history cases passed\n";
}



void test_cpp_history()
{
    using namespace sph::zstd::detail;
    std::size_t cases{};
    for (std::size_t size : {40960U, 40961U, 67585U, 130000U})
    for (unsigned last_symbol : {0U, 1U})
    {
        compression_workspace workspace;
        workspace.reserve(131072U, 1U);
        std::vector<std::uint8_t> seed(65536U);
        for (std::size_t i{}; i < seed.size(); ++i)
            seed[i] = static_cast<std::uint8_t>(i % 4U ? 0U : (i / 4U) % 256U);
        std::vector<std::uint8_t> middle(size + 3U, 42U);
        for (std::size_t i{}; i < 4096U; ++i)
        {
            middle[i] = static_cast<std::uint8_t>(i % 256U);
            middle[size - 4096U + i] = static_cast<std::uint8_t>((i % 256U) ^ last_symbol);
        }
        std::copy_n(middle.begin(), 3U, middle.begin() + static_cast<std::ptrdiff_t>(size));
        std::vector<std::uint8_t> expected = seed;
        expected.insert(expected.end(), middle.begin(), middle.end());
        expected.insert(expected.end(), seed.begin(), seed.end());
        std::vector<std::uint8_t> frame{0x28U, 0xb5U, 0x2fU, 0xfdU, 0xa0U};
        for (unsigned shift : {0U, 8U, 16U, 24U})
            frame.push_back(static_cast<std::uint8_t>(expected.size() >> shift));
        auto append = [&](std::vector<std::uint8_t> const& payload, bool last)
        {
            auto const header = (static_cast<std::uint32_t>(payload.size()) << 3U) | 4U | unsigned(last);
            for (unsigned shift : {0U, 8U, 16U})
                frame.push_back(static_cast<std::uint8_t>(header >> shift));
            frame.insert(frame.end(), payload.begin(), payload.end());
        };
        std::vector<std::uint8_t> payload;
        if (!encode_huffman_literals(seed, workspace, payload) || !workspace.huffman_repeat_valid)
            throw std::runtime_error("failed to initialize C++ Huffman history");
        payload.push_back(0U);
        append(payload, false);
        auto const frequencies = workspace.huffman_repeat_frequencies;
        auto const lengths = workspace.huffman_repeat_lengths;
        auto const log = workspace.huffman_repeat_log;
        parsed_block parsed;
        parsed.sequences.push_back(parsed_sequence{size, 3U, static_cast<std::uint32_t>(size), 0U});
        parsed.trailing_literal_position = middle.size();
        encode_sequences_block<false, true>(middle, parsed, workspace, payload);
        if (payload.empty() || (payload.front() & 3U) != 0U || !workspace.huffman_repeat_valid ||
            workspace.huffman_repeat_frequencies != frequencies || workspace.huffman_repeat_lengths != lengths ||
            workspace.huffman_repeat_log != log)
            throw std::runtime_error("sampled raw literals changed Huffman history");
        append(payload, false);
        if (!encode_huffman_literals(seed, workspace, payload) || (payload.front() & 3U) != 3U)
            throw std::runtime_error("Huffman repeat failed after sampled raw literals");
        payload.push_back(0U);
        append(payload, true);
        std::vector<std::uint8_t> decoded(expected.size());
        auto const result = ZSTD_decompress(decoded.data(), decoded.size(), frame.data(), frame.size());
        if (ZSTD_isError(result) || result != expected.size() || decoded != expected)
            throw std::runtime_error("reference rejected sampling-history frame");
        decoded.clear();
        auto decoder = sph::zstd::zstd_decompress{[&](std::span<std::uint8_t const> bytes) {
            decoded.insert(decoded.end(), bytes.begin(), bytes.end());
        }};
        decoder.update(frame);
        decoder.finish();
        if (decoded != expected) throw std::runtime_error("C++ rejected sampling-history frame");
        ++cases;
    }
    std::cout << cases << " sampling/raw/Huffman-repeat frames cross-decode\n";
}


template<std::size_t Block, unsigned Minimum> void check_custom_frames() {
    constexpr auto parameters = [] {
        sph::zstd::compression_parameters p{};
        p.compression_level=1; p.strategy=sph::zstd::compression_strategy::fast;
        p.minimum_match=Minimum; p.window_log=16; p.hash_log=15;
        p.block_size=Block; p.checksum=true; p.content_size=false;
        return p;
    }();
    std::vector<std::uint8_t> encoded;
    auto compressor=sph::zstd::make_zstd_compress<parameters>([&](std::span<std::uint8_t const> bytes) {
        encoded.insert(encoded.end(),bytes.begin(),bytes.end());
    });
    for (std::size_t size : {0U,1U,15U,16U,17U,127U,128U,129U,255U,256U,257U,65535U,65536U,65537U,131089U})
    for (unsigned pattern{}; pattern<3; ++pattern) {
        std::vector<std::uint8_t> input(size),decoded(size);
        std::uint32_t rng=17;
        for(std::size_t i{};i<size;++i) {
            rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
            input[i]=pattern==0 ? 42U : pattern==1 ? static_cast<std::uint8_t>(rng) :
                static_cast<std::uint8_t>(i%149U==0 ? rng : i%37U);
        }
        for(unsigned mode{};mode<3;++mode) {
            compressor.reset(); encoded.clear();
            if(mode!=1) compressor.compress_frame(input);
            else {
                for(std::size_t pos{};pos<size;pos+=17U)
                    compressor.update(std::span<std::uint8_t const>{input}.subspan(pos,std::min<std::size_t>(17U,size-pos)));
                compressor.finish();
            }
            auto const result=ZSTD_decompress(decoded.data(),decoded.size(),encoded.data(),encoded.size());
            if(ZSTD_isError(result) || result!=size || decoded!=input)
                throw std::runtime_error("custom fast min3..6 cross-decode failed");
        }
    }
}

}

int main()
{
    try
    {
        test_sample_boundaries();
        test_reference_history();
        test_cpp_history();
        check_custom_frames<256U, 3U>(); check_custom_frames<65536U, 3U>();
        check_custom_frames<256U, 4U>(); check_custom_frames<65536U, 4U>();
        check_custom_frames<256U, 5U>(); check_custom_frames<65536U, 5U>();
        check_custom_frames<256U, 6U>(); check_custom_frames<65536U, 6U>();
        std::cout << "1080 custom fast-strategy frames reference-decode\n";
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
