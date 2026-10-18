#include <sph/zstd++/zstd_compress.h>
#include <zstd.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

template <std::uint8_t MinimumMatch>
void check_reuse()
{
    constexpr auto parameters = [] {
        sph::zstd::compression_parameters p{};
        p.compression_level = 1;
        p.strategy = sph::zstd::compression_strategy::fast;
        p.window_log = 16;
        p.hash_log = 15;
        p.minimum_match = MinimumMatch;
        p.block_size = 65536;
        p.checksum = true;
        p.content_size = false;
        return p;
    }();
    std::vector<std::uint8_t> encoded;
    encoded.reserve(ZSTD_compressBound(131073));
    auto const before = sph::zstd::detail::cache_aligned_allocation_count;
    auto const freed_before = sph::zstd::detail::cache_aligned_deallocation_count;
    {
        auto compressor = sph::zstd::make_zstd_compress<parameters>(
            [&](std::span<std::uint8_t const> bytes) {
                encoded.insert(encoded.end(), bytes.begin(), bytes.end());
            });
        auto const initialized = sph::zstd::detail::cache_aligned_allocation_count;
        for (std::size_t size : {0U, 1U, 31U, 4095U, 4096U, 65535U, 65536U, 65537U, 131073U})
        {
            for (unsigned pattern = 0; pattern < 3; ++pattern)
            {
                std::vector<std::uint8_t> input(size);
                std::uint32_t random = 0x12345678U;
                for (std::size_t i = 0; i < size; ++i) {
                    random = random * 1664525U + 1013904223U;
                    input[i] = pattern == 0 ? 0 : pattern == 1 ?
                        static_cast<std::uint8_t>(i % 29) : static_cast<std::uint8_t>(random >> 24);
                }
                for (bool streaming : {false, true}) {
                    compressor.reset();
                    encoded.clear();
                    if (streaming) {
                        for (std::size_t offset = 0; offset < size; offset += 997)
                            compressor.update(std::span<std::uint8_t const>(input).subspan(
                                offset, std::min<std::size_t>(997, size - offset)));
                        compressor.finish();
                    } else compressor.compress_frame(input);
                    if (sph::zstd::detail::cache_aligned_allocation_count != initialized)
                        throw std::runtime_error("aligned storage reallocated after construction");
                    std::vector<std::uint8_t> decoded(size);
                    auto const result = ZSTD_decompress(decoded.data(), decoded.size(), encoded.data(), encoded.size());
                    if (ZSTD_isError(result) || result != size || decoded != input)
                        throw std::runtime_error("reference cross-decode failed after reuse");
                }
            }
        }
    }
    if (sph::zstd::detail::cache_aligned_allocation_count - before !=
        sph::zstd::detail::cache_aligned_deallocation_count - freed_before)
        throw std::runtime_error("aligned allocation/deallocation mismatch");
}

int main()
{
    check_reuse<3>(); check_reuse<4>(); check_reuse<5>(); check_reuse<6>(); check_reuse<7>();
    std::cout << "270 fast minMatch3..7 boundary/pattern/streaming/reuse frames passed\n";
}
