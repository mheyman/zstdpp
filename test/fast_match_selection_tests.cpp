#include <sph/zstd++/detail/zstd_compression.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

template <unsigned WindowLog, unsigned HashLog, std::size_t Target>
std::size_t check()
{
    using namespace sph::zstd::detail;
    fast_match_state<WindowLog, HashLog, 6U, Target> candidate{WindowLog, HashLog, 6U, Target};
    // Runtime minimum_match retains the original branching validity check.
    fast_match_state<WindowLog, HashLog, 0U, Target> oracle{WindowLog, HashLog, 6U, Target};
    std::size_t cases{};
    for (unsigned frame = 0; frame < 600; ++frame) {
        std::size_t const sizes[]{0, 7, 8, 9, 31, 4095, 4096, 8193, 131073};
        auto const size = sizes[frame % 9];
        std::vector<std::uint8_t> input(size);
        std::uint32_t random = frame + 1U;
        for (std::size_t i = 0; i < size; ++i) {
            random = random * 1664525U + 1013904223U;
            input[i] = frame % 3 == 0 ? static_cast<std::uint8_t>(random >> 24) :
                static_cast<std::uint8_t>((i % (frame % 127 + 1U)) ^ (i / 149U));
        }
        for (std::size_t begin = 0; begin < size || (size == 0 && begin == 0); begin += 1024) {
            auto const length = std::min<std::size_t>(1024, size - begin);
            auto const actual = candidate.parse(input, begin, length);
            auto const expected = oracle.parse(input, begin, length);
            if (actual.sequences != expected.sequences ||
                actual.trailing_literal_position != expected.trailing_literal_position ||
                actual.trailing_literal_length != expected.trailing_literal_length)
                throw std::runtime_error("fast validity selection changed parser output");
            ++cases;
            if (size == 0) break;
        }
        candidate.reset(); oracle.reset();
    }
    return cases;
}

int main()
{
    // 600 resets with windowLog23 cross the 32-bit index rollover; tiny hash
    // tables deliberately collide. Also exercise non-default skip distance.
    auto const cases = check<10, 8, 0>() + check<16, 8, 0>() +
        check<23, 8, 0>() + check<23, 10, 3>();
    std::cout << cases << " fast conditional-move parser oracle blocks passed\n";
}
