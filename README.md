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

Measured 2026-11-01 with optimized GCC 13 against the bundled reference on five deterministic 1 MiB inputs at every level. Each cell shows **throughput / peak memory / executable size** as C++ percentages of reference. Higher throughput and lower memory or code size are better. Compression frames matched byte for byte, and both decoders recovered the input. These short runs are a screening comparison; the methodology and absolute C++/reference values in MiB/s, MiB, and KiB are in the [full benchmark matrix](BENCHMARKS_INPUTS.md).

### Compression (% of reference)

The mixed-input level 13 compression cell uses a longer six-trial 1,000-frame [confirmation](BENCHMARKS.md#confirmed-mixed-input-level-13-compression). The mixed-input level 16 compression cell uses a longer six-trial 2,000-frame [confirmation](BENCHMARKS.md#confirmed-mixed-input-level-16-compression). The short-screen rows remain in the detailed evaluation.

| Level | Zero | Repeated | Structured | Random | Mixed |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 457.1% / 87.9% / 20.9% | 212.9% / 86.9% / 20.9% | 96.4% / 84.6% / 20.9% | 134.7% / 99.8% / 20.9% | 108.2% / 87.1% / 20.9% |
| 2 | 473.8% / 85.5% / 20.3% | 271.1% / 85.9% / 20.3% | 95.7% / 84.5% / 20.3% | 122.7% / 99.5% / 20.3% | 109.3% / 89.9% / 20.3% |
| 3 | 252.6% / 90.5% / 20.3% | 166.7% / 91.2% / 20.3% | 95.7% / 88.6% / 20.3% | 127.9% / 104.5% / 20.3% | 108.1% / 90.9% / 20.3% |
| 4 | 259.2% / 95.1% / 20.3% | 164.7% / 95.8% / 20.3% | 94.7% / 97.8% / 20.3% | 133.1% / 106.4% / 20.3% | 113.5% / 97.5% / 20.3% |
| 5 | 219.1% / 89.7% / 20.3% | 161.5% / 90.4% / 20.3% | 93.4% / 90.2% / 20.3% | 134.6% / 100.1% / 20.3% | 110.9% / 91.5% / 20.3% |
| 6 | 368.2% / 89.8% / 19.6% | 181.7% / 89.6% / 19.6% | 130.0% / 89.2% / 19.6% | 119.9% / 99.6% / 19.6% | 128.3% / 90.6% / 19.6% |
| 7 | 366.0% / 91.9% / 19.6% | 182.1% / 91.7% / 19.6% | 147.1% / 92.5% / 19.6% | 136.7% / 99.5% / 19.6% | 144.9% / 92.7% / 19.6% |
| 8 | 323.8% / 91.3% / 19.6% | 169.2% / 91.7% / 19.6% | 124.4% / 91.5% / 19.6% | 111.1% / 98.9% / 19.6% | 111.8% / 93.0% / 19.6% |
| 9 | 353.8% / 94.7% / 19.6% | 169.5% / 94.7% / 19.6% | 127.7% / 94.3% / 19.6% | 103.5% / 99.4% / 19.6% | 119.3% / 95.4% / 19.6% |
| 10 | 320.7% / 94.2% / 19.6% | 170.2% / 94.1% / 19.6% | 125.8% / 95.2% / 19.6% | 112.8% / 99.6% / 19.6% | 120.7% / 95.2% / 19.6% |
| 11 | 331.5% / 94.0% / 19.6% | 168.0% / 94.1% / 19.6% | 115.4% / 94.4% / 19.6% | 123.1% / 100.1% / 19.6% | 117.3% / 95.2% / 19.6% |
| 12 | 319.6% / 94.5% / 19.6% | 222.5% / 94.1% / 19.6% | 126.4% / 94.2% / 19.6% | 111.4% / 99.9% / 19.6% | 124.4% / 95.1% / 19.6% |
| 13 | 502.7% / 95.7% / 20.2% | 148.4% / 96.2% / 20.2% | 116.9% / 95.8% / 20.2% | 79.7% / 100.1% / 20.2% | 112.0% / 96.6% / 20.2% |
| 14 | 510.5% / 95.5% / 19.6% | 147.5% / 95.8% / 19.6% | 121.5% / 95.7% / 19.6% | 118.8% / 99.7% / 19.6% | 114.1% / 96.3% / 19.6% |
| 15 | 503.4% / 96.0% / 19.6% | 163.6% / 95.6% / 19.6% | 122.8% / 95.8% / 19.6% | 114.8% / 100.1% / 19.6% | 108.4% / 96.1% / 19.6% |
| 16 | 389.8% / 97.4% / 22.4% | 112.2% / 97.2% / 22.4% | 90.9% / 97.7% / 22.4% | 111.8% / 101.9% / 22.4% | 110.8% / 98.1% / 22.4% |
| 17 | 337.9% / 97.4% / 22.4% | 113.8% / 97.4% / 22.4% | 93.1% / 97.3% / 22.4% | 134.3% / 102.0% / 22.4% | 110.4% / 99.8% / 22.4% |
| 18 | 371.7% / 97.5% / 22.4% | 108.6% / 97.2% / 22.4% | 114.8% / 97.6% / 22.4% | 117.5% / 101.9% / 22.4% | 131.6% / 99.7% / 22.4% |
| 19 | 457.4% / 97.8% / 22.5% | 115.2% / 97.3% / 22.5% | 114.0% / 98.0% / 22.5% | 123.5% / 102.4% / 22.5% | 132.4% / 99.9% / 22.5% |
| 20 | 445.1% / 97.7% / 22.5% | 116.0% / 97.3% / 22.5% | 118.8% / 97.5% / 22.5% | 121.6% / 102.3% / 22.5% | 126.0% / 99.7% / 22.5% |
| 21 | 431.2% / 97.5% / 22.5% | 113.0% / 97.4% / 22.5% | 115.8% / 97.5% / 22.5% | 125.8% / 102.1% / 22.5% | 130.0% / 100.1% / 22.5% |
| 22 | 438.2% / 97.3% / 22.5% | 116.3% / 97.6% / 22.5% | 115.2% / 97.0% / 22.5% | 117.3% / 102.3% / 22.5% | 126.7% / 100.2% / 22.5% |

### Decompression (% of reference)

Structured levels 16–22 use six alternating 10,000-frame exact-output trials; the other cells use the screening trials described above.

| Level | Zero | Repeated | Structured | Random | Mixed |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 113.3% / 101.1% / 60.8% | 523.8% / 99.3% / 60.8% | 73.4% / 99.1% / 60.8% | 105.0% / 101.1% / 60.8% | 111.9% / 99.9% / 60.8% |
| 2 | 129.6% / 101.5% / 60.8% | 492.8% / 101.8% / 60.8% | 106.5% / 99.7% / 60.8% | 97.3% / 101.1% / 60.8% | 132.3% / 97.3% / 60.8% |
| 3 | 125.9% / 100.2% / 60.8% | 541.8% / 97.7% / 60.8% | 108.2% / 99.0% / 60.8% | 86.2% / 100.3% / 60.8% | 130.2% / 97.8% / 60.8% |
| 4 | 138.6% / 100.0% / 60.8% | 493.9% / 99.1% / 60.8% | 107.4% / 99.0% / 60.8% | 92.4% / 100.6% / 60.8% | 131.2% / 97.8% / 60.8% |
| 5 | 140.0% / 101.4% / 60.8% | 508.0% / 98.0% / 60.8% | 113.6% / 98.5% / 60.8% | 88.5% / 101.6% / 60.8% | 126.0% / 98.4% / 60.8% |
| 6 | 123.3% / 100.6% / 60.8% | 522.7% / 98.5% / 60.8% | 110.6% / 98.9% / 60.8% | 99.4% / 100.4% / 60.8% | 167.9% / 97.5% / 60.8% |
| 7 | 135.5% / 100.8% / 60.8% | 540.8% / 97.6% / 60.8% | 111.2% / 98.9% / 60.8% | 94.7% / 100.2% / 60.8% | 162.9% / 97.9% / 60.8% |
| 8 | 139.5% / 101.8% / 60.8% | 550.0% / 99.4% / 60.8% | 119.9% / 98.1% / 60.8% | 130.4% / 101.2% / 60.8% | 167.8% / 98.4% / 60.8% |
| 9 | 129.8% / 100.8% / 60.8% | 475.4% / 98.1% / 60.8% | 111.7% / 98.4% / 60.8% | 88.8% / 101.3% / 60.8% | 170.2% / 98.3% / 60.8% |
| 10 | 129.9% / 101.1% / 60.8% | 523.6% / 99.4% / 60.8% | 108.4% / 99.0% / 60.8% | 102.9% / 101.1% / 60.8% | 168.5% / 99.7% / 60.8% |
| 11 | 135.4% / 101.1% / 60.8% | 542.3% / 100.1% / 60.8% | 106.6% / 100.3% / 60.8% | 97.0% / 102.5% / 60.8% | 167.6% / 98.7% / 60.8% |
| 12 | 125.1% / 100.5% / 60.8% | 553.5% / 99.0% / 60.8% | 114.0% / 96.7% / 60.8% | 83.9% / 101.3% / 60.8% | 171.1% / 100.5% / 60.8% |
| 13 | 136.6% / 101.5% / 60.8% | 487.6% / 99.1% / 60.8% | 121.6% / 99.1% / 60.8% | 133.4% / 102.2% / 60.8% | 169.6% / 98.4% / 60.8% |
| 14 | 126.2% / 101.7% / 60.8% | 556.4% / 100.3% / 60.8% | 113.0% / 98.9% / 60.8% | 107.5% / 101.4% / 60.8% | 169.3% / 99.7% / 60.8% |
| 15 | 126.5% / 102.6% / 60.8% | 533.4% / 98.8% / 60.8% | 104.8% / 99.5% / 60.8% | 94.9% / 100.9% / 60.8% | 181.3% / 97.8% / 60.8% |
| 16 | 143.5% / 100.6% / 60.8% | 284.6% / 99.2% / 60.8% | 91.8% / 100.3% / 60.8% | 101.6% / 100.8% / 60.8% | 121.6% / 98.2% / 60.8% |
| 17 | 115.5% / 100.3% / 60.8% | 265.1% / 98.5% / 60.8% | 91.3% / 97.7% / 60.8% | 104.2% / 102.5% / 60.8% | 116.6% / 99.9% / 60.8% |
| 18 | 123.4% / 101.4% / 60.8% | 466.9% / 98.8% / 60.8% | 89.2% / 98.8% / 60.8% | 101.7% / 100.4% / 60.8% | 122.3% / 97.8% / 60.8% |
| 19 | 115.7% / 100.1% / 60.8% | 453.6% / 101.6% / 60.8% | 89.1% / 98.2% / 60.8% | 104.3% / 101.8% / 60.8% | 122.5% / 97.8% / 60.8% |
| 20 | 126.9% / 102.6% / 60.8% | 440.7% / 99.1% / 60.8% | 89.4% / 98.5% / 60.8% | 103.3% / 102.3% / 60.8% | 126.2% / 97.9% / 60.8% |
| 21 | 134.8% / 101.3% / 60.8% | 497.3% / 99.0% / 60.8% | 90.3% / 99.9% / 60.8% | 89.0% / 100.3% / 60.8% | 126.0% / 97.8% / 60.8% |
| 22 | 125.4% / 101.7% / 60.8% | 463.9% / 99.5% / 60.8% | 88.8% / 99.3% / 60.8% | 100.5% / 102.8% / 60.8% | 126.7% / 98.4% / 60.8% |

## More information

- [Benchmark methodology and paired results](BENCHMARKS.md)
- [Full benchmark matrix with absolute measurements](BENCHMARKS_INPUTS.md)
- [Current performance work](PERFORMANCE_PLAN.md)
- [Experiment history and earlier results](PERFORMANCE_HISTORY.md)
- [Bundled reference implementation](reference_zstd/README.md)

The headers are in [`include/sph/zstd++/`](include/sph/zstd++/), tests are in [`test/`](test/), and benchmark tools are in [`eval/`](eval/). To benchmark a 1 MiB input, build the optimized `msvc-pair` or `gcc-pair` target and run `zstdpp_pair --level 3 --input path/to/file --iterations 1000 --trials 6`. The tool checks exact compressed bytes and both decoders.
