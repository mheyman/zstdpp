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

Measured 2026-11-01 from the current optimized GCC 13 build against the bundled reference on five deterministic 1 MiB inputs at every level. Each cell shows **throughput / peak memory / executable size**, as C++ percentages of reference. Higher throughput and lower memory or code size are better. Compression frames matched byte for byte, and both decoders recovered the input. The mixed-input rows used three alternating trials of 200 iterations; other inputs used two alternating trials of 100 iterations. Short runs, especially for zero-filled data, are a screening comparison rather than a precise speedup claim.

### Compression (% of reference)

| Level | Zero | Repeated | Structured | Random | Mixed |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 461.3% / 86.5% / 20.9% | 160.6% / 86.3% / 20.9% | 98.6% / 85.1% / 20.9% | 128.5% / 102.6% / 20.9% | 112.3% / 89.7% / 20.9% |
| 2 | 461.6% / 86.5% / 20.3% | 253.9% / 85.5% / 20.3% | 97.2% / 88.3% / 20.3% | 135.9% / 102.6% / 20.3% | 111.5% / 89.9% / 20.3% |
| 3 | 251.0% / 90.3% / 20.3% | 163.2% / 90.5% / 20.3% | 97.9% / 90.1% / 20.3% | 122.6% / 103.7% / 20.3% | 103.8% / 92.2% / 20.3% |
| 4 | 240.7% / 95.1% / 20.3% | 167.2% / 94.9% / 20.3% | 93.9% / 94.7% / 20.3% | 131.0% / 107.4% / 20.3% | 110.3% / 97.5% / 20.3% |
| 5 | 190.6% / 89.5% / 20.3% | 163.0% / 88.1% / 20.3% | 94.4% / 88.4% / 20.3% | 124.6% / 99.5% / 20.3% | 112.2% / 92.0% / 20.3% |
| 6 | 367.1% / 89.2% / 19.6% | 184.9% / 89.8% / 19.6% | 134.3% / 89.8% / 19.6% | 112.7% / 100.7% / 19.6% | 144.3% / 90.1% / 19.6% |
| 7 | 368.0% / 92.0% / 19.6% | 180.8% / 91.7% / 19.6% | 133.7% / 91.7% / 19.6% | 121.5% / 99.1% / 19.6% | 101.6% / 93.3% / 19.6% |
| 8 | 317.8% / 91.9% / 19.6% | 166.3% / 91.1% / 19.6% | 131.8% / 91.6% / 19.6% | 115.6% / 99.3% / 19.6% | 117.0% / 93.0% / 19.6% |
| 9 | 310.4% / 93.7% / 19.6% | 169.6% / 93.9% / 19.6% | 123.3% / 94.5% / 19.6% | 116.9% / 99.8% / 19.6% | 110.7% / 95.1% / 19.6% |
| 10 | 341.5% / 94.5% / 19.6% | 166.0% / 94.1% / 19.6% | 124.4% / 94.4% / 19.6% | 98.5% / 99.6% / 19.6% | 138.9% / 94.4% / 19.6% |
| 11 | 322.2% / 94.5% / 19.6% | 162.7% / 94.4% / 19.6% | 122.7% / 94.3% / 19.6% | 143.8% / 99.8% / 19.6% | 131.4% / 94.7% / 19.6% |
| 12 | 324.0% / 94.5% / 19.6% | 159.6% / 94.0% / 19.6% | 113.4% / 94.6% / 19.6% | 109.7% / 100.1% / 19.6% | 118.4% / 94.9% / 19.6% |
| 13 | 612.3% / 95.5% / 20.2% | 147.0% / 95.8% / 20.2% | 121.7% / 95.8% / 20.2% | 133.4% / 99.8% / 20.2% | 77.2% / 96.5% / 20.2% |
| 14 | 510.7% / 95.7% / 19.6% | 158.2% / 95.8% / 19.6% | 132.1% / 96.2% / 19.6% | 124.2% / 99.4% / 19.6% | 102.6% / 96.4% / 19.6% |
| 15 | 524.9% / 95.7% / 19.6% | 156.3% / 95.6% / 19.6% | 124.3% / 95.1% / 19.6% | 84.3% / 100.6% / 19.6% | 144.1% / 96.4% / 19.6% |
| 16 | 378.1% / 97.7% / 22.4% | 91.3% / 97.5% / 22.4% | 84.4% / 97.3% / 22.4% | 110.6% / 102.4% / 22.4% | 112.0% / 98.1% / 22.4% |
| 17 | 417.7% / 97.8% / 22.4% | 96.1% / 97.3% / 22.4% | 84.9% / 97.4% / 22.4% | 112.3% / 102.0% / 22.4% | 98.5% / 99.5% / 22.4% |
| 18 | 442.0% / 97.7% / 22.5% | 92.5% / 97.7% / 22.5% | 109.5% / 97.3% / 22.5% | 137.4% / 101.4% / 22.5% | 130.9% / 99.5% / 22.5% |
| 19 | 371.6% / 97.5% / 22.5% | 90.6% / 97.6% / 22.5% | 108.4% / 97.4% / 22.5% | 134.2% / 102.3% / 22.5% | 149.4% / 99.3% / 22.5% |
| 20 | 363.6% / 97.6% / 22.5% | 88.1% / 97.1% / 22.5% | 110.2% / 97.7% / 22.5% | 128.7% / 101.6% / 22.5% | 148.7% / 99.4% / 22.5% |
| 21 | 385.6% / 97.3% / 22.5% | 91.2% / 97.3% / 22.5% | 95.8% / 97.6% / 22.5% | 131.7% / 102.1% / 22.5% | 148.6% / 99.6% / 22.5% |
| 22 | 378.2% / 97.6% / 22.5% | 90.2% / 97.3% / 22.5% | 139.4% / 97.0% / 22.5% | 118.5% / 102.1% / 22.5% | 139.4% / 100.1% / 22.5% |

