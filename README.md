# sph-zstd++

A header-only C++23 Zstandard implementation built around compile-time configuration and
callback-driven streaming. The project is an incremental C++ port of the reference implementation
in `reference_zstd`.

## Interface

Both streams accept any callback invocable with `std::span<std::uint8_t const>`. The callback span
is valid for the duration of that invocation. Call `finish()` explicitly to complete or validate a
stream; destructors do not perform hidden output operations.

```cpp
#include <sph/zstd++/zstd.h>

#include <cstdint>
#include <array>
#include <span>
#include <vector>

std::vector<std::uint8_t> compressed;
auto compressor = sph::zstd::zstd_compress{
    [&compressed](std::span<std::uint8_t const> bytes)
    {
        compressed.insert(compressed.end(), bytes.begin(), bytes.end());
    }};

std::array<std::uint8_t, 3> const input{1, 2, 3};
compressor.update(std::uint8_t{42});
compressor.update(input);
compressor.finish();
```

Compile-time options use a structural non-type template parameter. The helper preserves the
callback's concrete type so it can be inlined:

```cpp
constexpr auto checked = []
{
    auto options = sph::zstd::compression_parameters{};
    options.checksum = true;
    options.window_log = 20;
    return options;
}();

auto compressor = sph::zstd::make_zstd_compress<checked>(callback);
```

Compression also provides `compress_frame(span)` for a complete frame; it borrows the input for
the duration of the call and avoids copying it into streaming history. Other useful operations and
observations include `update(span)`, `update(byte)`, `flush()` (compression), `finish()`, `reset()`,
`status()`, byte counters, completed frame count, and parsed frame information.

## Port status

The current working slice provides:

- valid standard and magicless frame headers;
- streaming raw, RLE, and single-match compressed-block output;
- compile-time resolution of the reference 1.6.0 compression-level presets, including
  source-size adjustment and explicit per-field overrides;
- a linear fast match finder with a compile-time-selected hash-table size;
- a persistent reference-compatible level-1 fast parser, with sequence-field parity checked
  against the bundled reference implementation;
- persistent reference-compatible double-fast, greedy, lazy2 hash-chain, and binary-tree lazy2 parsers;
- multi-sequence FSE entropy encoding with predefined, run-length, and normalized tables;
- native Huffman literal encoding for direct-weight alphabets, with one/four streams;
- Huffman literal decoding (new/repeat tables and one/four bitstreams), including
  direct and FSE-compressed Huffman weights;
- deterministic reference-compatible Huffman tree construction and compressed weight tables;
- the reference strategy-specific pre-block splitters;
- repeat-offset execution against an overlap-safe history bounded by the frame window;
- content-size fields and XXH64 content checksums;
- concatenated and skippable frames;
- explicit truncation, checksum, size, format, and state errors;
- interoperability tests in both directions against the downloaded reference library;
- reference-produced entropy frames at compression levels 1, 3, and 9; and
- corruption tests for matches outside the retained history window.

The level-1 fast, level-3 double-fast, level-5 greedy, levels 7/8 lazy, level-9 lazy2, and level-15 binary-tree lazy2
compressors produce byte-for-byte identical output to reference zstd 1.6.0 for the acceptance
corpus. These paths include exact sequence parsing, Huffman construction, normalized FSE sequence
tables, and strategy-specific automatic pre-block splitting. Dictionary and multi-threaded modes
are intentionally rejected until their implementations exist.

## Build and test

```text
cmake -S . -B out/build -DSPH_ZSTDPP_BUILD_TESTS=ON
cmake --build out/build
ctest --test-dir out/build --output-on-failure
```

Tests compile with warnings-as-errors on MSVC, Clang, and GCC-style frontends. The public
`sph::zstdpp` CMake target is header-only and does not link the reference implementation; only the
interoperability test target does.

Compression parity is a separate acceptance gate. It compresses the deterministic 1 MiB mixed
corpus at levels 1, 3, 5, 7, 8, 9, and 15 with both implementations, first verifies every resolved core
compression parameter, and then requires the encoded bytes to match exactly:

```text
cmake --preset msvc-acceptance
cmake --build --preset msvc-acceptance
ctest --preset msvc-acceptance
```

Equivalent `clang-acceptance` and `gcc-acceptance` presets are provided. All seven acceptance levels
are byte-identical.

The published performance matrix uses this parity-safe corpus. An expanded phase-reset corpus is
also exercised during performance work; it currently exposes a known level-8 difference caused by
the template implementation not yet having reference zstd's salted row-hash matcher.

### Presets and reference builds

Compiler-specific presets are provided for MSVC, Clang, and GCC. Reference presets expose and build
the downloaded `libzstd_static` target without enabling the C++ tests or evaluation matrix:

```text
cmake --preset msvc-release-reference
cmake --build --preset msvc-release-reference
```

Replace `msvc` with `clang` or `gcc` where appropriate. Evaluation presets build twenty isolated
workers—compression and decompression for each implementation at levels 1, 3, 5, 7, 8, 9, and 15—plus
the controller:

```text
cmake --preset msvc-release-eval
cmake --build --preset msvc-release-eval
cmake --build --preset msvc-run-eval
```

The final command validates every worker, writes `metrics.csv` and a graphical `METRICS.md` beside
it beneath the preset's build directory, and replaces the marked evaluation section below. The controller accepts
`--iterations`, `--output`, and `--readme` overrides when invoked directly.

<!-- SPH_ZSTDPP_EVAL_BEGIN -->

## Current evaluation

Generated by `zstdpp_eval` using **MSVC 19.51.36256.0**, Release, 20 timed iterations, and a deterministic 1 MiB mixed corpus. **Key: `C++` is sph-zstd++; `Reference` is reference zstd.** Code size is the complete worker executable; memory is peak resident set size, so both intentionally include runtime overhead.

Compression workers receive identical input. Decompression workers both consume the same reference-zstd-generated frame at each level, so their throughput and memory results use identical compressed bytes.

### Compression

| Level | C++ size (KiB) | Reference size (KiB) | C++ throughput (MiB/s) | Reference throughput (MiB/s) | C++ memory (MiB) | Reference memory (MiB) | C++ code (KiB) | Reference code (KiB) |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 259 | 259 | 1978 | 1998 | 7 | 7 | 106 | 382 |
| 3 | 274 | 274 | 1444 | 1515 | 7 | 8 | 107 | 382 |
| 5 | 274 | 274 | 1213 | 1176 | 10 | 9 | 107 | 382 |
| 7 | 259 | 259 | 2997 | 1430 | 13 | 12 | 109 | 382 |
| 8 | 260 | 260 | 755 | 1017 | 13 | 12 | 109 | 382 |
| 9 | 260 | 260 | 1168 | 876 | 19 | 17 | 109 | 382 |
| 15 | 260 | 260 | 166 | 212 | 24 | 23 | 110 | 382 |

### Decompression

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | C++ memory (MiB) | Reference memory (MiB) | C++ code (KiB) | Reference code (KiB) |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 3022 | 2861 | 7 | 7 | 85 | 114 |
| 3 | 4500 | 5288 | 7 | 7 | 85 | 114 |
| 5 | 5058 | 5247 | 7 | 7 | 85 | 114 |
| 7 | 15495 | 6094 | 7 | 7 | 85 | 114 |
| 8 | 16031 | 10538 | 7 | 7 | 85 | 114 |
| 9 | 16303 | 11450 | 7 | 7 | 85 | 114 |
| 15 | 17166 | 11618 | 7 | 7 | 85 | 114 |

<!-- SPH_ZSTDPP_EVAL_END -->
