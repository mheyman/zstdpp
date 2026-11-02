# sph-zstd++

A header-only C++23 implementation of [Zstandard](https://facebook.github.io/zstd/), built around compile-time compression settings and callback-driven streaming. This repository also contains the reference zstd implementation used for byte-level compatibility checks and matched performance comparisons.

[Use it](#use-it) · [Build and verify](#build-and-verify) · [Performance](#performance) · [Full benchmark matrix](BENCHMARKS_INPUTS.md) · [Roadmap](PERFORMANCE_PLAN.md)

## Project status

This is an active port, with streaming compression and decompression, Huffman and FSE entropy coding, checksums, concatenated and skippable frames, and fast through binary-tree match parsing. Dictionary and multithreaded modes are still missing.

The current benchmark matrix confirms byte-for-byte identical compressed output at levels 1–22 on five deterministic inputs. The acceptance suite checks the public API and decoding in both directions. See the [performance work list](PERFORMANCE_PLAN.md) for current optimization targets and the [experiment history](PERFORMANCE_HISTORY.md) for earlier results.

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

Measured 2026-11-01 with optimized GCC 13 against the bundled reference on five deterministic 1 MiB inputs at levels 1–22. Each cell shows **throughput / peak resident memory / complete worker executable size** as C++ percentages of reference. A throughput value above 100% is faster; a memory or code-size value below 100% is smaller. Compression frames matched byte for byte, and both decoders recovered the input. Short trials are a screening comparison. The [full benchmark matrix](BENCHMARKS_INPUTS.md) provides all nine absolute C++ and reference measurements for every level and input, input definitions, and longer confirmations.

### Compression (% of reference)

| Level | Zero | Repeated | Structured | Random | Mixed |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 486.6% / 87.1% / 20.9% | 257.9% / 86.8% / 20.9% | 101.3% / 85.3% / 20.9% | 129.5% / 101.5% / 20.9% | 113.1% / 90.0% / 20.9% |
| 2 | 485.4% / 85.9% / 20.3% | 268.4% / 84.9% / 20.3% | 102.0% / 85.7% / 20.3% | 120.3% / 100.3% / 20.3% | 115.2% / 89.6% / 20.3% |
| 3 | 256.1% / 90.6% / 20.3% | 168.3% / 91.9% / 20.3% | 99.3% / 90.7% / 20.3% | 125.3% / 104.5% / 20.3% | 115.0% / 93.1% / 20.3% |
| 4 | 260.0% / 95.3% / 20.3% | 170.6% / 95.1% / 20.3% | 100.3% / 97.1% / 20.3% | 126.7% / 106.1% / 20.3% | 119.0% / 96.5% / 20.3% |
| 5 | 205.8% / 89.6% / 20.3% | 167.5% / 89.7% / 20.3% | 102.6% / 91.1% / 20.3% | 139.3% / 100.4% / 20.3% | 118.8% / 92.4% / 20.3% |
| 6 | 366.9% / 89.3% / 19.6% | 183.1% / 88.7% / 19.6% | 144.9% / 90.6% / 19.6% | 111.4% / 100.1% / 19.6% | 130.7% / 89.6% / 19.6% |
| 7 | 384.3% / 91.2% / 19.6% | 174.5% / 92.5% / 19.6% | 129.4% / 92.2% / 19.6% | 118.7% / 100.4% / 19.6% | 135.6% / 92.9% / 19.6% |
| 8 | 327.1% / 90.9% / 19.6% | 172.2% / 91.8% / 19.6% | 133.4% / 91.8% / 19.6% | 134.0% / 98.9% / 19.6% | 112.7% / 93.3% / 19.6% |
| 9 | 310.0% / 93.2% / 19.6% | 169.0% / 94.4% / 19.6% | 117.6% / 94.5% / 19.6% | 134.9% / 99.6% / 19.6% | 119.0% / 95.0% / 19.6% |
| 10 | 322.7% / 94.7% / 19.6% | 173.8% / 94.5% / 19.6% | 125.3% / 94.1% / 19.6% | 120.6% / 99.8% / 19.6% | 130.5% / 95.4% / 19.6% |
| 11 | 323.8% / 94.6% / 19.6% | 167.3% / 94.6% / 19.6% | 123.0% / 94.2% / 19.6% | 101.7% / 100.1% / 19.6% | 131.1% / 94.8% / 19.6% |
| 12 | 310.6% / 94.2% / 19.6% | 166.5% / 94.4% / 19.6% | 123.8% / 94.6% / 19.6% | 110.7% / 99.3% / 19.6% | 123.8% / 95.1% / 19.6% |
| 13 | 511.9% / 96.2% / 20.2% | 149.3% / 95.8% / 20.2% | 112.4% / 95.7% / 20.2% | 115.9% / 99.9% / 20.2% | 110.3% / 96.4% / 20.2% |
| 14 | 509.7% / 95.9% / 19.6% | 156.0% / 95.8% / 19.6% | 124.9% / 95.7% / 19.6% | 117.3% / 99.8% / 19.6% | 156.7% / 96.4% / 19.6% |
| 15 | 491.2% / 95.3% / 19.6% | 158.6% / 96.1% / 19.6% | 126.0% / 95.7% / 19.6% | 111.0% / 99.6% / 19.6% | 130.0% / 96.2% / 19.6% |
| 16 | 460.5% / 97.6% / 23.1% | 120.7% / 97.4% / 23.1% | 101.2% / 97.6% / 23.1% | 111.3% / 101.9% / 23.1% | 109.3% / 98.1% / 23.1% |
| 17 | 492.4% / 96.8% / 23.1% | 122.0% / 97.6% / 23.1% | 101.5% / 97.5% / 23.1% | 117.2% / 102.1% / 23.1% | 111.8% / 99.7% / 23.1% |
| 18 | 427.4% / 97.3% / 23.1% | 107.3% / 97.3% / 23.1% | 108.9% / 97.6% / 23.1% | 114.5% / 101.7% / 23.1% | 128.0% / 99.5% / 23.1% |
| 19 | 379.6% / 97.4% / 23.1% | 109.0% / 97.5% / 23.1% | 114.9% / 97.4% / 23.1% | 119.4% / 101.8% / 23.1% | 125.8% / 99.4% / 23.1% |
| 20 | 382.5% / 97.4% / 23.1% | 107.8% / 97.5% / 23.1% | 116.7% / 97.5% / 23.1% | 121.6% / 102.0% / 23.1% | 135.6% / 99.7% / 23.1% |
| 21 | 374.1% / 97.2% / 23.1% | 109.3% / 97.3% / 23.1% | 117.2% / 97.7% / 23.1% | 114.2% / 102.0% / 23.1% | 132.0% / 99.4% / 23.1% |
| 22 | 374.5% / 97.1% / 23.1% | 109.2% / 97.7% / 23.1% | 114.1% / 97.2% / 23.1% | 135.3% / 102.1% / 23.1% | 114.6% / 99.8% / 23.1% |

### Decompression (% of reference)

| Level | Zero | Repeated | Structured | Random | Mixed |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 128.4% / 100.9% / 60.8% | 478.1% / 98.5% / 60.8% | 103.7% / 101.5% / 60.8% | 89.9% / 101.5% / 60.8% | 113.6% / 97.8% / 60.8% |
| 2 | 138.1% / 102.0% / 60.8% | 514.8% / 97.9% / 60.8% | 107.7% / 98.4% / 60.8% | 125.5% / 100.2% / 60.8% | 130.9% / 98.1% / 60.8% |
| 3 | 113.6% / 101.3% / 60.8% | 529.2% / 100.7% / 60.8% | 109.9% / 99.5% / 60.8% | 104.2% / 100.0% / 60.8% | 134.4% / 97.8% / 60.8% |
| 4 | 130.6% / 100.8% / 60.8% | 542.9% / 100.2% / 60.8% | 109.6% / 98.4% / 60.8% | 109.8% / 100.0% / 60.8% | 134.6% / 98.4% / 60.8% |
| 5 | 130.3% / 103.2% / 60.8% | 429.6% / 100.7% / 60.8% | 116.6% / 99.0% / 60.8% | 110.1% / 102.0% / 60.8% | 125.7% / 97.8% / 60.8% |
| 6 | 129.3% / 103.1% / 60.8% | 504.4% / 98.8% / 60.8% | 112.1% / 99.1% / 60.8% | 84.7% / 100.1% / 60.8% | 161.3% / 98.4% / 60.8% |
| 7 | 120.3% / 101.0% / 60.8% | 463.7% / 98.8% / 60.8% | 119.1% / 101.3% / 60.8% | 108.9% / 100.9% / 60.8% | 166.9% / 100.5% / 60.8% |
| 8 | 142.2% / 99.9% / 60.8% | 495.7% / 100.1% / 60.8% | 111.9% / 98.5% / 60.8% | 92.7% / 101.4% / 60.8% | 159.3% / 97.7% / 60.8% |
| 9 | 123.2% / 101.7% / 60.8% | 479.7% / 98.8% / 60.8% | 111.8% / 98.7% / 60.8% | 107.9% / 101.4% / 60.8% | 163.3% / 99.7% / 60.8% |
| 10 | 126.7% / 101.5% / 60.8% | 511.8% / 97.1% / 60.8% | 112.9% / 100.5% / 60.8% | 113.1% / 101.9% / 60.8% | 173.3% / 99.9% / 60.8% |
| 11 | 124.0% / 99.9% / 60.8% | 520.0% / 100.4% / 60.8% | 110.0% / 99.4% / 60.8% | 96.8% / 101.7% / 60.8% | 170.2% / 97.7% / 60.8% |
| 12 | 131.8% / 101.4% / 60.8% | 532.5% / 100.8% / 60.8% | 106.0% / 98.4% / 60.8% | 84.9% / 100.5% / 60.8% | 170.5% / 100.5% / 60.8% |
| 13 | 111.3% / 101.7% / 60.8% | 540.8% / 97.8% / 60.8% | 105.2% / 99.7% / 60.8% | 70.1% / 100.2% / 60.8% | 155.3% / 97.3% / 60.8% |
| 14 | 127.0% / 102.0% / 60.8% | 502.7% / 99.5% / 60.8% | 104.4% / 97.8% / 60.8% | 107.7% / 101.2% / 60.8% | 168.2% / 99.9% / 60.8% |
| 15 | 131.9% / 100.7% / 60.8% | 524.0% / 99.8% / 60.8% | 116.0% / 99.1% / 60.8% | 104.2% / 99.6% / 60.8% | 154.9% / 97.7% / 60.8% |
| 16 | 128.0% / 102.1% / 60.8% | 251.9% / 98.7% / 60.8% | 91.0% / 100.0% / 60.8% | 103.2% / 99.3% / 60.8% | 122.7% / 98.6% / 60.8% |
| 17 | 132.1% / 101.6% / 60.8% | 255.4% / 97.2% / 60.8% | 90.3% / 100.0% / 60.8% | 97.2% / 102.3% / 60.8% | 123.0% / 98.3% / 60.8% |
| 18 | 125.6% / 101.5% / 60.8% | 428.2% / 98.8% / 60.8% | 89.4% / 100.0% / 60.8% | 98.4% / 101.2% / 60.8% | 132.1% / 97.2% / 60.8% |
| 19 | 128.2% / 101.4% / 60.8% | 425.2% / 97.6% / 60.8% | 90.9% / 100.0% / 60.8% | 98.5% / 102.0% / 60.8% | 137.1% / 97.2% / 60.8% |
| 20 | 129.2% / 101.9% / 60.8% | 390.8% / 98.8% / 60.8% | 93.3% / 100.0% / 60.8% | 92.6% / 100.8% / 60.8% | 129.5% / 97.8% / 60.8% |
| 21 | 121.3% / 101.1% / 60.8% | 447.1% / 99.5% / 60.8% | 90.4% / 100.0% / 60.8% | 102.0% / 100.2% / 60.8% | 129.9% / 97.8% / 60.8% |
| 22 | 128.1% / 100.4% / 60.8% | 458.2% / 100.5% / 60.8% | 92.1% / 100.0% / 60.8% | 99.9% / 101.2% / 60.8% | 132.6% / 97.8% / 60.8% |

## More information

- [Benchmark methodology and paired results](BENCHMARKS.md)
- [Full benchmark matrix with absolute measurements](BENCHMARKS_INPUTS.md)
- [Current performance work](PERFORMANCE_PLAN.md)
- [Experiment history and earlier results](PERFORMANCE_HISTORY.md)
- [Bundled reference implementation](reference_zstd/README.md)

The headers are in [`include/sph/zstd++/`](include/sph/zstd++/), tests are in [`test/`](test/), and benchmark tools are in [`eval/`](eval/). To benchmark a 1 MiB input, build the optimized `msvc-pair` or `gcc-pair` target and run `zstdpp_pair --level 3 --input path/to/file --iterations 1000 --trials 6`. The tool checks exact compressed bytes and both decoders.
