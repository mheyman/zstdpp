# sph-zstd++

A header-only C++23 implementation of [Zstandard](https://facebook.github.io/zstd/), built around compile-time compression settings and callback-driven streaming. This repository also contains the reference zstd implementation used for byte-level compatibility checks and matched performance comparisons.

The project is an active port, not a drop-in replacement for every zstd feature. Supported paths include streaming compression and decompression, Huffman and FSE entropy coding, checksums, concatenated and skippable frames, and the fast, double-fast, greedy, lazy, and binary-tree parsing strategies. Dictionary and multithreaded modes are not implemented. The current benchmark matrix confirms byte-for-byte identical compressed output at levels 1–22 on five deterministic inputs; the acceptance suite checks the public API and cross-decoding separately.

## Use it

Add `include` to your C++23 include path, or link the header-only `sph::zstdpp` CMake target. Output is delivered to a callback whose span remains valid for that invocation. Call `finish()` explicitly; destructors do not emit or validate output.

```cpp
#include <sph/zstd++/zstd.h>

#include <cstdint>
#include <span>
#include <vector>

std::vector<std::uint8_t> compressed;
auto compressor = sph::zstd::zstd_compress{
    [&compressed](std::span<std::uint8_t const> bytes) {
        compressed.insert(compressed.end(), bytes.begin(), bytes.end());
    }};

std::vector<std::uint8_t> input{/* ... */};
compressor.update(input);
compressor.finish();

std::vector<std::uint8_t> restored;
auto decompressor = sph::zstd::zstd_decompress{
    [&restored](std::span<std::uint8_t const> bytes) {
        restored.insert(restored.end(), bytes.begin(), bytes.end());
    }};
decompressor.update(compressed);
decompressor.finish();
```

Compression parameters can be fixed at compile time. The helper preserves the callback's concrete type for inlining:

```cpp
constexpr auto settings = [] {
    auto value = sph::zstd::compression_parameters{};
    value.checksum = true;
    value.window_log = 20;
    return value;
}();

auto compressor = sph::zstd::make_zstd_compress<settings>(callback);
```

For a complete borrowed input, `compress_frame(span)` avoids copying it into streaming history. The streams also provide `update`, `flush` (compression), `reset`, status, byte counters, and parsed frame information.

## Build and verify

```text
cmake -S . -B out/build -DSPH_ZSTDPP_BUILD_TESTS=ON
cmake --build out/build
ctest --test-dir out/build --output-on-failure
```

For the reference-byte acceptance suite, use `msvc-acceptance`, `clang-acceptance`, or `gcc-acceptance`:

```text
cmake --preset msvc-acceptance
cmake --build --preset msvc-acceptance
ctest --preset msvc-acceptance
```

The tests compare resolved compression parameters and compressed bytes, and check decoding in both directions. The public library target itself does not link reference zstd.

## Performance

Measured 2026-11-01 from the current optimized GCC 13 build against the bundled reference on five deterministic 1 MiB inputs at every level. Each cell shows **speed / peak memory / executable size**, as C++ percentages of reference. Higher speed and lower memory or code size are better. Compression frames matched byte for byte, and both decoders recovered the input. The mixed-input rows used three alternating trials of 200 iterations; other inputs used two alternating trials of 100 iterations. Short runs, especially for zero-filled data, are a screening comparison rather than a precise speedup claim. [Full measurements](BENCHMARKS_INPUTS.md) give C++ and reference values in MiB/s, MiB, and KiB for every level and input.

### Compression (% of reference)

The mixed-input level 13 compression cell uses a longer six-trial 1,000-frame [confirmation](BENCHMARKS.md#confirmed-mixed-input-level-13-compression). The mixed-input level 16 compression cell uses a longer six-trial 2,000-frame [confirmation](BENCHMARKS.md#confirmed-mixed-input-level-16-compression). The short-screen rows remain in the detailed evaluation.

| Level | Zero | Repeated | Structured | Random | Mixed |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 428.4% / 86.0% / 20.9% | 237.9% / 87.3% / 20.9% | 98.3% / 84.0% / 20.9% | 139.2% / 103.7% / 20.9% | 115.0% / 89.1% / 20.9% |
| 2 | 464.0% / 85.5% / 20.3% | 265.1% / 85.3% / 20.3% | 98.4% / 85.9% / 20.3% | 124.9% / 100.9% / 20.3% | 111.3% / 89.0% / 20.3% |
| 3 | 254.7% / 90.3% / 20.3% | 166.8% / 92.0% / 20.3% | 91.5% / 90.7% / 20.3% | 131.3% / 103.5% / 20.3% | 109.3% / 92.2% / 20.3% |
| 4 | 247.7% / 96.8% / 20.3% | 166.7% / 95.1% / 20.3% | 91.1% / 97.7% / 20.3% | 146.9% / 106.6% / 20.3% | 122.4% / 96.8% / 20.3% |
| 5 | 205.9% / 89.3% / 20.3% | 168.1% / 90.0% / 20.3% | 106.4% / 91.0% / 20.3% | 140.8% / 100.1% / 20.3% | 117.3% / 92.2% / 20.3% |
| 6 | 361.4% / 89.3% / 19.6% | 180.0% / 88.6% / 19.6% | 154.9% / 90.3% / 19.6% | 117.4% / 100.4% / 19.6% | 134.8% / 91.0% / 19.6% |
| 7 | 368.1% / 91.5% / 19.6% | 180.9% / 92.3% / 19.6% | 101.9% / 91.9% / 19.6% | 128.4% / 100.8% / 19.6% | 111.1% / 93.0% / 19.6% |
| 8 | 320.1% / 91.8% / 19.6% | 171.5% / 91.5% / 19.6% | 128.0% / 91.5% / 19.6% | 137.1% / 101.0% / 19.6% | 138.5% / 92.7% / 19.6% |
| 9 | 313.0% / 94.7% / 19.6% | 176.9% / 94.7% / 19.6% | 126.5% / 94.3% / 19.6% | 126.4% / 99.7% / 19.6% | 119.0% / 95.5% / 19.6% |
| 10 | 324.8% / 94.3% / 19.6% | 169.6% / 93.8% / 19.6% | 120.9% / 94.3% / 19.6% | 142.7% / 100.1% / 19.6% | 131.2% / 95.2% / 19.6% |
| 11 | 322.2% / 94.3% / 19.6% | 164.4% / 94.4% / 19.6% | 120.9% / 94.5% / 19.6% | 106.7% / 99.7% / 19.6% | 116.7% / 95.3% / 19.6% |
| 12 | 325.4% / 94.7% / 19.6% | 165.1% / 94.0% / 19.6% | 130.3% / 94.5% / 19.6% | 123.0% / 99.7% / 19.6% | 137.1% / 95.1% / 19.6% |
| 13 | 499.5% / 95.6% / 20.2% | 147.3% / 95.8% / 20.2% | 121.0% / 96.1% / 20.2% | 84.5% / 99.6% / 20.2% | 112.0% / 96.6% / 20.2% |
| 14 | 522.1% / 95.8% / 19.6% | 156.3% / 95.7% / 19.6% | 124.5% / 95.7% / 19.6% | 125.2% / 99.8% / 19.6% | 113.5% / 95.8% / 19.6% |
| 15 | 529.7% / 95.8% / 19.6% | 166.6% / 95.7% / 19.6% | 123.4% / 95.8% / 19.6% | 112.0% / 100.3% / 19.6% | 114.8% / 96.3% / 19.6% |
| 16 | 392.7% / 97.6% / 22.4% | 106.9% / 97.4% / 22.4% | 90.0% / 97.7% / 22.4% | 118.3% / 102.4% / 22.4% | 110.8% / 98.1% / 22.4% |
| 17 | 424.3% / 97.8% / 22.4% | 114.8% / 97.1% / 22.4% | 93.3% / 97.3% / 22.4% | 118.3% / 102.4% / 22.4% | 117.5% / 99.8% / 22.4% |
| 18 | 385.7% / 97.4% / 22.4% | 109.4% / 97.7% / 22.4% | 109.1% / 97.7% / 22.4% | 126.1% / 101.6% / 22.4% | 126.3% / 99.5% / 22.4% |
| 19 | 435.4% / 97.7% / 22.5% | 114.3% / 97.5% / 22.5% | 111.2% / 97.4% / 22.5% | 116.1% / 101.7% / 22.5% | 126.6% / 99.8% / 22.5% |
| 20 | 433.5% / 97.9% / 22.5% | 113.4% / 97.0% / 22.5% | 114.3% / 97.7% / 22.5% | 119.1% / 102.0% / 22.5% | 132.2% / 100.0% / 22.5% |
| 21 | 453.9% / 97.6% / 22.5% | 115.8% / 97.6% / 22.5% | 114.8% / 97.5% / 22.5% | 119.9% / 102.4% / 22.5% | 121.8% / 99.7% / 22.5% |
| 22 | 438.4% / 97.5% / 22.5% | 112.5% / 97.6% / 22.5% | 114.8% / 97.5% / 22.5% | 122.9% / 102.1% / 22.5% | 118.5% / 99.8% / 22.5% |

### Decompression (% of reference)

| Level | Zero | Repeated | Structured | Random | Mixed |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 136.2% / 100.4% / 60.8% | 544.8% / 99.1% / 60.8% | 100.4% / 99.3% / 60.8% | 101.7% / 101.3% / 60.8% | 111.9% / 97.9% / 60.8% |
| 2 | 132.3% / 99.4% / 60.8% | 534.5% / 99.4% / 60.8% | 109.9% / 98.9% / 60.8% | 113.2% / 99.9% / 60.8% | 127.0% / 98.3% / 60.8% |
| 3 | 128.0% / 101.3% / 60.8% | 526.2% / 98.4% / 60.8% | 105.8% / 99.3% / 60.8% | 107.7% / 100.1% / 60.8% | 128.8% / 97.8% / 60.8% |
| 4 | 124.7% / 101.6% / 60.8% | 523.8% / 99.8% / 60.8% | 99.9% / 99.2% / 60.8% | 89.1% / 100.2% / 60.8% | 129.7% / 98.4% / 60.8% |
| 5 | 128.2% / 100.9% / 60.8% | 477.0% / 97.7% / 60.8% | 111.0% / 98.4% / 60.8% | 120.4% / 101.1% / 60.8% | 125.2% / 97.7% / 60.8% |
| 6 | 129.1% / 102.0% / 60.8% | 484.3% / 98.7% / 60.8% | 96.8% / 101.3% / 60.8% | 99.6% / 101.4% / 60.8% | 164.6% / 99.9% / 60.8% |
| 7 | 127.4% / 100.1% / 60.8% | 545.8% / 98.7% / 60.8% | 113.7% / 101.1% / 60.8% | 101.4% / 100.1% / 60.8% | 155.6% / 98.4% / 60.8% |
| 8 | 109.8% / 101.0% / 60.8% | 462.3% / 99.3% / 60.8% | 109.4% / 98.8% / 60.8% | 95.8% / 100.8% / 60.8% | 168.2% / 99.3% / 60.8% |
| 9 | 135.5% / 101.7% / 60.8% | 537.9% / 97.4% / 60.8% | 97.4% / 98.4% / 60.8% | 103.2% / 99.7% / 60.8% | 163.6% / 97.8% / 60.8% |
| 10 | 122.1% / 101.7% / 60.8% | 546.4% / 99.8% / 60.8% | 102.5% / 100.8% / 60.8% | 97.8% / 99.9% / 60.8% | 162.7% / 99.3% / 60.8% |
| 11 | 121.6% / 101.5% / 60.8% | 526.1% / 99.0% / 60.8% | 100.2% / 98.2% / 60.8% | 73.9% / 101.8% / 60.8% | 152.1% / 99.9% / 60.8% |
| 12 | 104.6% / 100.2% / 60.8% | 555.6% / 99.6% / 60.8% | 103.2% / 97.6% / 60.8% | 100.3% / 102.2% / 60.8% | 160.7% / 99.8% / 60.8% |
| 13 | 138.6% / 102.1% / 60.8% | 540.9% / 99.9% / 60.8% | 103.9% / 98.8% / 60.8% | 95.3% / 100.6% / 60.8% | 161.5% / 97.8% / 60.8% |
| 14 | 120.7% / 101.5% / 60.8% | 547.1% / 97.8% / 60.8% | 107.6% / 99.4% / 60.8% | 79.1% / 100.7% / 60.8% | 149.8% / 99.9% / 60.8% |
| 15 | 136.6% / 102.5% / 60.8% | 422.3% / 100.0% / 60.8% | 111.8% / 99.5% / 60.8% | 90.4% / 102.2% / 60.8% | 166.2% / 98.4% / 60.8% |
| 16 | 108.5% / 102.2% / 60.8% | 248.9% / 99.5% / 60.8% | 90.3% / 100.1% / 60.8% | 91.7% / 100.8% / 60.8% | 119.7% / 98.9% / 60.8% |
| 17 | 131.2% / 100.7% / 60.8% | 261.6% / 99.1% / 60.8% | 88.9% / 99.4% / 60.8% | 82.7% / 101.8% / 60.8% | 115.3% / 99.9% / 60.8% |
| 18 | 128.8% / 103.1% / 60.8% | 445.2% / 98.4% / 60.8% | 81.5% / 97.2% / 60.8% | 89.0% / 102.3% / 60.8% | 120.1% / 97.3% / 60.8% |
| 19 | 136.3% / 101.0% / 60.8% | 443.2% / 100.3% / 60.8% | 84.4% / 97.8% / 60.8% | 93.0% / 100.7% / 60.8% | 122.6% / 97.2% / 60.8% |
| 20 | 127.0% / 101.4% / 60.8% | 465.0% / 98.5% / 60.8% | 77.5% / 101.2% / 60.8% | 111.3% / 100.8% / 60.8% | 121.6% / 97.0% / 60.8% |
| 21 | 125.4% / 101.9% / 60.8% | 414.1% / 99.0% / 60.8% | 85.1% / 98.2% / 60.8% | 91.0% / 102.0% / 60.8% | 114.0% / 97.8% / 60.8% |
| 22 | 130.3% / 102.7% / 60.8% | 450.3% / 97.9% / 60.8% | 87.1% / 100.2% / 60.8% | 137.0% / 101.2% / 60.8% | 121.4% / 96.9% / 60.8% |

[Experiment history](PERFORMANCE_HISTORY.md) and the [performance work list](PERFORMANCE_PLAN.md) track progress separately.

To benchmark your own 1 MiB input, build the optimized `msvc-pair` or `gcc-pair` target and run `zstdpp_pair --level 3 --input path/to/file --iterations 1000 --trials 6`. The pair tool checks exact compressed bytes and both decoders.

## Project notes

- [Benchmark tables and methodology](BENCHMARKS.md)
- [Full current benchmark matrix](BENCHMARKS_INPUTS.md)
- [Performance task list](PERFORMANCE_PLAN.md)
- [Experiment history and prior reports](PERFORMANCE_HISTORY.md)
- [Bundled reference implementation](reference_zstd/README.md)

The headers live under `include/sph/zstd++/`; tests and acceptance checks live under `test/`, and the isolated benchmark workers and controllers live under `eval/`.
