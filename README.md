# sph-zstd++

A header-only C++23 implementation of [Zstandard](https://facebook.github.io/zstd/), built around compile-time compression settings and callback-driven streaming. This repository also contains the reference zstd implementation used for byte-level compatibility checks and matched performance comparisons.

The project is an active port, not a drop-in replacement for every zstd feature. Supported paths include streaming compression and decompression, Huffman and FSE entropy coding, checksums, concatenated and skippable frames, and the fast, double-fast, greedy, lazy, and binary-tree parsing strategies. Dictionary and multithreaded modes are not implemented. The acceptance corpus produces byte-for-byte identical compressed output at levels 1, 3, 5, 7, 8, 9, and 15; broader level and input coverage is exercised by the paired evaluator.

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

[Benchmark tables](BENCHMARKS.md) report C++ and reference throughput, peak memory, and complete worker code size, including percentages, for compression and decompression. The selected tables below show the last complete 22-level mixed-input compression screen and recent production-decoder and alternate-input measurements. These were **not all captured from one build**; the complete all-level screen predates later retained changes. A current-checkout all-level/all-input matrix still needs to be run, and no missing level/input measurements are inferred. The older all-level decompression matrix in the detailed report used an evaluator-only trusted-input decoder and must not be read as production-decoder throughput.

Higher throughput is better; lower memory and code size are better. Every percentage is C++ / Reference × 100.

### Mixed-input compression, levels 1–22

Last complete GCC 13 Release `-O3` screen, 1-MiB deterministic mixed input, 2026-10-24; six alternating trials per level, with exact compressed bytes and reciprocal decoding. See [methodology and newer focused results](BENCHMARKS.md).

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 / mixed, 1 MiB | 2272.44 | 2129.66 | 106.70 | 5.52 | 6.12 | 90.11 | 131.23 | 628.16 | 20.89 |
| 2 / mixed, 1 MiB | 2842.75 | 2514.11 | 113.07 | 5.63 | 6.24 | 90.29 | 127.15 | 628.16 | 20.24 |
| 3 / mixed, 1 MiB | 1865.05 | 1732.12 | 107.67 | 6.23 | 6.75 | 92.25 | 127.34 | 628.16 | 20.27 |
| 4 / mixed, 1 MiB | 1936.43 | 1651.40 | 117.26 | 7.76 | 8.03 | 96.69 | 127.34 | 628.16 | 20.27 |
| 5 / mixed, 1 MiB | 2281.13 | 2134.45 | 106.87 | 7.85 | 8.51 | 92.22 | 127.41 | 628.16 | 20.28 |
| 6 / mixed, 1 MiB | 2526.90 | 1908.92 | 132.37 | 7.76 | 8.49 | 91.33 | 122.82 | 628.16 | 19.55 |
| 7 / mixed, 1 MiB | 1981.91 | 1527.08 | 129.78 | 10.26 | 11.02 | 93.12 | 122.82 | 628.16 | 19.55 |
| 8 / mixed, 1 MiB | 1536.34 | 1200.21 | 128.00 | 10.22 | 11.01 | 92.88 | 122.82 | 628.16 | 19.55 |
| 9 / mixed, 1 MiB | 1162.91 | 974.56 | 119.33 | 15.26 | 16.00 | 95.36 | 122.82 | 628.16 | 19.55 |
| 10 / mixed, 1 MiB | 1155.97 | 982.07 | 117.71 | 15.24 | 16.00 | 95.22 | 122.83 | 628.16 | 19.55 |
| 11 / mixed, 1 MiB | 1080.65 | 890.42 | 121.36 | 15.25 | 16.04 | 95.08 | 122.83 | 628.16 | 19.55 |
| 12 / mixed, 1 MiB | 1088.96 | 931.19 | 116.94 | 15.24 | 16.02 | 95.10 | 122.83 | 628.16 | 19.55 |
| 13 / mixed, 1 MiB | 516.60 | 421.38 | 122.60 | 21.20 | 22.00 | 96.33 | 126.85 | 628.16 | 20.19 |
| 14 / mixed, 1 MiB | 491.91 | 428.03 | 114.92 | 21.20 | 21.99 | 96.39 | 122.85 | 628.16 | 19.56 |
| 15 / mixed, 1 MiB | 497.41 | 420.91 | 118.17 | 21.20 | 22.00 | 96.36 | 122.85 | 628.16 | 19.56 |
| 16 / mixed, 1 MiB | 100.21 | 94.89 | 105.60 | 21.47 | 21.87 | 98.18 | 140.66 | 628.16 | 22.39 |
| 17 / mixed, 1 MiB | 103.08 | 92.97 | 110.88 | 21.84 | 21.87 | 99.84 | 140.66 | 628.16 | 22.39 |
| 18 / mixed, 1 MiB | 74.23 | 58.49 | 126.91 | 22.34 | 22.38 | 99.81 | 141.02 | 628.16 | 22.45 |
| 19 / mixed, 1 MiB | 74.10 | 60.30 | 122.89 | 22.33 | 22.37 | 99.83 | 141.02 | 628.16 | 22.45 |
| 20 / mixed, 1 MiB | 75.07 | 58.77 | 127.74 | 22.31 | 22.38 | 99.69 | 141.02 | 628.16 | 22.45 |
| 21 / mixed, 1 MiB | 73.30 | 58.72 | 124.84 | 22.34 | 22.38 | 99.83 | 141.02 | 628.16 | 22.45 |
| 22 / mixed, 1 MiB | 76.03 | 59.34 | 128.12 | 22.34 | 22.37 | 99.86 | 141.02 | 628.16 | 22.45 |

### Other input shapes, optimized compression

Latest available focused paired reports, collected 2026-10-25 on the indicated compiler. They cover measured levels only.

MSVC Release, six alternating trials per 1-MiB input at levels 1, 8, 9, and 15:

| Input / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1-MiB varied text / 1 | 3219.31 | 3401.78 | 94.64% | 5.76 | 6.38 | 90.18% | 119.00 | 382.00 | 31.15% |
| 1-MiB structured text / 1 | 827.08 | 1091.71 | 75.76% | 5.91 | 6.57 | 89.90% | 119.00 | 382.00 | 31.15% |
| 1-MiB mixed input / 1 | 1937.37 | 2263.21 | 85.60% | 6.06 | 6.58 | 92.02% | 119.00 | 382.00 | 31.15% |
| 1-MiB mixed input / 8 | 1445.87 | 1362.37 | 106.13% | 11.02 | 11.45 | 96.31% | 115.00 | 382.00 | 30.10% |
| 1-MiB mixed input / 9 | 1272.34 | 1199.60 | 106.06% | 16.00 | 16.48 | 97.05% | 115.00 | 382.00 | 30.10% |
| 1-MiB varied text / 8 | 157.36 | 167.12 | 94.15% | 10.69 | 11.31 | 94.49% | 115.00 | 382.00 | 30.10% |
| 1-MiB varied text / 9 | 150.39 | 159.27 | 94.42% | 15.69 | 16.31 | 96.18% | 115.00 | 382.00 | 30.10% |
| 1-MiB repeated text / 15 | 1223.52 | 1227.72 | 99.66% | 21.66 | 22.28 | 97.23% | 116.00 | 382.00 | 30.37% |
| 1-MiB structured text / 15 | 833.92 | 730.85 | 114.10% | 21.67 | 22.34 | 97.03% | 116.00 | 382.00 | 30.37% |
| 1-MiB mixed input / 15 | 672.41 | 535.68 | 125.52% | 21.98 | 22.46 | 97.88% | 116.00 | 382.00 | 30.37% |

GCC 13 Release `-O3`, six alternating trials after the double-fast splitter-layout change; levels 2 (256 KiB), 3 and 4 (1 MiB):

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2 / varied, 256 KiB | 3123.22 | 3520.78 | 88.71 | 4.32 | 4.51 | 95.63 | 132.03 | 628.16 | 21.02 |
| 2 / balanced, 256 KiB | 2060.08 | 2112.22 | 97.53 | 4.58 | 4.45 | 102.99 | 132.03 | 628.16 | 21.02 |
| 2 / mixed, 256 KiB | 26805.51 | 9840.88 | 272.39 | 4.33 | 4.52 | 95.76 | 132.03 | 628.16 | 21.02 |
| 3 / varied, 1 MiB | 2630.16 | 2567.10 | 102.46 | 5.99 | 6.62 | 90.56 | 127.34 | 628.16 | 20.27 |
| 3 / mixed, 1 MiB | 1950.79 | 1745.24 | 111.78 | 6.26 | 6.71 | 93.33 | 127.34 | 628.16 | 20.27 |
| 4 / varied, 1 MiB | 2713.39 | 2548.81 | 106.46 | 7.51 | 7.85 | 95.69 | 127.34 | 628.16 | 20.27 |
| 4 / mixed, 1 MiB | 1926.41 | 1730.69 | 111.31 | 7.76 | 7.97 | 97.38 | 127.34 | 628.16 | 20.27 |

### Production decompression, varied and mixed input

MSVC Release `/O2 /Ob3 /Z7`, 1-MiB reference-produced frames, six alternating 10,000-frame trials per row on 2026-10-25. Both workers decoded identical bytes. See [additional GCC decoder measurements](BENCHMARKS.md#gcc-13-decompression-levels-3-and-16-1-mib).

| Input / operation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Varied text / decode L3 | 10333.97 | 8908.93 | 116.00% | 6.34 | 6.34 | 99.97% | 234.00 | 311.00 | 75.24% |
| Varied text / decode L12 | 9728.95 | 8919.41 | 109.08% | 6.34 | 6.34 | 99.94% | 234.00 | 311.00 | 75.24% |
| Varied text / decode L16 | 3660.21 | 4996.38 | 73.26% | 6.34 | 6.34 | 100.00% | 234.00 | 311.00 | 75.24% |
| Mixed / decode L3 | 7536.46 | 5816.44 | 129.57% | 6.60 | 6.57 | 100.54% | 234.00 | 311.00 | 75.24% |
| Mixed / decode L12 | 23980.84 | 15725.24 | 152.50% | 6.59 | 6.55 | 100.63% | 234.00 | 311.00 | 75.24% |
| Mixed / decode L16 | 17887.03 | 15136.28 | 118.17% | 6.59 | 6.56 | 100.54% | 234.00 | 311.00 | 75.24% |

For a fresh, byte-checked comparison at one level and input, build the optimized paired evaluator:

```powershell
cmake --preset msvc-release-eval
cmake --build --preset msvc-pair
.\out\build\msvc-release-eval\eval\bin\zstdpp_pair.exe --level 3 --iterations 1000 --trials 6
```

Pass `--input path\to\file` to use your own data. Its size must equal the configured `SPH_ZSTDPP_EVAL_CORPUS_SIZE` (1 MiB by default); configure a separate Release build for other sizes. The tool requires its four worker executables beside it, compares compressed bytes after every trial, checks both decoders against both frame encodings, and writes a fresh `SUMMARY.md` and raw trial files. The `gcc-pair` and `gcc-native-pair` presets provide equivalent Linux comparisons. See [benchmark methodology](BENCHMARKS.md#how-to-reproduce) for the full matrix and caveats.

## Project notes

- [Benchmark tables and methodology](BENCHMARKS.md)
- [Performance task list](PERFORMANCE_PLAN.md)
- [Experiment history and prior reports](PERFORMANCE_HISTORY.md)
- [Bundled reference implementation](reference_zstd/README.md)

The headers live under `include/sph/zstd++/`; tests and acceptance checks live under `test/`, and the isolated benchmark workers and controllers live under `eval/`.