### Decompression (% of reference)

| Level | Zero | Repeated | Structured | Random | Mixed |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 133.4% / 101.2% / 60.8% | 480.0% / 98.8% / 60.8% | 110.0% / 99.1% / 60.8% | 105.7% / 99.6% / 60.8% | 120.0% / 99.8% / 60.8% |
| 2 | 131.9% / 101.0% / 60.8% | 500.4% / 101.3% / 60.8% | 112.6% / 98.0% / 60.8% | 110.6% / 100.8% / 60.8% | 129.5% / 97.2% / 60.8% |
| 3 | 131.9% / 102.9% / 60.8% | 546.3% / 99.7% / 60.8% | 109.1% / 99.5% / 60.8% | 97.4% / 101.3% / 60.8% | 131.7% / 98.3% / 60.8% |
| 4 | 136.5% / 101.3% / 60.8% | 479.5% / 99.9% / 60.8% | 108.5% / 97.9% / 60.8% | 110.2% / 101.6% / 60.8% | 127.4% / 97.8% / 60.8% |
| 5 | 125.1% / 101.3% / 60.8% | 484.5% / 98.4% / 60.8% | 114.0% / 98.4% / 60.8% | 100.1% / 100.9% / 60.8% | 129.1% / 98.1% / 60.8% |
| 6 | 127.6% / 103.1% / 60.8% | 518.4% / 96.3% / 60.8% | 107.9% / 98.8% / 60.8% | 92.2% / 100.4% / 60.8% | 170.6% / 97.3% / 60.8% |
| 7 | 122.3% / 101.7% / 60.8% | 511.2% / 99.8% / 60.8% | 109.4% / 100.8% / 60.8% | 96.1% / 100.9% / 60.8% | 165.6% / 97.8% / 60.8% |
| 8 | 124.0% / 102.7% / 60.8% | 474.8% / 100.5% / 60.8% | 110.5% / 98.1% / 60.8% | 97.7% / 99.9% / 60.8% | 170.2% / 99.9% / 60.8% |
| 9 | 139.8% / 102.2% / 60.8% | 501.8% / 97.4% / 60.8% | 107.3% / 97.8% / 60.8% | 96.2% / 101.1% / 60.8% | 168.0% / 100.5% / 60.8% |
| 10 | 151.6% / 101.2% / 60.8% | 523.5% / 101.2% / 60.8% | 116.6% / 98.0% / 60.8% | 98.1% / 100.7% / 60.8% | 161.9% / 98.1% / 60.8% |
| 11 | 136.9% / 103.2% / 60.8% | 513.4% / 100.1% / 60.8% | 100.2% / 99.5% / 60.8% | 97.3% / 101.3% / 60.8% | 169.2% / 99.9% / 60.8% |
| 12 | 128.1% / 101.3% / 60.8% | 508.6% / 98.0% / 60.8% | 112.6% / 98.6% / 60.8% | 99.7% / 100.9% / 60.8% | 167.6% / 98.4% / 60.8% |
| 13 | 139.7% / 99.7% / 60.8% | 483.1% / 98.9% / 60.8% | 120.6% / 97.5% / 60.8% | 101.5% / 100.1% / 60.8% | 160.5% / 98.4% / 60.8% |
| 14 | 103.6% / 101.4% / 60.8% | 518.4% / 98.4% / 60.8% | 106.4% / 97.7% / 60.8% | 102.4% / 101.6% / 60.8% | 168.1% / 98.0% / 60.8% |
| 15 | 123.3% / 103.2% / 60.8% | 537.5% / 98.4% / 60.8% | 105.2% / 99.4% / 60.8% | 103.9% / 101.4% / 60.8% | 163.5% / 97.6% / 60.8% |
| 16 | 120.1% / 102.8% / 60.8% | 285.7% / 98.8% / 60.8% | 92.2% / 99.0% / 60.8% | 103.7% / 101.4% / 60.8% | 127.4% / 100.7% / 60.8% |
| 17 | 134.2% / 101.4% / 60.8% | 264.2% / 98.5% / 60.8% | 96.8% / 100.6% / 60.8% | 97.4% / 102.1% / 60.8% | 127.9% / 98.7% / 60.8% |
| 18 | 146.2% / 101.0% / 60.8% | 442.5% / 98.8% / 60.8% | 78.8% / 98.8% / 60.8% | 97.4% / 99.7% / 60.8% | 110.3% / 98.4% / 60.8% |
| 19 | 127.1% / 99.9% / 60.8% | 465.1% / 100.6% / 60.8% | 81.4% / 99.2% / 60.8% | 110.2% / 101.2% / 60.8% | 111.7% / 98.3% / 60.8% |
| 20 | 129.4% / 101.0% / 60.8% | 446.1% / 98.0% / 60.8% | 85.0% / 100.7% / 60.8% | 97.4% / 101.1% / 60.8% | 110.4% / 98.2% / 60.8% |
| 21 | 139.1% / 101.4% / 60.8% | 411.2% / 100.7% / 60.8% | 79.8% / 100.7% / 60.8% | 102.9% / 100.4% / 60.8% | 114.9% / 97.8% / 60.8% |
| 22 | 120.2% / 99.9% / 60.8% | 444.7% / 98.7% / 60.8% | 81.0% / 97.4% / 60.8% | 97.7% / 100.4% / 60.8% | 112.0% / 97.3% / 60.8% |

[Full measurements and method](BENCHMARKS.md#current-evaluation) include the C++ and reference values for mixed input. [Other input measurements](BENCHMARKS_INPUTS.md) contain the full values for zero, repeated, structured, and random data. [Experiment history](PERFORMANCE_HISTORY.md) and the [performance work list](PERFORMANCE_PLAN.md) track progress separately.

To benchmark your own 1 MiB input, build the optimized `msvc-pair` or `gcc-pair` target and run `zstdpp_pair --level 3 --input path/to/file --iterations 1000 --trials 6`. The pair tool checks exact compressed bytes and both decoders.

## Project notes

- [Benchmark tables and methodology](BENCHMARKS.md)
- [Other input benchmark matrix](BENCHMARKS_INPUTS.md)
- [Performance task list](PERFORMANCE_PLAN.md)
- [Experiment history and prior reports](PERFORMANCE_HISTORY.md)
- [Bundled reference implementation](reference_zstd/README.md)

The headers live under `include/sph/zstd++/`; tests and acceptance checks live under `test/`, and the isolated benchmark workers and controllers live under `eval/`.
