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

Replace `msvc` with `clang` or `gcc` where appropriate. Evaluation presets build twenty-eight isolated
workers—compression and decompression for each implementation at levels 1, 3, 5, 7, 8, 9, and 15—plus
the controller:

```text
cmake --preset msvc-release-eval
cmake --build --preset msvc-release-eval
cmake --build --preset msvc-run-eval
```

The final command validates every worker, writes `metrics.csv` and a `METRICS.md` report beside
it beneath the preset's build directory, and replaces the marked evaluation section below. The controller accepts
`--iterations`, `--trials`, `--output`, and `--readme` overrides when invoked directly.
The default is six alternating trials per level and operation. Reported throughput
uses median elapsed time, and reported memory is median peak RSS. Every individual
measurement is retained in `trials.csv` and per-trial worker CSVs; `metrics.csv`
contains the aggregates. The variability table includes throughput ranges and
paired win counts. Use a fresh output directory for each evaluation.

### WSL Linux builds and profiling

Run these commands from the repository root inside WSL. Select both compilers explicitly;
the GCC preset's compiler settings can override `CC`/`CXX` environment variables.
Use a separate build directory when changing compiler versions:

```bash
cmake --preset gcc-release-eval -B out/build/gcc13-release-eval \
  -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13
cmake --build out/build/gcc13-release-eval -j4
./out/build/gcc13-release-eval/eval/bin/zstdpp_eval --iterations 100
```

The evaluator generates `out/build/gcc13-release-eval/eval/results/corpus.bin` and
updates the README benchmark section. Linux workers have no `.exe` suffix; `l3`
means lowercase `l` followed by level `3`.

The default evaluation covers levels 1, 3, 5, 7, 8, 9, and 15, not every
compression level. Configure `-DSPH_ZSTDPP_EVAL_LEVELS="1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17;18;19;20;21;22"`
to build all positive levels, or supply a smaller semicolon-separated list.
The controller uses that same configured list and refuses to update its report
if candidate compressed bytes differ from the reference. Building a level does
not establish that its parser is reference-equivalent.

New evaluator builds use the normal checked C++ decoder. Historical benchmark
tables below were produced with an evaluator-only trusted-input decoder and do
not establish the production decoder's throughput; they need regeneration.

Store profiler recordings on the Linux filesystem: recording to `/mnt/c` produced
`Bad address` on the tested WSL host. For example, after running the evaluator:

```bash
perf record -o /tmp/zstdpp-l3.perf -e cpu-clock -F 499 -- \
  ./out/build/gcc13-release-eval/eval/bin/sph_compress_l3 \
  out/build/gcc13-release-eval/eval/results/corpus.bin \
  /tmp/zstdpp-l3.zst 5000 /tmp/zstdpp-l3.csv
DEBUGINFOD_URLS= perf report -i /tmp/zstdpp-l3.perf --stdio --no-children
```

Repeat with `reference_compress_l3` and a different recording filename. Both builds
use Release optimization. For source-level profiles, additionally configure
`-DCMAKE_C_FLAGS_RELEASE="-O3 -g -DNDEBUG"` and
`-DCMAKE_CXX_FLAGS_RELEASE="-O3 -g -DNDEBUG"`, then rebuild both implementations.

For a separate host-specific comparison, configure another build directory with
`-DCMAKE_C_FLAGS_RELEASE="-O3 -march=native -DNDEBUG"` and
`-DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=native -DNDEBUG"`. Apply CPU targeting to
both implementations and keep these results separate from generic builds.
Native-targeted binaries may not run on older or different CPUs.

Current optimal-parser validation (2026-09-25): the integrated post-sequence
splitter closes the remaining 283-byte gap at levels 16 and 17. Both now produce
263,655-byte frames identical to reference defaults on the 1 MiB mixed corpus.
The three-byte optimal-match path now also closes the mixed-corpus gap at levels
18–22: all five produce 266,850-byte frames identical to reference defaults.
The collector checks repeat offsets, a dedicated three-byte hash table, and then
the binary tree in reference order; the parser can relax three-byte matches.
The new table is allocated at construction and uses the existing aligned allocator.
GCC and MSVC acceptance now cover levels **1–22**, including cross-decoding and
reset paths. Optimized GCC13 workers at levels 16–22 pass the post-warmup allocation
guard. These are corpus-specific parity checks, not exhaustive format validation.

A broader audit now passes byte-for-byte parity and cross-decoding for all nine
combinations of levels 16, 18, and 22 with rotated mixed, varied-text, and random
corpora. This remains corpus-specific coverage, not an exhaustive format proof.
Enabling reference's
optimal Huffman-depth search in the ultra writer fixed the text-first level-18
case: its frame shrank from 266,843 to the reference's 266,842 bytes. That case is
now an acceptance regression, and the tree cache includes the depth policy.
Subsequent fixes to longest-match selection, block-end path bounds, full-byte
Huffman alphabets, and the 256-literal four-stream threshold close the level-16
varied-text gap completely: both implementations emit identical 24,022-byte frames.
This is now an acceptance regression. Unit tests additionally cross-decode new
and repeated full-byte Huffman tables at the stream/header thresholds.
Porting ultra's match-plus-one-literal lookahead also closes the level-18
varied-text gap: both frames are now identical at 21,383 bytes, with an additional
acceptance regression. Ultra2's first-block statistics prepass closes the level-22
cases too: varied text is 18,586 bytes and text-first is 266,853 bytes, both exact.
The prepass reuses the parser workspace and advances the match-history epoch,
retaining only entropy statistics and restoring the initial repeat offsets.
It adds no post-warmup allocation; both level-22 cases are acceptance regressions.
GCC13 Release and MSVC Debug unit and acceptance tests pass, as does the nine-case
cross-decoding audit. These correctness results do not establish a general speedup.
The historical optimization measurements in the following paragraphs remain
scoped to the original mixed corpus and predate this writer-policy correction.
The all-level evaluation at the end of this section includes the earlier
optimal-depth correction but predates the subsequent parser/alphabet/stream fixes.

Latest Huffman writer improvement: resizing once and
keeping the output cursor and bit accumulator local removes per-word vector
resizing. Five alternating 10,000-frame baseline/candidate/reference trials on
varied text show level1 improving from2774.10 to2920.02 MiB/s (+5.26%), and
level3 from2003.44 to2060.77 (+2.86%). Before/after ranges overlap slightly;
an independent repeat shows +7.66%/+1.24%, respectively. These are not wins over
reference. The implementation reserves about48KiB more Huffman scratch storage
at a128KiB block size; measured RSS below includes the change. Generic GCC13
Release `-O3 -DNDEBUG`, WSL CPU2; median throughput and whole-process peak RSS,
complete executable size. GCC13 unit/acceptance, MSVC Debug unit tests, and
ASan/UBSan unit tests pass. All22 rebuilt GCC13 compression levels also pass
exact-frame and reciprocal-decoding checks on both mixed and varied corpora.
The full short timing screen is being checked with longer controlled runs;
the older performance tables below retain their original scope.
Actual mixed-corpus confirmation uses the same five10,000-frame triples:
level1 improves2271.26 →2301.37 MiB/s (+1.33%) and level3 improves1620.25
→1661.10 (+2.52%), with overlapping ranges. Separate level3 varied counters
drop63.394B →61.206B instructions (-3.5%) and23.259B →22.682B cycles (-2.5%).

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 / varied text | 2920.02 | 3434.98 | 85.01 | 5.12 | 5.83 | 87.81 | 122.92 | 628.16 | 19.57 |
| 3 / varied text | 2060.77 | 2562.60 | 80.42 | 5.99 | 6.61 | 90.61 | 122.94 | 628.16 | 19.57 |
| 1 / mixed | 2301.37 | 2087.91 | 110.22 | 5.37 | 6.11 | 87.92 | 122.92 | 628.16 | 19.57 |
| 3 / mixed | 1661.10 | 1650.18 | 100.66 | 6.21 | 6.72 | 92.45 | 122.94 | 628.16 | 19.57 |

Broader post-Huffman controls use five alternating baseline/candidate/reference
triples,500 frames per trial (level22:30 frames), with the same compiler and
measurement definitions. Varied5/6 median changes are +4.64%/+7.09%, while
mixed5/6/13/22 changes are -0.87%/-1.30%/-4.12%/-4.54%. All of these
before/after ranges overlap: they do not establish either universal gains or
universal regressions. The short screen's large apparent level5 and22 losses
are not reliable estimates. Candidate/reference measurements:

The direct-writer-only level22 varied-text build was a confirmed exception: throughput fell2.6900 →2.5665
MiB/s (-4.59%), with disjoint ranges2.6495–2.7358 versus2.5354–2.5747.
Profiling located a generated-code change in optimal match relaxation, not
substantial Huffman runtime. The retained follow-up prevalidated relaxation
path keeps checks for general callers and debug assertions for
production calls already bounded by the parser's path window. Five varied-text
triples now measure2.3761 →2.4132 MiB/s (+1.56%) against the original pre-Huffman
baseline, with overlapping ranges2.3530–2.6629 versus2.3851–2.5232. Thus this
run does not reproduce the previous clear loss, but does not establish a robust
gain either. It executes13.5% fewer instructions than the direct-writer-only
build. Five300-frame mixed triples measure37.35 →39.18 MiB/s (+4.91%),
with very broad overlapping ranges32.87–73.51 versus35.82–76.62; this is
not a stable speedup estimate. GCC13 Release and MSVC Debug unit/acceptance
suites pass, as do ASan/UBSan unit tests and an assertion-enabled level22
varied-text worker with exact output. The broader16–21 comparison below is
complete, with clearer gains at20–21 and no established cross-corpus regression.

| Level / follow-up corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 22 / varied text | 2.41 | 1.63 | 148.40 | 21.70 | 22.25 | 97.54 | 136.98 | 628.16 | 21.81 |
| 22 / mixed | 39.18 | 26.22 | 149.43 | 22.32 | 22.34 | 99.93 | 136.98 | 628.16 | 21.81 |

The completed level16–21 follow-ups compare prevalidated relaxation against the
direct-writer-only baseline (unlike the level22 comparison against the original
pre-Huffman baseline). Five alternating triples show varied text7.7471 →7.7883
MiB/s (+0.53%,100 frames/trial), and mixed93.3294 →90.3348 (-3.21%,300 frames).
Before/after ranges overlap on both corpora; neither result establishes a change.
Level17 changes are likewise inconclusive: varied7.5679 →7.6436 (+1.00%) and
mixed84.6829 →82.4056 (-2.69%), with overlapping before/after ranges and the
same trial counts. Level18 varied text is unchanged5.6726 →5.6618 (-0.19%);
mixed rises62.9098 →70.1364 (+11.49%), although before/after ranges overlap
(60.85–71.98 versus63.04–74.01). Its mixed candidate/reference ranges are
disjoint in this run, but varied16–18 remain clearly slower than reference.
Level19 medians increase2.6215 →2.7661 (+5.52%) on varied text and
54.8927 →61.1665 (+11.43%) on mixed data. Before/after ranges overlap here
too; these are provisional gains, not precise speedup guarantees. Varied19
uses20 frames/trial, mixed19 uses300. Level20 uses the same counts and shows
a clearer varied-text gain:2.5807 →2.7526 MiB/s (+6.66%), with disjoint
before/after ranges2.5332–2.6487 versus2.6815–2.7972. Mixed20 rises64.8786
→70.0875 (+8.03%), but its ranges overlap. Level21 varied text improves
2.5977 →2.7089 (+4.28%), also with disjoint ranges2.4535–2.6147 versus
2.6737–2.7393; mixed21 is unchanged68.0732 →68.0148 (-0.09%, overlapping
ranges). These20–21 results support retaining the specialization, but do not
close the remaining reference-speed gaps. Follow-up metrics:

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 / varied text | 7.79 | 8.50 | 91.66 | 21.20 | 21.71 | 97.66 | 136.55 | 628.16 | 21.74 |
| 16 / mixed | 90.33 | 94.56 | 95.53 | 21.45 | 21.88 | 98.04 | 136.55 | 628.16 | 21.74 |
| 17 / varied text | 7.64 | 8.41 | 90.93 | 21.20 | 21.75 | 97.47 | 136.55 | 628.16 | 21.74 |
| 17 / mixed | 82.41 | 91.29 | 90.27 | 21.79 | 21.83 | 99.80 | 136.55 | 628.16 | 21.74 |
| 18 / varied text | 5.66 | 6.14 | 92.23 | 21.76 | 22.23 | 97.86 | 136.98 | 628.16 | 21.81 |
| 18 / mixed | 70.14 | 59.57 | 117.73 | 22.32 | 22.40 | 99.67 | 136.98 | 628.16 | 21.81 |
| 19 / varied text | 2.77 | 1.79 | 154.71 | 21.70 | 22.23 | 97.61 | 136.98 | 628.16 | 21.81 |
| 19 / mixed | 61.17 | 52.01 | 117.60 | 22.32 | 22.38 | 99.77 | 136.98 | 628.16 | 21.81 |
| 20 / varied text | 2.75 | 1.80 | 153.25 | 21.70 | 22.27 | 97.42 | 136.98 | 628.16 | 21.81 |
| 20 / mixed | 70.09 | 61.32 | 114.31 | 22.32 | 22.36 | 99.83 | 136.98 | 628.16 | 21.81 |
| 21 / varied text | 2.71 | 1.78 | 151.91 | 21.70 | 22.25 | 97.52 | 136.98 | 628.16 | 21.81 |
| 21 / mixed | 68.01 | 56.13 | 121.16 | 22.32 | 22.38 | 99.77 | 136.98 | 628.16 | 21.81 |

Earlier direct-writer-only controls:

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 / mixed | 2279.83 | 2061.05 | 110.62 | 7.87 | 8.48 | 92.86 | 122.50 | 628.16 | 19.50 |
| 6 / mixed | 2108.01 | 1805.86 | 116.73 | 7.75 | 8.52 | 91.01 | 122.58 | 628.16 | 19.51 |
| 13 / mixed | 490.00 | 387.79 | 126.36 | 21.22 | 21.98 | 96.55 | 126.49 | 628.16 | 20.14 |
| 22 / mixed | 57.67 | 44.50 | 129.62 | 22.32 | 22.38 | 99.74 | 136.98 | 628.16 | 21.81 |
| 5 / varied text | 207.80 | 237.60 | 87.46 | 7.46 | 8.38 | 89.04 | 122.50 | 628.16 | 19.50 |
| 6 / varied text | 231.54 | 295.92 | 78.25 | 7.50 | 8.38 | 89.51 | 122.58 | 628.16 | 19.51 |
| 13 / varied text | 156.44 | 127.19 | 123.00 | 21.00 | 21.87 | 96.05 | 126.49 | 628.16 | 20.14 |
| 22 / varied text | 2.57 | 1.77 | 145.12 | 21.66 | 22.26 | 97.32 | 136.98 | 628.16 | 21.81 |

Latest row-matcher improvement: greedy parsing keeps insertion-table pointers,
hash salt, frame origin, and cursor local to each batch, together with a
branch-free circular-index wrap. Lazy parsers retain the prior path: applying
the same change there regressed level9. No additional allocations or workspace.

Five alternating 500-frame varied-text baseline/candidate/reference triples
measured level5 at189.26 /216.20 /258.99 MiB/s: **14.2% faster than the prior
C++ version**, but still83.48% of reference in that run. Before/after ranges
overlap (178.90–198.86 versus194.50–231.96). Separate1000-frame counters show
instructions49.990B →41.679B (-16.6%), branches3.093B →2.060B (-33.4%),
branch misses66.858M →13.365M (-80.0%), and cycles24.399B →21.536B (-11.7%).
Hoisting alone and branch-free wrap alone did not establish a gain.

Final greedy-only build controls below use generic GCC13 Release `-O3 -DNDEBUG`,
WSL CPU2, three alternating C++/reference pairs (500 mixed frames,100 varied
frames). Values are median throughput, whole-process peak RSS, and complete
executable size. These controls are not before/after comparisons. In particular,
level5 varied-text timings fluctuate substantially; its near-reference result
here does not establish a consistent win over reference.

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 / mixed | 2338.97 | 2005.38 | 116.63 | 7.84 | 8.50 | 92.23 | 122.70 | 628.16 | 19.53 |
| 5 / varied text | 182.40 | 182.52 | 99.94 | 7.50 | 8.25 | 90.86 | 122.70 | 628.16 | 19.53 |
| 9 / mixed | 1201.85 | 990.70 | 121.31 | 15.25 | 16.04 | 95.08 | 122.60 | 628.16 | 19.52 |
| 9 / varied text | 157.81 | 172.76 | 91.35 | 14.96 | 15.86 | 94.31 | 122.60 | 628.16 | 19.52 |

All timed frames are reference-identical and allocation guards pass. GCC13
Release and MSVC Debug unit/expanded acceptance tests pass, as do ASan/UBSan
unit tests and the level5 varied-text reset/reuse worker. Raw results:
`out/full-metrics-20260926/{row-context-wrap-*,row-context-wrap5-perf-*,row-greedy-final-*}`.

Earlier row-matcher improvement: stop clearing the 64-entry candidate scratch
buffer before every search. Collection writes every element in the returned
prefix, and only that prefix is consumed, matching the reference's scratch
buffer contract. No allocation or workspace-size change.

Five alternating 500-frame baseline/C++/reference triples on varied text give
median gains of **4.5% at level5** (149.65 →156.36 MiB/s) and **5.9% at level9**
(136.25 →144.25). Level5 before/after ranges overlap (141.46–158.69 versus
152.28–165.39); level9 ranges do not (130.12–136.65 versus136.84–147.56).
Separate1000-frame level5 counters show cycles28.350B →26.941B (-5.0%),
instructions50.046B →49.990B (-0.1%), and essentially unchanged branches.

Current metrics: generic GCC13 Release `-O3 -DNDEBUG`, WSL CPU2. Varied-text
rows use the five-trial triples above; mixed controls use three alternating
500-frame pairs. Values are median throughput and whole-process peak RSS;
code is the complete executable. All frames are reference-identical and
post-warmup allocation guards pass.

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 / mixed | 1924.19 | 1987.10 | 96.83 | 7.88 | 8.55 | 92.10 | 122.70 | 628.16 | 19.53 |
| 5 / varied text | 156.36 | 174.24 | 89.74 | 7.50 | 8.42 | 89.05 | 122.70 | 628.16 | 19.53 |
| 9 / mixed | 1056.78 | 795.06 | 132.92 | 15.25 | 15.95 | 95.62 | 122.60 | 628.16 | 19.52 |
| 9 / varied text | 144.25 | 152.67 | 94.49 | 14.99 | 15.91 | 94.21 | 122.60 | 628.16 | 19.52 |

Mixed controls have overlapping C++/reference ranges and are not controlled
before/after comparisons. Varied-text still trails reference. Raw data:
`out/full-metrics-20260926/row-scratch-*`; summary scripts accept stage names
to preserve prior experiments.

Earlier row-matcher improvement: remove the repeated input-bound check from
each cached hash. The parser's reference-equivalent `blockEnd-16` cutoff
already leaves room for an eight-byte lookahead plus an eight-byte load;
debug builds assert that invariant. New tests exercise exact-size buffers
around the cutoff. No additional memory or allocations.

Five alternating 500-frame baseline/C++/reference triples on varied text
measure C++ median gains of **4.1% at level5** (141.78 → 147.65 MiB/s) and
**6.9% at level9** (124.53 → 133.07). Before/after ranges overlap, so these
are measured medians, not guaranteed gains on every run. Level5 counters on
1000 frames independently show instructions53.101B →50.046B (-5.8%) and
branches4.133B →3.093B (-25.2%); cycles28.923B →27.911B (-3.5%).

Current metrics: GCC13 Release `-O3 -DNDEBUG`, WSL CPU2. Varied-text rows use
the five-trial triples above; mixed controls use three alternating 500-frame
pairs. Throughput and peak process RSS are medians; code is the complete
executable. Every frame matches reference defaults and allocation guards pass.

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 / mixed | 1915.61 | 1807.90 | 105.96 | 7.88 | 8.54 | 92.18 | 122.70 | 628.16 | 19.53 |
| 5 / varied text | 147.65 | 169.11 | 87.31 | 7.46 | 8.37 | 89.17 | 122.70 | 628.16 | 19.53 |
| 9 / mixed | 818.30 | 857.50 | 95.43 | 15.25 | 16.00 | 95.31 | 122.60 | 628.16 | 19.52 |
| 9 / varied text | 133.07 | 150.51 | 88.41 | 14.95 | 15.86 | 94.21 | 122.60 | 628.16 | 19.52 |

Baseline/current varied-text ranges: level5 137.12–152.57 /143.08–150.72,
level9 120.23–127.68 /124.73–139.06 MiB/s. Mixed controls are not a controlled
before/after comparison and have overlapping C++/reference ranges. This is
progress, not reference-speed parity on varied text. Raw data and scripts:
`out/full-metrics-20260926/row-bounds-*`.
GCC13 Release and MSVC Debug unit/expanded acceptance tests pass. ASan/UBSan
unit tests (including exact-size production buffers) and the level9 reset/reuse
worker pass. The sanitizer run also exposed and corrected an older low-level
cache test fixture that lacked the lookahead required by its pointer-based API.
GCC13 Release and MSVC Debug unit/expanded acceptance tests pass. ASan/UBSan
unit tests (including exact-size production buffers) and the level9 reset/reuse
worker pass. The sanitizer run also exposed and corrected an older low-level
cache test fixture that lacked the lookahead required by its pointer-based API.

Earlier architectural correction (2026-09-26): greedy/lazy levels now select the
SIMD row matcher under the same default window-size policy as reference. This
closes varied-text byte mismatches at levels5–12, including reset/reuse. The
production row path combines bounded updates, rolling hashes, SIMD tag matching,
prefetching and frame epochs without allocating chain tables. It also matches
the reference's extra eight-byte search cutoff for rolling-cache lookahead.
Expanded GCC13 Release and MSVC Debug acceptance pass for all eight formerly failing levels; focused
tests cover candidate order, salt entropy, stale-frame rejection and rollover.
The GCC13 level-5 varied-text/reset worker also passes AddressSanitizer and
UndefinedBehaviorSanitizer with reference-identical output.

Parity-correct baseline, **not a speedup claim**: three alternating pairs per
case, generic GCC13 Release `-O3 -DNDEBUG`, WSL CPU2; 500 frames per mixed
trial, 100 per varied-text trial. Medians below; every trial is reference-exact
and passes allocation guards. Memory is whole-process peak RSS; code is the
complete executable. Varied-text timings show a persistent ~15% throughput
gap to address next; mixed timings have wider/overlapping ranges.

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 / mixed | 1715.20 | 1766.66 | 97.09 | 7.87 | 8.54 | 92.18 | 122.70 | 628.16 | 19.53 |
| 5 / varied text | 154.20 | 180.67 | 85.35 | 7.50 | 8.33 | 90.01 | 122.70 | 628.16 | 19.53 |
| 9 / mixed | 750.10 | 903.00 | 83.07 | 15.21 | 16.04 | 94.81 | 122.60 | 628.16 | 19.52 |
| 9 / varied text | 141.16 | 165.31 | 85.39 | 15.00 | 15.86 | 94.56 | 122.60 | 628.16 | 19.52 |

Varied-text C++/reference ranges: level5 146.41–159.50 /176.01–185.24,
level9 136.09–143.57 /162.52–175.77 MiB/s. Raw trials and summarizer are in
`out/full-metrics-20260926/row-l*-*.csv` and `summarize-row.ps1`. Older
mixed-corpus tables below predate row-matcher integration and are historical.

Earlier improvement: hoist zero-literal pricing and the one-literal lookahead
price difference out of the optimal parser's candidate-length loops. These
prices are constant until the selected path updates the frequency statistics.
Five alternating 20-frame level-22 varied-text trials measured **2.8453 MiB/s**,
up **51.1%** from the saved full-price-cache baseline (1.8835), versus reference
1.7974 (**158.30% of reference**). Baseline/candidate/reference ranges were
1.8331–1.9396 / 2.7619–2.8730 / 1.7631–1.8441 MiB/s. A separate three-trial
screen also showed a clear gain (+44.3%). All frames remain byte-identical.

Current metrics (GCC13 Release `-O3 -DNDEBUG`, WSL CPU 2). Level22 uses five
alternating triples, 20 frames per varied-text trial and 100 per mixed trial;
level16/18 controls use three 20-frame triples. Throughput and peak process RSS
are medians; code is the complete executable, not just its text section.

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 / varied text | 7.18 | 8.16 | 88.03 | 21.20 | 21.73 | 97.54 | 136.67 | 628.16 | 21.76 |
| 18 / varied text | 4.95 | 5.46 | 90.78 | 21.70 | 22.31 | 97.27 | 136.88 | 628.16 | 21.79 |
| 22 / mixed | 42.80 | 32.53 | 131.55 | 22.32 | 22.38 | 99.77 | 136.98 | 628.16 | 21.81 |
| 22 / varied text | 2.85 | 1.80 | 158.30 | 21.70 | 22.24 | 97.56 | 136.98 | 628.16 | 21.81 |

No additional workspace or allocations. On 20 varied-text22 frames, `perf`
measured instructions 167.651B → 133.321B (-20.5%), branches 34.031B →
25.160B (-26.1%), and cycles 54.159B → 37.315B (-31.1%). Reference measured
162.519B instructions and 56.893B cycles. These counter runs are separate from
the timing medians. Level16/18 controls moved -0.7%/-2.4% versus their saved
baselines with overlapping ranges; mixed22 moved -1.8% with very wide ranges.
Those controls do not establish improvement and levels16/18 still lag reference.
GCC13 Release and MSVC Debug unit/all-level acceptance, plus all nine broader corpus parity/cross-decoding
cases pass; post-warmup allocation guards remain clean. This is a strong
level22 workload-specific gain, not completion of the all-level objective.

Earlier follow-up: cache complete offset/match-length symbol-price contributions,
including extra bits and base prices, in the same 340-byte workspace. This removes
more arithmetic from candidate-length evaluation without additional allocation.
Five alternating 20-frame level-22 varied-text trials measured **1.7564 MiB/s**,
up **3.8%** from the saved weight-cache baseline (1.6927), versus reference
1.6440 (**106.84% of reference**). Candidate/reference ranges were
1.7138–1.7842 / 1.6294–1.6874 MiB/s; baseline range was 1.6239–1.7378.
An earlier three-trial screen also favored the candidate (+5.5%). These are
workload-specific results, not an all-level win.

Current controlled follow-up (GCC13 Release `-O3 -DNDEBUG`, WSL CPU 2;
five 20-frame triples at level 22, three 30-frame triples at 16/18).
Throughput and peak process RSS are medians; code is the complete executable.
Every trial's compressed frame matches both reference and saved baseline.

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 / varied text | 7.52 | 8.52 | 88.30 | 21.20 | 21.80 | 97.21 | 136.67 | 628.16 | 21.76 |
| 18 / varied text | 5.44 | 5.74 | 94.79 | 21.70 | 22.23 | 97.59 | 137.02 | 628.16 | 21.81 |
| 22 / mixed | 42.21 | 31.24 | 135.15 | 22.32 | 22.39 | 99.70 | 136.98 | 628.16 | 21.81 |
| 22 / varied text | 1.76 | 1.64 | 106.84 | 21.70 | 22.23 | 97.61 | 136.98 | 628.16 | 21.81 |

Level-16/18 medians moved +0.3%/+4.8% versus the saved C++ workers, with
overlapping trial ranges. Mixed22 varied widely (baseline 30.68–56.02,
candidate 39.73–44.71, reference 27.45–44.93 MiB/s); do not interpret its
median change as a reliable gain. Compare simultaneous trials, not absolute
rates across the historical snapshots below. Allocation guards pass throughout.
GCC13 Release unit and all-level acceptance tests pass, including reciprocal
cross-decoding. Cache-equivalence tests cover both endpoints of all 53
match-length symbols across all 32 offset codes and both pricing modes.

Earlier optimal-parser improvement: cache the 85 offset/match-length frequency
weights between statistics updates instead of recalculating them for every
candidate length. This adds 340 bytes of fixed cache data and no allocation.
On level-22 varied text, five alternating 20-frame baseline/C++/reference trials
improve the C++ median **41.4%, from 1.2568 to 1.7771 MiB/s**. Reference is
1.7691 MiB/s; overlapping ranges mean the 0.46% lead is effectively a tie, not
an established win. Baseline/current/reference ranges are 1.2463–1.2699,
1.7401–1.7863, and 1.7554–1.7802 MiB/s. All frames are identical.

Complete follow-up metrics (generic GCC13 `-O3 -DNDEBUG`, WSL CPU 2; five
20-frame trials at level 22, three 30-frame trials at levels 16/18). Throughput
and peak process RSS are medians; code is the complete executable, and percentages
use unrounded values:

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 / mixed | 110.67 | 109.82 | 100.77 | 21.48 | 21.87 | 98.21 | 136.67 | 628.16 | 21.76 |
| 16 / varied text | 7.82 | 8.90 | 87.90 | 21.19 | 21.74 | 97.47 | 136.67 | 628.16 | 21.76 |
| 18 / mixed | 66.10 | 62.28 | 106.13 | 22.29 | 22.36 | 99.69 | 136.88 | 628.16 | 21.79 |
| 18 / varied text | 5.58 | 6.14 | 90.89 | 21.70 | 22.26 | 97.47 | 136.88 | 628.16 | 21.79 |
| 22 / mixed | 68.77 | 55.22 | 124.55 | 22.32 | 22.47 | 99.34 | 136.98 | 628.16 | 21.81 |
| 22 / varied text | 1.78 | 1.77 | 100.46 | 21.70 | 22.26 | 97.47 | 136.98 | 628.16 | 21.81 |

The short mixed controls moved +6.1%, +0.9%, and +6.5% versus their saved C++
baselines; they are too timing-sensitive to establish those as separate gains.
Varied-text levels 16/18 moved -1.7%/-2.8%, with overlapping before/after ranges;
they remain slower than reference. Follow-up work should avoid cache-refresh
overhead where fewer candidate lengths are examined. On level-22 varied text,
20-frame instruction counts fell 25.3% (243.394B → 181.858B), and cycles fell
29.0%. All worker allocation guards and the nine-case corpus audit pass.
GCC13, GCC16, and MSVC unit/acceptance suites pass; unit tests verify cached
prices against the original formula after initialization and statistics updates.
This is a demonstrated workload-specific improvement, not an all-level victory.

Earlier level-16 follow-up after the full-byte Huffman/stream fixes: five alternating
100-frame saved-baseline/C++/reference trials, generic GCC13 `-O3 -DNDEBUG`, WSL
CPU 2. Every current C++ frame equals reference and passes the post-warmup
allocation guard. Memory is median peak process RSS; code is the complete worker
executable. Percentages use unrounded values.

| Corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Mixed | 74.59 | 73.45 | 101.54 | 21.45 | 21.93 | 97.81 | 136.36 | 628.16 | 21.71 |
| Varied text | 7.49 | 8.18 | 91.66 | 21.20 | 21.77 | 97.40 | 136.36 | 628.16 | 21.71 |

The saved C++ baseline medians were 68.75 and 7.52 MiB/s, respectively.
Mixed-corpus ranges overlap substantially (baseline 54.20–76.04, current
69.48–77.87, reference 61.53–79.43), so the apparent 8.5% baseline improvement
and 1.5% reference lead are not established speedups. Varied-text throughput is
essentially unchanged from baseline (-0.3%), but remains 8.3% behind reference;
current/reference ranges are 7.46–7.69 and 8.09–8.23 MiB/s. Its old baseline
frame was not reference-identical; the current one is. This now provides a
parity-valid workload for profiling that remaining performance gap.

Level-18 lookahead follow-up (three alternating 100-frame trials, same GCC13
Release/CPU-2 setup; medians, peak RSS, complete executable size):

| Corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Mixed | 69.94 | 72.65 | 96.27 | 22.32 | 22.37 | 99.81 | 136.81 | 628.16 | 21.78 |
| Varied text | 5.65 | 6.22 | 90.79 | 21.70 | 22.24 | 97.56 | 136.81 | 628.16 | 21.78 |

Every current frame matches reference and passes the allocation guard. This is
a correctness fix, not a speedup: saved-baseline medians were 75.28 and 5.71 MiB/s.
Mixed timings are noisy (baseline 49.32–78.69, current 67.68–70.30, reference
66.32–78.39). Varied text is about 1.2% below the old, non-identical baseline,
and remains 9.2% behind reference. These measurements supersede any implication
that the earlier mixed-corpus level-18 lead establishes a general performance win.

Level-22 screen after adding the required statistics prepass (three alternating
30-frame C++/reference pairs, generic GCC13 Release, WSL CPU 2):

| Corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Mixed | 76.18 | 66.03 | 115.37 | 22.32 | 22.37 | 99.81 | 136.77 | 628.16 | 21.77 |
| Varied text | 1.25 | 1.79 | 69.75 | 21.70 | 22.26 | 97.49 | 136.77 | 628.16 | 21.77 |

Frames are exact and allocation guards pass. These are medians, peak process RSS,
and complete executable sizes; percentages use unrounded values. Mixed ranges
are 71.96–78.60 versus 59.34–70.64 MiB/s; the short runs warrant longer validation.
Varied-text ranges are 1.23–1.26 versus 1.77–1.79: a clear remaining deficit of
about 30.3%. This is a new parity-valid baseline, not a speedup over the old
single-pass implementation, which emitted different frames on this input.

Earlier three-byte repeat-prefix measurements:

Rejecting three-byte repeat misses before entering the full match counter improved
level 18 substantially. Five alternating 100-frame baseline/candidate/reference
trials (generic GCC13 `-O3 -DNDEBUG`, WSL CPU 2) measured medians of
**32.80 / 48.23 / 37.69 MiB/s**: **47.0% faster than the saved C++ baseline**
and **28.0% faster than reference** on this corpus. Candidate range was
46.61–51.14 MiB/s, compared with baseline 31.70–34.41 and reference 33.56–40.58.
Every trial produced identical frames and passed the post-warmup allocation guard.
Instructions fell 11.8% (15.341B to 13.529B per 100 frames) and branches 14.7%
(2.328B to 1.985B), with branch misses essentially unchanged. GCC/MSVC all-level
acceptance and unit tests pass. The earlier parity-valid run measured 30.79 versus
32.69 MiB/s; only the saved-binary comparison establishes this speedup.

The same optimized builds and CPU affinity, with five alternating **50-frame**
pairs per level, give the following compression medians. Every pair matches
reference bytes and passes the allocation guard; these results are specific to
the mixed corpus and are not a controlled pre-change comparison at these levels.

| Level | zstd++ MiB/s | Reference MiB/s | zstd++ lead |
| --- | ---: | ---: | ---: |
| 19 | 49.66 | 38.62 | 28.6% |
| 20 | 50.27 | 37.39 | 34.4% |
| 21 | 47.61 | 37.24 | 27.9% |
| 22 | 48.52 | 37.33 | 30.0% |

Timing ranges remain variable: candidate ranges for levels 19–22 were
44.91–52.80, 45.19–50.92, 43.80–48.94, and 39.99–50.91 MiB/s, respectively;
reference ranges were 35.98–39.99, 31.46–40.63, 30.96–37.41, and 34.37–38.56.

Initializing only the reached optimal-parser frontier instead of clearing all
4,099 path entries per match series improves level 16 without changing its output.
Five alternating 100-frame baseline/candidate/reference trials (generic GCC/G++13
`-O3 -DNDEBUG`, WSL CPU 2) measured medians of **54.96 / 62.44 / 58.20 MiB/s**:
**13.6% faster than the saved C++ baseline** and **7.3% faster than reference**
on this corpus. Every trial produced identical frames and passed the worker's
post-warmup allocation check. Timing was noisy (candidate 45.69–66.72 MiB/s),
so this is a scoped result, not an all-level performance claim. Hardware counters
support the reduction: 16.2% fewer instructions and 9.2% fewer branches over
100 frames. GCC and MSVC unit/acceptance suites pass, including a regression
that poisons reused path storage before parsing.

The same five-trial comparison at level 17 measured **54.04 / 57.46 / 59.58
MiB/s** for baseline/candidate/reference: **6.3% faster than the saved C++
baseline**, but still **3.6% behind reference**. Candidate throughput ranged
from 55.22 to 77.03 MiB/s. All five trials retained exact reference frames and
passed the post-warmup allocation guard.

The earlier integrated-splitter baseline measured 46.50 MiB/s against reference's
55.55 MiB/s. Host conditions varied; the controlled saved-binary comparison above,
not that older timing, establishes the new speedup. Earlier measurements below
describe different development states and host conditions.

Earlier level-16 optimal-parser development snapshot (GCC/G++ 13, generic `-O3 -DNDEBUG`,
1 MiB mixed corpus): the first valid repeat-aware implementation measured only
20.71 MiB/s. Linux `perf` then exposed two architectural differences. The template
used the lazy parser's unsorted binary-tree updater, while reference btopt keeps
skipped positions sorted and jumps across long repetitive ranges; it also reset and
walked the entire 4 KiB path table before proving that a match existed. Giving the
optimal parser its own sorted updater and delaying path-table initialization until
the first match reduced a 20-frame run from about 965 ms to about 206 ms.

In seven alternating 50-frame trials, the resulting candidate median was
98.87 MiB/s versus 123.65 MiB/s for reference. A `perf stat` run scaled to 20
frames used about 2.00 billion instructions and 342 million branches, down from
10.52 billion and 2.12 billion before these changes. Levels 16 through 22 all
complete without post-warmup allocation and their frames decode with reference
zstd. The level-16 frame is 263,989 bytes versus 263,655 bytes for reference, so
these remain development measurements rather than part of the byte-identical
acceptance table below.

Subsequent output-preserving collector specialization reduced a controlled
50-frame run from 5.152 to 4.996 billion instructions (-3.0%) and from 865.2 to
854.6 million branches (-1.2%). This combines progressive-candidate filtering,
distance-relative match pointers, and compiling the lazy parser's unused
single-best-match scoring out of the optimal specialization. The frame remains
263,989 bytes and reference-decodable. Recent wall-clock trials were heavily
scheduler-dependent, so this update reports stable hardware work counts rather
than replacing the earlier paired throughput snapshot with a noisy number.
The matching reference worker used 4.526 billion instructions and 511.5 million
branches in the same 50-frame counter run, leaving about a 10.4% instruction gap
and a much larger retired-branch gap in the remaining collector/path lifecycle.

### Single-histogram fast literals below 8 KiB (2026-10-18)

The GCC 13 grouped fast encoder now uses one literal histogram below 8 KiB,
instead of clearing and reducing four histograms above the old 1,500-byte
threshold. Other entropy paths retain their previous histogram policy. A
phase trace on 256-KiB varied text found about 5.4 KiB of literals per frame,
placing the hot literal blocks in the newly single-histogram range. The
expanded unit test checks poisoned-workspace reuse across both sides of the
8-KiB boundary; all ten GCC tests, including all-level byte parity and
reciprocal decoding, pass.

Six balanced 8-GiB screens followed by six reversed-order 16-GiB confirmations
per worker used CPU 2 and optimized GCC 13 binaries. Every candidate and
baseline frame matched reference, and the workers detected no post-warmup
allocations. The table reports the longer confirmation medians, peak RSS, and
complete no-debug-symbol worker sizes.

| Level-1 workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 256 KiB varied text | 3541.83 | 3988.44 | 88.80% | 4.36 | 4.38 | 99.54% | 122.50 | 628.16 | 19.50% |
| 256 KiB scaled mixed | 2392.64 | 2460.45 | 97.24% | 4.46 | 4.48 | 99.55% | 122.50 | 628.16 | 19.50% |

Against the same-source C++ baseline, varied text improved from 3488.02 to
3541.83 MiB/s (+1.54%, five of six paired wins), while scaled mixed changed
from 2389.83 to 2392.64 MiB/s (+0.12%, three of six wins). Varied cycles fell
from 20.915 to 20.613 billion despite 1.11% more instructions. Six balanced
4-GiB trials on both 64-KiB corpora were effectively neutral (-0.33% varied,
-0.12% mixed; three paired wins each, identical instruction counts). This
retained improvement narrows but does not close the varied-text gap. Raw data:
`out/aligned-strategy-control/literal-hist-*`.

### Matched GCC 13 native-CPU control (2026-10-18)

The separate host-specific Release build used `-O3 -march=native -DNDEBUG`
for both C++ and reference C code; it does not replace the generic evaluation.
Six balanced CPU-2 trials per corpus processed 5 GiB per worker. Native and
generic workers emitted identical frames and reported no post-warmup allocations.

| Level-1 native workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied text | 3207.81 | 3599.50 | 89.12% | 5.10 | 5.92 | 86.15% | 134.71 | 639.85 | 21.05% |
| 1 MiB mixed | 2358.60 | 2145.72 | 109.92% | 5.50 | 6.01 | 91.51% | 134.71 | 639.85 | 21.05% |

On varied text, the matched generic medians were 3117.26 / 3246.31 MiB/s
(C++ / reference). Native targeting improved C++ by 2.90% (six paired wins),
but reference by 10.88%, widening the relative gap. On mixed input, C++ rose
from 2199.99 to 2358.60 MiB/s; reference rose from 2068.23 to 2145.72.
Native flags are not a general solution to the text deficit and are not used
for the generic report. Raw data: `out/aligned-strategy-control/native-l1-*`.

### Matched GCC 13 link-time optimization control (2026-10-18)

A separate Release build enabled interprocedural optimization for both the
header-only C++ worker and every reference C object. Six position-balanced,
CPU-2 trials per corpus processed 5 GiB per worker. The LTO and non-LTO
workers emitted identical reference frames and passed their post-warmup
allocation guards. The table reports the LTO binaries; whole-worker code size
and peak RSS use the same definitions as the regular evaluation.

| Level-1 workload with LTO | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied text | 3238.47 | 3693.09 | 87.69% | 5.11 | 5.89 | 86.76% | 118.58 | 541.25 | 21.91% |
| 1 MiB mixed | 2256.25 | 2164.17 | 104.25% | 5.49 | 6.13 | 89.56% | 118.58 | 541.25 | 21.91% |

The matched non-LTO varied-text medians were 3232.98 / 3381.80 MiB/s
(C++ / reference). LTO barely changed C++ (+0.17%, three of six paired
wins) but improved reference by 9.20%, widening the gap. Mixed C++ changed
from 2259.90 to 2256.25 MiB/s. Thus LTO is not an architectural remedy for
this deficit; the existing evaluation configuration is unchanged. Raw data:
`out/aligned-strategy-control/ipo-l1-*`.

### Separated level-1 literal gather (2026-10-18, rejected)

The GCC 13 grouped level-1 encoder was tested with literal copies in a first
pass and LL/OF/ML code generation in a second pass. This is the level-1
counterpart of the earlier rejected level-2 split-loop experiment. GCC unit
and all-level reciprocal acceptance tests passed. Six balanced CPU-2 trials
per corpus processed 8 GiB per worker; all candidate and baseline frames
matched reference and no post-warmup allocations were reported.

| Workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Split gather, 256 KiB varied | 3431.80 | 3963.11 | 86.59% | 4.32 | 4.40 | 98.18% | 106.30 | 628.16 | 16.92% |
| Split gather, 256 KiB scaled mixed | 2374.03 | 2448.32 | 96.97% | 4.49 | 4.50 | 99.78% | 106.30 | 628.16 | 16.92% |

Against the same-source C++ baseline, varied text fell from 3545.40 to
3431.80 MiB/s (-3.20%, zero of six paired wins); scaled mixed fell from
2387.87 to 2374.03 MiB/s (-0.58%, overlapping ranges). Instructions and
cycles rose on both inputs. The smaller candidate executable does not offset
the throughput regression. Production is unchanged. Raw results:
`out/aligned-strategy-control/separate-gather-262144-*`.

### Current level-22 varied-text confirmation (2026-10-18)

The older level-22 varied-text deficit below no longer describes the current
binary. Six alternating-order, CPU-2 pairs of optimized GCC 13 workers each
compressed 20 one-MiB frames. Every C++ frame matched reference byte for byte
and the workers reported no post-warmup allocations. C++ won all six pairs;
the ranges were 2.56–2.73 versus 1.67–1.76 MiB/s. Peak RSS and whole-worker
code size are reported alongside the throughput medians.

| Workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Level 22, 1 MiB varied text | 2.65 | 1.74 | 152.16% | 21.70 | 22.25 | 97.52% | 140.65 | 628.16 | 22.39% |

An instruction-sampling profile attributed about 97% of C++ level-22 work to
the optimal parser and binary-tree match search. The current 256-KiB level-1
varied-text deficit remains a separate target. Raw paired results:
`out/aligned-strategy-control/l22-varied-current-*`.

### Trusted grouped-FSE transitions (2026-10-18, rejected)

The grouped level-1 sequence writer was tested with the per-symbol RLE check
removed after table selection. Its general-purpose encoder retained the check;
an address-sanitized oracle matched 286,000 checked and trusted transitions,
and unit plus all-level reciprocal acceptance tests passed. Generated code
confirmed the checks left the inner sequence loop. Six balanced, CPU-pinned
8 GiB trials per worker and corpus produced byte-identical reference frames
with no post-warmup allocations.

| Workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Trusted FSE, 256 KiB varied | 3585.67 | 4041.57 | 88.72% | 4.35 | 4.37 | 99.54% | 122.34 | 628.16 | 19.48% |
| Trusted FSE, 256 KiB scaled mixed | 2366.62 | 2441.17 | 96.95% | 4.50 | 4.51 | 99.78% | 122.34 | 628.16 | 19.48% |

Against the saved same-source baseline, throughput fell from 3595.71 to
3585.67 MiB/s on varied text and from 2381.75 to 2366.62 MiB/s on scaled
mixed data, with only two of six paired wins on either corpus. Varied-text
instructions fell from 38.760 to 38.444 billion, but cycles rose from 10.229
to 10.282 billion; mixed instructions and cycles both rose. The candidate is
rejected and production code is unchanged. Artifacts:
`out/aligned-strategy-control/trusted-fse-262144-*`.

### Grouped sequence extra-bit pairing (2026-10-18, rejected)

An isolated GCC 13 level-1 candidate combined the literal-length and
match-length extra fields into one bit-writer call, leaving FSE transitions
and offset encoding unchanged. All-level byte parity and reciprocal decoding
passed. Six balanced CPU-2 trials per corpus processed 8 GiB per worker;
candidate, baseline and reference frames matched, with no post-warmup worker
allocations. The current-source baseline binary was saved before the edit;
both C++ workers were stripped of debug symbols for matched timing.

| Workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Extra-bit pair, 256 KiB varied | 3602.26 | 4071.90 | 88.47% | 4.37 | 4.38 | 99.64% | 122.34 | 628.16 | 19.48% |
| Extra-bit pair, 256 KiB scaled mixed | 2367.03 | 2505.79 | 94.46% | 4.49 | 4.47 | 100.39% | 122.34 | 628.16 | 19.48% |

Compared with the same-source C++ baseline, varied text changed from
3594.41 to 3602.26 MiB/s (+0.22%, overlapping ranges), while genuine
scaled-mixed data fell from 2430.38 to 2367.03 MiB/s (-2.61%, one of six
paired wins). Mixed instructions rose from 56.427 to 58.625 billion.
The candidate is rejected and production is unchanged. An earlier easy-to-
compress mixed fixture ran too briefly for reliable throughput inference;
the scaled-mixed result above is the decision control. Artifacts:
`out/aligned-strategy-control/extra-pair-262144-*`.

### Packed level-1 fast-parser entries (2026-10-18)

For GCC 13 x86-64's compile-time minMatch-7 path, the fast parser now stores each
32-bit history index and its byte tag in one cache-aligned 64-bit table entry.
This removes the separate tag-table address stream. On an index-epoch rebase,
only the low 32-bit index is adjusted; the tag is preserved. Other compiler and
minimum-match paths retain their previous table layout.

Six position-balanced 8 GiB screens and six reversed-order 16 GiB confirmation
trials per implementation used optimized GCC 13 workers pinned to CPU 2.
All three encoders produced identical frames in every trial, and the worker's
post-warmup allocation guard passed. The table below gives the longer-run
medians; code size is the complete worker binary.

| Level 1 workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Packed, 1 MiB varied | 3153.30 | 3312.59 | 95.19% | 5.11 | 5.86 | 87.14% | 126.71 | 628.16 | 20.17% |
| Packed, 1 MiB mixed | 2217.39 | 2089.48 | 106.12% | 5.49 | 6.12 | 89.67% | 126.71 | 628.16 | 20.17% |
| Packed, 4 MiB varied | 3325.12 | 3232.76 | 102.86% | 8.24 | 11.87 | 69.44% | 126.71 | 628.16 | 20.17% |
| Packed, 4 MiB mixed | 2095.77 | 2023.42 | 103.58% | 9.23 | 12.12 | 76.16% | 126.71 | 628.16 | 20.17% |

Against the matched pre-change C++ binary, the longer-run median changes are
+1.97%, +1.63%, +2.91% and -2.03%, respectively. The 4 MiB mixed range is
wide: the packed worker wins four of six paired trials despite the negative
median, so that workload is inconclusive. The shorter screens were positive
on all four workloads (+2.96%, +3.96%, +4.31%, +1.17%). This change improves
the confirmed varied-text deficit but does not yet put 1 MiB varied ahead of
reference. The active GCC 13 x86-64 production path was checked against the
measured overlay. ASan/UBSan rollover/parser/streaming
tests, strict units, MSVC's unchanged path, and all 10 optimized GCC CTests
(including reciprocal acceptance at levels 1–22) pass. The broader 22-level
matrix below predates this change and is not a fresh measurement of it.

### Cached fast-parser epoch limit (2026-10-18, rejected)

An isolated GCC 13 x86-64 prototype stores the largest safe block end when a
frame starts or indices are rebased. The common check becomes one comparison;
the cached value occupies existing object padding (104 bytes before and after).
The complete 4 MiB rollover regression passed ASan/UBSan, as did units and
all-level reciprocal acceptance. Six balanced 8 GiB trials per implementation
and workload used optimized binaries and verified exact frames and allocations:

| Workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Cached limit, varied level 1 | 3160.94 | 3385.93 | 93.36% | 5.13 | 5.90 | 87.05% | 126.71 | 628.16 | 20.17% |
| Cached limit, mixed level 1 | 2223.17 | 2086.78 | 106.54% | 5.38 | 6.14 | 87.63% | 126.71 | 628.16 | 20.17% |
| Cached limit, varied level 2 | 3232.20 | 3432.26 | 94.17% | 5.25 | 6.11 | 85.93% | 122.51 | 628.16 | 19.50% |
| Cached limit, mixed level 2 | 2680.15 | 2502.74 | 107.09% | 5.61 | 6.26 | 89.55% | 122.51 | 628.16 | 19.50% |

Compared with the corrected production baseline, median changes were +0.17%,
+0.55%, -0.72% and +0.62%, respectively. All ranges overlap and instruction
counts change by less than 0.01%. The cached check is not retained; it does not
demonstrate a reliable throughput improvement. Artifacts:
`out/aligned-strategy-control/fast-epoch-cache-1048576-*`.

### Cross-strategy index-epoch audit (2026-10-18)

Commit `a2f82a2` extends index-rollover protection to double-fast, hash-chain,
binary-tree and row-table storage. A near-limit audit exposed wrapped indices
in double-fast/hash-chain and an ASan crash in binary-tree lazy parsing, even
though two ordinary double-fast reuse screens had passed. Boundary tests are
therefore checked independently of whether a particular input still compresses
to the same bytes.

Rebasing preserves current-frame entries and discards stale epochs. Chain/tree
ring slots move with their links and byte tags; tree sentinels, repeat offsets,
update cursors and row-local state retain their meaning. Optimal parsing supplies
the complete block extent before preparing the tree. No new processing-time
workspace allocation is introduced.

All ten optimized GCC CTests pass, including reciprocal acceptance at levels
1–22. ASan/UBSan and MSVC pass 768 boundary-block comparisons across aligned
and unaligned origins, tagged/runtime double-fast, lazy/hash-chain, three/four-byte
optimal trees, varied text, random tiles and RLE. Additional checks cover ring
mapping, sentinels, row-origin rebasing, range rejection and aligned-allocation
balance. These are correctness changes, not claimed throughput improvements;
the earlier performance tables predate this commit.

### Fast index-epoch correctness fix (2026-10-18)

Commit `903eeef` fixes a repeated-frame crash when a fast-parser frame exceeds
its window and exhausts the 32-bit index epoch mid-frame. The permanent
regression fails under ASan before the fix and passes afterward: 2,049 complete
4 MiB frames remain byte-identical, alongside 35,200 parser/reset/literal cases.
MSVC passes the same regression; all nine optimized GCC CTests, including
reciprocal acceptance at levels 1–22, pass.

Six balanced 8 GiB trials compare corrected C++ against the pre-fix binary and
reference for 1 MiB inputs. Six alternating 8 GiB trials compare corrected C++
and reference for 4 MiB inputs. Every trial passes exact reference-frame and
post-warmup allocation guards. There is **no valid unfixed 4 MiB baseline** at
this reuse count: that executable crashes. Both implementations use matching
compression parameters, optimized GCC 13 builds and CPU 2.

| Level 1 workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Corrected epoch, 1 MiB varied | 3166.38 | 3382.73 | 93.60% | 5.12 | 5.88 | 87.08% | 126.71 | 628.16 | 20.17% |
| Corrected epoch, 1 MiB mixed | 2158.92 | 2086.38 | 103.48% | 5.35 | 6.15 | 87.04% | 126.71 | 628.16 | 20.17% |
| Corrected epoch, 4 MiB varied | 3175.76 | 3216.05 | 98.75% | 8.11 | 11.87 | 68.27% | 126.71 | 628.16 | 20.17% |
| Corrected epoch, 4 MiB mixed | 1979.57 | 1942.48 | 101.91% | 9.11 | 12.12 | 75.11% | 126.71 | 628.16 | 20.17% |

Pre-fix 1 MiB medians were 3163.69 MiB/s varied and 2220.32 MiB/s mixed:
the correction changes these by **+0.09%** and **-2.77%**, with overlapping
ranges. Instructions rise 0.33% and 0.49%. Retention is required for correctness,
not justified as a speedup; reducing the guard's cost remains an optimization
opportunity. Varied-text throughput still trails reference at both sizes.
Artifacts: `out/aligned-strategy-control/fast-epoch-fixed-{1048576,4194304}-*`.

### Packed fast-parser entries (2026-10-18, experimental)

An isolated GCC 13 minMatch-7 prototype combines the full 32-bit history index
and byte tag into one 64-bit table entry. It preserves the tag filter, insertion
order and index-rollover semantics, replacing separate index/tag address streams
and stores. The default table grows from 64+16 KiB to 128 KiB; this is a locality
tradeoff, not an allocation reduction or an assumed cache benefit.

Strict units, all-level reciprocal acceptance tests, and 49,875 parser-oracle
blocks under ASan/UBSan passed, including collisions, resets, index rollover and
custom skip distance. Assembly confirms combined 64-bit loads/stores. Six
balanced production/candidate/reference trials processed 8 GiB per measurement:

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Packed fast entries, varied text | 3227.18 | 3443.67 | 93.71% | 5.12 | 5.89 | 87.03% | 126.71 | 628.16 | 20.17% |
| Packed fast entries, mixed data | 2293.89 | 2137.42 | 107.32% | 5.48 | 6.10 | 89.85% | 126.71 | 628.16 | 20.17% |

Production medians were 3208.93 and 2230.61 MiB/s: **+0.57%** and **+2.84%**.
Ranges overlap for both workloads: varied 3077.07–3235.54 versus
3185.16–3254.36; mixed 2166.90–2283.73 versus 2281.04–2347.82.
Instructions changed +0.97% and -0.20%; cycles fell 0.41% and 3.15%.
All exact-frame and allocation guards passed. This is not integrated; longer
same-binary reversed-order confirmation and broader gates are required.
Artifacts: `out/aligned-strategy-control/fast-packed-entry-1048576-*`.

Longer confirmation used the exact same binaries, reversed trial order, and
six 16 GiB measurements per implementation/workload:

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Packed entries confirmation, varied text | 3209.37 | 3410.53 | 94.10% | 5.09 | 5.89 | 86.43% | 126.71 | 628.16 | 20.17% |
| Packed entries confirmation, mixed data | 2282.03 | 2154.19 | 105.93% | 5.50 | 6.13 | 89.74% | 126.71 | 628.16 | 20.17% |

Production medians were 3171.70 and 2223.69 MiB/s: **+1.19%** and **+2.62%**,
with 5/6 paired wins for each workload and overlapping ranges. All frames and
allocation guards passed. The expanded 79,800-case collected-literal oracle,
270 streaming/reuse cases and full unit suite passed ASan/UBSan; the disabled
MSVC path passed the expanded oracle too.

The subsequent 4 MiB screen exposed a pre-existing fast-parser index-epoch
overflow in **both** production and the candidate after repeated frame resets.
Those aborted trials are invalid performance measurements. A complete-frame
regression reproduced the crash under ASan. The packing candidate remains
unintegrated and must be rechecked against the corrected baseline; the numbers
above predate that correctness fix. The fix rebases current-frame indices before
overflow and discards stale epochs without new allocation. It is not itself a
claimed speedup. Other strategies' epoch handling remains a follow-up audit.
Artifacts: `out/aligned-strategy-control/fast-packed-entry-confirm-1048576-*`.

### Parser-time sequence symbols (2026-10-18, experimental)

The fast minMatch-7 parser already has each sequence's logical lengths and
offset available while appending it. This isolated candidate generates LL/OF/ML
symbols there, reusing the three existing code arrays and avoiding the later
compact-record reconstruction/symbol-generation pass. Cache bindings are checked
once per block; ordinary callers and mismatched storage retain the original pass.
No extra code-array allocation is introduced. Production is not yet changed.

Strict unit/all-level acceptance tests passed. A dedicated ASan/UBSan oracle
passed 204 parser/entropy comparisons across long lengths, collisions, multiple
blocks, resets, moves, poisoned code storage, and mismatched-binding fallback.
Six balanced-order GCC 13 Release trials per corpus, CPU 2, level 1, 1 MiB inputs,
processed 8 GiB each; all frame and allocation guards passed.

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Parser-time symbols, varied text | 3088.26 | 3351.92 | 92.13% | 5.12 | 5.89 | 86.99% | 123.46 | 628.16 | 19.65% |
| Parser-time symbols, mixed data | 2379.79 | 2046.96 | 116.26% | 5.37 | 6.14 | 87.46% | 123.46 | 628.16 | 19.65% |

Production medians were 3128.10 and 2133.06 MiB/s. The candidate changed
throughput by **-1.27%** on varied text and **+11.57%** on mixed data, with
overlapping ranges in both cases. Instructions fell 0.18% and 9.12% respectively.
The mixed-data gain is promising; longer confirmation of both the gain and the
possible text tradeoff is required before retention. Artifacts:
`out/aligned-strategy-control/parser-symbols-1048576-*`.

Longer confirmation reused the exact binaries, doubled work to 16 GiB per
measurement, and reversed the six trial orders:

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Parser-time symbols, varied confirmation | 3065.75 | 3325.85 | 92.18% | 5.11 | 5.87 | 87.06% | 123.46 | 628.16 | 19.65% |
| Parser-time symbols, mixed confirmation | 2518.45 | 2138.11 | 117.79% | 5.35 | 6.15 | 87.10% | 123.46 | 628.16 | 19.65% |

Production medians were 3146.99 and 2257.46 MiB/s. The mixed gain reproduced
at **+11.56%**, with disjoint ranges: 2310.65–2557.18 versus 2011.95–2285.83
MiB/s. Instructions fell 9.12%. Varied text measured **-2.58%**, with overlapping
ranges and only a 0.18% instruction reduction. The prototype remains experimental
while its duplicate per-sequence bookkeeping is investigated; the confirmed
mixed gain alone does not establish an improvement on the remaining text gap.
Artifacts: `out/aligned-strategy-control/parser-symbols-confirm-1048576-*`.

A revision derives the sequence literal total once from the existing gather
cursor at block exit (including the RLE shortcut), and avoids repeated validity
flag writes where the static window proves the offset code fits. Runtime and
large-window parsers retain the check. Unit/all-level acceptance and 408 expanded
ASan/UBSan cases passed, including runtime and maximum-window configurations.
Eight position-balanced four-way trials processed 4 GiB each:

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block-exit bookkeeping, varied text | 3035.13 | 3316.43 | 91.52% | 5.12 | 5.86 | 87.47% | 123.46 | 628.16 | 19.65% |
| Block-exit bookkeeping, mixed data | 2445.85 | 2077.23 | 117.75% | 5.38 | 6.12 | 87.87% | 123.46 | 628.16 | 19.65% |

Production medians were 3043.76 and 2153.79 MiB/s: **-0.28%** on varied text
(overlapping ranges) and **+13.56%** on mixed data (disjoint ranges). Instructions
fell 0.75% and 9.15%. The preceding prototype measured 3025.14 and 2453.60 MiB/s
in the same trials. These results support retaining the main fusion benefit,
but do not yet establish the absence of a text tradeoff. Production remains unchanged. Artifacts:
`out/aligned-strategy-control/parser-symbols-exit-1048576-*`.

Six reversed-order 16 GiB confirmations measured:

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block-exit bookkeeping, varied confirmation | 3073.18 | 3256.32 | 94.38% | 5.12 | 5.91 | 86.68% | 123.46 | 628.16 | 19.65% |
| Block-exit bookkeeping, mixed confirmation | 2378.23 | 2012.65 | 118.16% | 5.35 | 6.11 | 87.62% | 123.46 | 628.16 | 19.65% |

Production medians were 3126.56 and 2126.02 MiB/s: **-1.71%** on varied text
and **+11.86%** on mixed data, with overlapping ranges. Instruction changes
remained -0.75% and -9.15%. The unconditional revision remains experimental
because the text tradeoff persists. A separate prototype uses preceding-block
sequence density as a hint for choosing fusion; it changes where symbol codes
are computed, not match decisions or compressed bytes. Artifacts:
`out/aligned-strategy-control/parser-symbols-exit-confirm-1048576-*`.

The first density-hint prototype enables fusion when the preceding block has
at least one sequence per 32 input bytes (with a 32-sequence minimum). Inspected
varied-text blocks contained 879–881 sequences; the dense mixed blocks contained
16,128–16,384. Unit/all-level acceptance and 408 sanitizer transition cases
passed, including clearing cache validity when returning to the ordinary pass.
Eight position-balanced four-way trials processed 4 GiB each:

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| First density hint, varied text | 3002.61 | 3388.69 | 88.61% | 5.06 | 5.85 | 86.49% | 123.68 | 628.16 | 19.69% |
| First density hint, mixed data | 2027.73 | 1965.67 | 103.16% | 5.31 | 6.13 | 86.64% | 123.68 | 628.16 | 19.69% |

Production medians were 3148.09 and 2087.35 MiB/s: **-4.62%** and **-2.86%**,
with overlapping ranges. Instructions rose 3.70% on varied text and fell 4.13%
on mixed data. This implementation is not retained. Generated calls show that
even its unfused path still uses coded-block parser/encoder types; preserving
the original compact-block types is a separate follow-up control, not an
established explanation for the slowdown. All measured frames and allocation
guards passed. Artifacts:
`out/aligned-strategy-control/parser-symbols-density-1048576-*`.

The follow-up restored the original compact parser and encoder types on the
unfused path. Strict unit/all-level acceptance tests and 408 ASan/UBSan cases
passed. Eight position-balanced four-way trials processed 4 GiB each:

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Original-type density fallback, varied text | 2941.99 | 3399.15 | 86.55% | 5.06 | 5.89 | 86.00% | 124.12 | 628.16 | 19.76% |
| Original-type density fallback, mixed data | 2144.85 | 2133.80 | 100.52% | 5.31 | 6.11 | 86.98% | 124.12 | 628.16 | 19.76% |

Production medians were 3177.78 and 2227.12 MiB/s: **-7.42%** and **-3.69%**.
Varied-text ranges were disjoint (3039.02–3219.05 versus 2800.55–2983.77);
mixed ranges overlapped. Instructions increased 8.09% and 4.30%, respectively.
Restoring types did not restore the original code generation or performance.
This revision is rejected, and production remains unchanged. All measured
frames and allocation guards passed. Artifacts:
`out/aligned-strategy-control/parser-symbols-legacy-1048576-*`.

### Bounded 64-bit sequence writer (2026-10-18, initial version rejected)

An isolated candidate uses live pre-sized output storage, overlapping 64-bit
stores, and a conservative constant sequence-bit bound. It removes resizing
from the emission loop and retains the original writer when existing capacity
is insufficient. This differs from the earlier direct-writer attempt by avoiding
an extra sizing pass and a shared emission lambda.

Strict unit/all-level acceptance tests passed. ASan/UBSan checks covered
20,560 scalar packing/prefix/pointer-stability cases and 1,056 integrated
sequence cases, whose complete output matched production. Six balanced trials
per corpus processed 8 GiB each, GCC 13 Release, CPU 2, level 1, 1 MiB inputs;
all measured frames and allocation guards passed.

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Bounded word writer, varied text | 2719.63 | 3220.07 | 84.46% | 5.12 | 5.89 | 87.03% | 131.12 | 628.16 | 20.87% |
| Bounded word writer, mixed data | 1651.24 | 1974.57 | 83.63% | 5.63 | 6.12 | 92.02% | 131.12 | 628.16 | 20.87% |

Contemporaneous production medians were 2850.38 and 1995.96 MiB/s: candidate
changes were **-4.59%** (overlapping ranges) and **-17.27%** (disjoint ranges).
Instructions rose 4.83% and 14.60%. This initial version is rejected and production
code is unchanged. Generated code calls the new append routine out of line at
each of three extra fields; this is a concrete follow-up diagnostic, not proof
that calls alone caused the regression. Artifacts:
`out/aligned-strategy-control/sequence-word-1048576-*`.

An append-inlining follow-up passed the same strict unit/all-level acceptance
gates, 20,560 packing checks, and 1,056 integrated sequence comparisons under
ASan/UBSan. Disassembly confirmed append calls disappeared, although some
flush paths remained out of line. Eight position-balanced four-way trials
(4 GiB each) compared production, the initial candidate, the revision, and
reference:

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Inline word writer, varied text | 2963.64 | 3378.89 | 87.71% | 5.13 | 5.88 | 87.27% | 130.84 | 628.16 | 20.83% |
| Inline word writer, mixed data | 1864.41 | 2095.64 | 88.97% | 5.59 | 6.10 | 91.68% | 130.84 | 628.16 | 20.83% |

Production medians were 3118.44 and 2162.25 MiB/s: **-4.96%** and **-13.77%**,
with overlapping varied-text ranges and disjoint mixed-data ranges. Initial
candidate medians in the same trials were 2973.34 and 1807.72 MiB/s.
Instructions versus production rose 5.27% and 14.60%. The revision is rejected;
removing append calls did not establish a benefit from this design. Artifacts:
`out/aligned-strategy-control/sequence-word-inline-1048576-*`.

### Post-packed all-level evaluation (2026-10-18)

The current generated tables below were rebuilt after the packed fast-parser
change and the index-epoch correctness fixes. They use six alternating trials
of 2,000 frames for each implementation, operation and level on the 1 MiB mixed
corpus, optimized GCC 13 Release, tracing off and WSL CPU 2. All 528 raw
measurements, 88 medians, binary sizes, worker CSVs, execution order, 22
identical compressed-frame pairs, nine displayed metrics per row and README
table cells passed an independent audit. Raw data and the standalone report
are in `out/full-metrics-20261018/post-packed`.

C++ exceeds reference throughput at **21 of 22 compression levels** and
**all 22 decompression levels** in this run. Level 17 compression is the
exception: 68.79 versus 78.72 MiB/s (87.38% of reference), with two of six
paired wins and wide overlapping trial ranges. The previous checkpoint had
level 17 ahead, but it predates the index-epoch fixes and used different
worker binaries; these reports alone do not identify the cause. Compression
level 17 received the controlled confirmations below before attributing the
change to code rather than run variability. The table covers only the mixed corpus:
the known small-input and varied-text gaps remain optimization targets.

Two six-pair level-17 confirmations used the current optimized workers,
4,000 frames per measurement, alternating CPU-2 order (reversed for the second
series), exact frame comparisons, allocation guards and hardware counters.
The standalone matrix row remains unchanged; these are separate measurements.

| Level 17 compression | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| First confirmation | 92.84 | 92.93 | 99.90% | 21.83 | 21.87 | 99.85% | 144.44 | 628.16 | 22.99% |
| Reversed-order repeat | 95.25 | 90.70 | 105.03% | 21.83 | 21.87 | 99.85% | 144.44 | 628.16 | 22.99% |

C++ won five of six paired trials in each series. Median paired ratios were
105.17% and 105.22%. Retired instructions were stable at 310.81 billion for
C++ and 365.10 billion for reference in every 4,000-frame run, while early
cycle totals and throughput varied widely. The matrix's level-17 deficit did
not repeat consistently, but these noisy results do not prove an across-run
speed advantage. Artifacts: `out/aligned-strategy-control/post-packed-l17-{confirm,repeat}-*`.

### Repeated all-level evaluation (2026-10-18, historical checkpoint)

The all-level checkpoint below uses six alternating trials of 2,000 frames per
implementation, operation, and level: **528 measurements**, optimized GCC 13
Release builds, tracing off, WSL CPU 2, and the deterministic 1 MiB mixed corpus.
C++ median throughput exceeds reference at **all 22 compression levels and all
22 decompression levels**: 104.67–140.12% of reference for compression and
108.55–165.65% for decompression. Compression won at least five of six paired
trials at every level; decompression also won at least five of six.

All raw worker fields, aggregate medians, executable sizes, 22 compressed-frame
pairs, all nine displayed metrics, variability rows, execution order, and the
complete generated README section passed an independent audit. Full raw data
and the standalone report are in `out/full-metrics-20261018/repeated-current`.
Earlier single-run snapshots remain historical evidence; the new medians
measure the same compression implementation with a more reliable protocol.
They are not an additional algorithmic speedup.

This establishes an all-level result for the mixed corpus, not universal
superiority. Timing ranges overlap in several cases, and known varied-text and
small-input compression gaps remain optimization targets. All nine metrics and
the per-level ranges and paired win counts are in the evaluation tables.
This checkpoint predates the later fast-parser index-epoch correctness fix
(`903eeef`); its raw results are preserved rather than attributed to a rebuilt
binary. The corrected parser requires fresh performance controls and a later
full-matrix refresh before those numbers describe current source exactly.

### Level 14/15 confirmation (2026-10-18)

The latest single-run full matrix reported compression deficits at levels 14
and 15. Six alternating C++/reference pairs using the same optimized workers
and 1 MiB mixed corpus, CPU 2, and 4,000 frames per measurement did not
reproduce those deficits consistently. Every output comparison and worker
allocation guard passed; the build was current, Release GCC 13, with tracing off.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Compression 14, confirmation | 450.55 | 399.91 | 112.66% | 21.19 | 21.98 | 96.38% | 122.65 | 628.16 | 19.53% |
| Compression 15, confirmation | 439.96 | 375.08 | 117.30% | 21.19 | 21.99 | 96.37% | 122.65 | 628.16 | 19.53% |

Level 14 won all six pairs; level 15 won five. Median paired throughput ratios
were 111.54% and 115.57%. Median instruction counts were 64.56/73.20 billion
(C++/reference) at level 14 and 64.52/73.20 billion at level 15. Timing ranges
still overlapped, so these results do not imply every individual run wins.
The original full-matrix rows remain unchanged; these confirmations measure
the same implementation and are not an algorithmic speedup. The fixed-order,
single-measurement evaluator needs repeated, alternating measurements before
small remaining differences can reliably guide optimization.
Artifacts: `out/aligned-strategy-control/cmov-matrix-l{14,15}-confirm-*`.

### Scalar FSE emission recheck (2026-09-27, not retained)

This diagnostic disabled only grouped FSE emission, using the existing scalar
inline emitter while preserving literal sampling, canonical Huffman selection,
and all parser policies. Unit and all-level acceptance tests passed.
Six balanced-order GCC 13 Release trials on CPU 2 processed 8 GiB each at
level 1 with 256 KiB inputs; all measured frame/allocation guards passed.

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Scalar FSE emission, varied text | 3511.19 | 4010.92 | 87.54% | 4.34 | 4.38 | 99.20% | 126.65 | 628.16 | 20.16% |
| Scalar FSE emission, mixed data | 2361.24 | 2477.32 | 95.31% | 4.48 | 4.49 | 99.87% | 126.65 | 628.16 | 20.16% |

Grouped production medians were 3567.97 and 2412.41 MiB/s. Scalar emission
changed throughput by **-1.59%** and **-2.12%**, with overlapping ranges;
instructions increased 0.48% and 0.80%. There is no evidence here to replace
the existing grouped policy. Production code is unchanged.
Artifacts: `out/aligned-strategy-control/fse-scalar-ablation-262144-*`.

### Batched FSE state transitions (2026-09-27, rejected)

An isolated fast-strategy candidate advanced each of the three FSE state
streams separately in batches of at most 32 sequences, then emitted the saved
bits in the original order. It used 768 bytes of stack scratch and no additional
heap buffer. Unit/all-level acceptance tests passed, as did 8,208 scalar-output
comparisons under ASan/UBSan covering batch boundaries and workspace reuse.

GCC 13 Release, CPU 2, level 1, 256 KiB inputs; six balanced-order trials
processing 8 GiB each. All measured frames and allocation guards passed.

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Batched FSE, varied text | 3342.30 | 4027.48 | 82.99% | 4.35 | 4.37 | 99.55% | 122.50 | 628.16 | 19.50% |
| Batched FSE, mixed data | 2205.19 | 2506.57 | 87.98% | 4.50 | 4.48 | 100.35% | 122.50 | 628.16 | 19.50% |

Baseline medians were 3536.64 and 2442.41 MiB/s. Throughput fell **5.50%**
and **9.71%**, with disjoint ranges on mixed data; instructions rose only
0.13% and 1.29%. The candidate is rejected and production code is unchanged.
Generated code retains three separate dependent transition loops. Losing
overlap between the independent state streams is a possible explanation for
the cycle cost, not a demonstrated exclusive cause.
Artifacts: `out/aligned-strategy-control/fse-batch-262144-*`.

An interleaved follow-up advanced the three independent states together in
one transition loop, still separating bit emission. It passed the same tests
and sanitizer oracle. Generated code kept the ordinary transition-path states
in registers, but that alone did not beat the production loop.

Eight position-balanced four-way trials (4 GiB each) compared production,
the preceding separate-stream batch, the interleaved batch, and reference:

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Interleaved FSE batch, varied text | 3510.93 | 4008.78 | 87.58% | 4.35 | 4.36 | 99.78% | 122.66 | 628.16 | 19.53% |
| Interleaved FSE batch, mixed data | 2334.36 | 2497.03 | 93.49% | 4.47 | 4.48 | 99.78% | 122.66 | 628.16 | 19.53% |

Production medians were 3523.58 and 2422.06 MiB/s: **-0.36%** and **-3.62%**,
with overlapping ranges. The separate-stream batch measured 3332.57 and
2178.29 MiB/s in these same trials. Interleaving recovers much of that loss,
but neither batch design is retained. Instruction counts versus production
rose 0.34% and 1.75%. Artifacts:
`out/aligned-strategy-control/fse-batch-interleaved-262144-*`.

### Packed sequence-symbol scratch (2026-09-27, rejected)

This isolated experiment gathered LL/OF/ML symbols into one 32-bit word for
the reverse emission loop, retaining the original byte arrays for table
selection. Generated code confirmed one packed-symbol load, but FSE state
spills remained. The extra scratch vector and gathering store increased work.
Production code is unchanged.

GCC 13 Release, CPU 2, level 1, 256 KiB inputs; six balanced-order trials
processing 8 GiB each. Unit/all-level acceptance tests and every measured
frame/allocation guard passed.

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Packed sequence symbols, varied text | 3552.90 | 4005.64 | 88.70% | 4.50 | 4.38 | 102.63% | 122.33 | 628.16 | 19.47% |
| Packed sequence symbols, mixed data | 2322.75 | 2479.66 | 93.67% | 4.73 | 4.48 | 105.45% | 122.33 | 628.16 | 19.47% |

Current-baseline medians were 3571.39 and 2437.12 MiB/s. Candidate changes
were **-0.52%** (overlapping ranges) and **-4.69%** (disjoint ranges);
instructions rose 1.78% and 3.03%. The candidate is rejected.
Artifacts: `out/aligned-strategy-control/sequence-symbol-pack-262144-*`.

### Packed Huffman code representation (2026-09-27, not retained)

An isolated candidate stores the Huffman value in the high bits of a 64-bit
word and its length in the low byte, following the reference writer's
representation. It converts each selected table once, reuses it across streams,
and preserves per-used-symbol validation. Production code is unchanged.

GCC 13 Release (`-O3 -DNDEBUG`), CPU 2, six balanced-order trials per corpus,
8 GiB processed per trial, level 1, 256 KiB inputs. All candidate, current C++,
and reference frames matched exactly; unit and all-level acceptance tests
passed. The independent packing oracle passed 12,432 cases under ASan/UBSan,
including output reuse, plus invalid-code checks.

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Packed Huffman, varied text | 3585.32 | 3991.15 | 89.83% | 4.37 | 4.37 | 100.09% | 122.49 | 628.16 | 19.50% |
| Packed Huffman, mixed data | 2405.48 | 2486.37 | 96.75% | 4.49 | 4.49 | 100.04% | 122.49 | 628.16 | 19.50% |

Current C++ baseline medians were 3567.54 and 2436.08 MiB/s respectively:
the candidate changed throughput by **+0.50%** on varied text and **-1.26%**
on mixed data, with overlapping ranges in both cases. Instruction counts
changed by -0.77% and +0.23%. This does not establish a reliable improvement,
so the candidate is not retained. The table-conversion overhead is included;
these are end-to-end worker measurements, not standalone writer timings.
Artifacts: `out/aligned-strategy-control/huffman-packed-262144-*`.

A follow-up constructed packed entries directly during canonical assignment,
removing the separate conversion pass and duplicate temporary tables. It also
passed the unit/all-level acceptance suites and the 12,432-case sanitizer
oracle. The same six-trial protocol produced:

| Candidate / workload | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Direct packed Huffman, varied text | 3561.37 | 4040.52 | 88.14% | 4.37 | 4.37 | 99.96% | 122.52 | 628.16 | 19.50% |
| Direct packed Huffman, mixed data | 2376.79 | 2463.38 | 96.48% | 4.49 | 4.51 | 99.44% | 122.52 | 628.16 | 19.50% |

Contemporaneous baseline medians were 3559.56 and 2406.83 MiB/s: **+0.05%**
and **-1.25%**, again with overlapping ranges. Varied-text instructions fell
1.08%; mixed-data instructions were essentially unchanged. This variant is
also not retained: removing the conversion pass did not establish a throughput
gain. Artifacts: `out/aligned-strategy-control/huffman-direct-packed-262144-*`.

### Binary-tree catch-up look-ahead (2026-09-27, rejected)

An isolated GCC13 candidate precomputes eight future hash slots in a stack ring
and prefetches them during long binary-tree catch-up runs. Actual hash-table
loads/stores retain their original order, including collisions; short runs and
other compilers retain the existing path. No heap storage is added.

Six balanced 2,000-frame mixed-input trials at level 14 measured +2.80% versus
production's 417.71 MiB/s, but ranges overlap widely (production 289.44–432.92;
candidate 313.03–454.88). Instructions increased 15.28% (32.290B to 37.225B),
while median cycles decreased from 21.578B to 20.961B. This is a tentative
latency-hiding result, **not a retained optimization**.

| Diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Binary-tree look-ahead, mixed compression level 14 | 429.43 | 387.43 | 110.84% | 21.19 | 22.00 | 96.31% | 122.65 | 628.16 | 19.53% |

The candidate passed 2,016 ASan/UBSan collision-heavy block/reset comparisons,
strict GCC unit and all-level acceptance tests, plus every timed exact-frame
and allocation check. Longer confirmation and varied-input controls are pending.
Raw artifacts: `out/aligned-strategy-control/bt-lookahead-*`.

The longer reversed-order confirmation did **not** reproduce the gain.
Six 6,000-frame trials measured -2.25% versus production's 368.43 MiB/s;
ranges overlapped broadly (production 300.75–444.65; candidate 305.75–434.61).
Instructions still increased 15.28% (96.835B to 111.634B), and median cycles
were essentially unchanged (73.231B to 73.394B). This version is not supported
for retention; the already-running varied-text and level-15 controls remain
pending. Production is unchanged.

| Diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Binary-tree look-ahead confirmation, mixed level 14 | 360.14 | 386.00 | 93.30% | 21.20 | 22.01 | 96.33% | 122.65 | 628.16 | 19.53% |

The remaining controls reject this design. Level-14 varied text regressed
12.75% versus production's 103.23 MiB/s, with disjoint ranges
(100.35–106.39 versus 84.29–92.93). Level-15 varied text regressed 6.81%
versus 60.71 MiB/s, also disjoint (60.43–61.11 versus 56.25–57.26).
Level-15 mixed input showed +2.47% versus 467.01 MiB/s, but with overlapping
ranges (431.67–558.54 versus 453.95–496.74). Instructions increased 5.43%,
2.30%, and 15.23%, respectively. Every frame/allocation check passed.
The text regressions and failed mixed confirmation outweigh the isolated noisy
gains. The candidate is not integrated; production remains unchanged.

| Diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Binary-tree look-ahead, varied text level 14 | 90.06 | 86.25 | 104.43% | 20.94 | 21.85 | 95.83% | 122.65 | 628.16 | 19.53% |
| Binary-tree look-ahead, mixed level 15 | 478.55 | 414.85 | 115.36% | 21.15 | 21.94 | 96.40% | 122.65 | 628.16 | 19.53% |
| Binary-tree look-ahead, varied text level 15 | 56.58 | 51.50 | 109.87% | 20.93 | 21.84 | 95.84% | 122.65 | 628.16 | 19.53% |

### Fast minMatch-6 candidate selection (2026-09-27, retained for GCC 13/x86-64)

The exact measured candidate is now integrated for GCC13/x86-64 fast minMatch-6
only. Other compilers and strategies retain the previous validity check. All
seven integrated GCC tests pass, including all-level bidirectional acceptance
and the permanent 39,900-block parser oracle. Full-unit and custom-frame
ASan/UBSan checks passed; the disabled MSVC path passed the oracle under strict
warnings. Retention is based on repeated text gains, not a universal speedup:
the 256-KiB mixed confirmation was 0.76% slower with overlapping timing ranges,
while small-input controls were neutral. The historical 2026-09-27 single-run
matrix includes this integration. All 88 measurements (2,000 frames each), all nine metrics in
44 README rows, raw CSV fields, executable sizes, corpus size, and all 22
compressed-frame pairs passed independent verification. Artifacts are under
`out/full-metrics-20260927/after-fast-candidate-selection`.
On that historical 1-MiB mixed run, compression led at 20 of 22 levels; levels 14 and 15
measured 85.46% and 95.93% of reference. Decompression leads at all 22 levels
(108.11–163.60%). The changed fast path is inactive at levels 14 and 15, whose
earlier confirmations varied widely; this screen does not establish a causal
regression. These deficits and repeatable varied-text/small-input gaps mean the
overall goal remained unmet. The current repeated matrix above supersedes this
screen for mixed-corpus throughput; varied-text and small-input gaps remain.
The controlled results below measure the change.
Historical experiment notes follow.

An isolated GCC13/x86-64 experiment selects a safe integer candidate offset with
a conditional move before reading match bytes. It preserves the validity test
and never forms a pointer from an invalid offset. Only fast minMatch-6 changes;
production is unchanged. Units and 270 sanitizer-backed boundary/reuse frames pass.
Six balanced 8-GiB trials per corpus show a tradeoff: varied text improved 4.33%
(3414.14 to 3562.10 MiB/s), with branch misses falling from 29.287M to 5.838M,
but instructions increasing 2.31%. Baseline/candidate ranges overlap slightly
(3379.04–3446.37 / 3443.97–3576.39). Mixed input slowed 2.05% (2445.64 to
2395.56), with overlapping ranges 2420.30–2490.99 / 2363.97–2441.32 and no
branch-miss reduction (5.010M to 5.127M). Longer confirmation is required;
this is not retained as a workload-wide improvement.

| Conditional-move experiment | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Fast candidate selection, 256-KiB varied text, level 1 | 3562.10 | 4067.14 | 87.58% | 4.37 | 4.39 | 99.60% | 122.50 | 628.16 | 19.50% |
| Fast candidate selection, 256-KiB mixed, level 1 | 2395.56 | 2499.88 | 95.83% | 4.50 | 4.53 | 99.27% | 122.50 | 628.16 | 19.50% |

Longer confirmation used the same binaries, reversed order and 16 GiB per trial.
Varied text improved 2.02% (3400.76 to 3469.44 MiB/s), with overlapping ranges
3340.31–3437.44 / 3388.06–3566.83. Branch misses fell from 58.521M to 11.793M;
instructions rose 2.31%. Mixed input was 0.76% slower (2432.99 to 2414.48),
with broad overlapping ranges 2197.75–2498.66 / 2354.71–2488.78; instructions
rose 0.87%, and branch misses remained similar (10.116M / 10.192M). All frame
and allocation checks passed. The tradeoff needs smaller-input and level-2
controls before any retention decision; production remains unchanged.

| Conditional-move confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Fast candidate selection, 256-KiB varied text, level 1 | 3469.44 | 4001.34 | 86.71% | 4.35 | 4.37 | 99.64% | 122.50 | 628.16 | 19.50% |
| Fast candidate selection, 256-KiB mixed, level 1 | 2414.48 | 2458.69 | 98.20% | 4.50 | 4.52 | 99.52% | 122.50 | 628.16 | 19.50% |

Broader controls (six balanced 8-GiB trials each) were neutral at 64 KiB:
mixed −0.21% (1765.17 to 1761.54 MiB/s), varied +0.05% (3519.71 to 3521.40).
At 1 MiB, level 2 improved 3.16% on mixed input (2626.23 to 2709.23) and
1.60% on varied text (3274.33 to 3326.70). Every before/after timing range
overlaps; the level-2 gains need longer confirmation. Instructions increased
2.54%/2.67% on the two small cases and 0.39%/2.28% on the level-2 cases.
All exact-frame and allocation guards passed. Production remains unchanged.

| Conditional-move broader control | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Fast candidate selection, 64-KiB mixed, level 1 | 1761.54 | 1898.77 | 92.77% | 4.12 | 3.99 | 103.18% | 122.50 | 628.16 | 19.50% |
| Fast candidate selection, 64-KiB varied text, level 1 | 3521.40 | 3528.52 | 99.80% | 4.12 | 4.02 | 102.63% | 122.50 | 628.16 | 19.50% |
| Fast candidate selection, 1-MiB mixed, level 2 | 2709.23 | 2559.71 | 105.84% | 5.60 | 6.28 | 89.23% | 122.51 | 628.16 | 19.50% |
| Fast candidate selection, 1-MiB varied text, level 2 | 3326.70 | 3467.66 | 95.94% | 5.22 | 6.13 | 85.16% | 122.51 | 628.16 | 19.50% |

The longer level-2 confirmation (six reversed-order 16-GiB trials) retained a
small mixed gain of 0.59% (2716.05 to 2732.17 MiB/s), with overlapping ranges
2484.14–2774.18 / 2660.86–2808.49. Varied text improved 2.90% (3208.58 to
3301.50), with disjoint ranges 3160.20–3252.16 / 3264.08–3392.15. Instruction
changes remained +0.39%/+2.28%. All frame/allocation checks passed. A separate
sanitizer-backed oracle also passed 39,900 parser blocks covering hash collisions,
prefix boundaries, non-default skip distance, resets and 32-bit index rollover.
These measurements preceded the completed acceptance/sanitizer validation and integration.

| Level-2 conditional-move confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Fast candidate selection, 1-MiB mixed, level 2 | 2732.17 | 2535.97 | 107.74% | 5.59 | 6.24 | 89.54% | 122.51 | 628.16 | 19.50% |
| Fast candidate selection, 1-MiB varied text, level 2 | 3301.50 | 3470.28 | 95.14% | 5.24 | 6.13 | 85.50% | 122.51 | 628.16 | 19.50% |

Artifacts: `out/aligned-strategy-control/fast-cmov-{65536,262144,1048576}-*` and
`fast-cmov-confirm-262144-*`.

### Fast minMatch-6 scalar match counting (2026-09-27, rejected)

Branch-miss profiling of 256-KiB varied text attributed about 80% of C++ misses
to the fast parser. An isolated experiment replaced its two-word match-count
unroll with the reference-shaped one-word loop, leaving other strategies alone.
Six balanced 8-GiB baseline/candidate/reference trials per corpus did not support
retention: varied throughput fell 1.30% (3355.90 to 3312.32 MiB/s), instructions
rose 2.89%, and median branch misses stayed essentially unchanged (29.274M to
29.435M; reference 2.821M). Mixed throughput fell 5.14% (2463.19 to 2336.62),
with instructions up 1.68%. Timing ranges overlap; neither case shows a benefit.
Unit, exact-frame, and allocation checks passed. Production was not changed.

| Rejected experiment | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Scalar match count, 256-KiB varied text, level 1 | 3312.32 | 4009.97 | 82.60% | 4.33 | 4.38 | 98.84% | 122.50 | 628.16 | 19.50% |
| Scalar match count, 256-KiB mixed, level 1 | 2336.62 | 2493.75 | 93.70% | 4.47 | 4.51 | 99.05% | 122.50 | 628.16 | 19.50% |

Artifacts: `out/aligned-strategy-control/fast-scalar-count-262144-*`.

### Fast-path literal alignment (2026-09-27, retained for GCC 13)

The measured fast-path policy is now integrated for GCC 13 only; other compilers
retain their previous allocation policy. The integrated 64-KiB worker's machine-code
text matches the measured candidate. All six integrated GCC tests pass, including
all-level bidirectional acceptance and the new permanent compressor-storage reuse
test. Candidate validation also passed full-unit ASan/UBSan, 270 boundary/pattern/
streaming/reuse cases, and the custom sampling/history/fast-frame sanitizer tests.
Windows candidate unit, all-level acceptance, and custom-frame tests also passed;
no Windows speedup is claimed and production Windows allocation policy is unchanged.
The earlier post-alignment full-level report included this integration. All 88 measurements
(2,000 frames each), all nine metrics in all 44 README rows, raw worker CSVs,
actual executable sizes, corpus size, and all 22 exact compressed-frame pairs
passed an independent audit. Raw results are under
`out/full-metrics-20260927/after-fast-literal-alignment`.
On that historical 1-MiB mixed corpus, compression led at 20 of 22 levels: level 7 measured
99.36% and level 15 measured 88.21% of reference and need confirmation.
Decompression led at all 22 levels (105.20–170.02% of reference).
The alignment change is inactive at levels 7 and 15; this screen does not
establish a causal regression. Small-input and varied-text gaps also remain,
so the overall performance goal is not complete.

Six alternating 2,000-frame confirmation pairs did not reproduce those matrix
deficits. Level 7 won all six pairs (1699.79 versus 1500.49 MiB/s median).
Level 15 had a higher C++ median (374.81 versus 352.66), but split the pairs
three each and had wide overlapping ranges: C++ 318.02–410.84, reference
275.65–389.86. Its median paired ratio was 106.81%, distinct from the 106.28%
ratio of medians below. All frame/allocation checks passed. These confirmations
supplement the audited matrix rather than replacing its rows; they do not prove
stable wins across workloads. The confirmed 256-KiB varied-text deficit is the
next profiling target.

| Post-alignment confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Mixed compression level 15 | 374.81 | 352.66 | 106.28% | 21.19 | 21.99 | 96.33% | 122.65 | 628.16 | 19.53% |
| Mixed compression level 7 | 1699.79 | 1500.49 | 113.28% | 10.24 | 10.98 | 93.28% | 122.82 | 628.16 | 19.55% |

Raw confirmation artifacts: `out/aligned-strategy-control/fast-align-matrix-l{7,15}-confirm-*`.
Historical experiment notes follow.

A fresh 64-KiB mixed-input profile attributed 13.19% of C++ samples to the
raw-literal vector insertion. Diagnostic copy tracing found essentially the
same raw-copy volume as reference, but C++ source/destination page offsets of
16/19 versus reference 3936/1517. These diagnostic addresses suggest an
alignment-sensitive copy cost; they do not alone prove its cause.

The isolated candidate applied the existing aligned literal allocator to fast
presets with minimum match below seven. Production was unchanged during these trials. Six balanced
baseline/candidate/reference trials per corpus (GCC13 Release, CPU2, 131072
frames / 8 GiB per trial) passed exact-frame and allocation guards. Mixed-input
median throughput rose from 1585.71 to 1795.71 MiB/s (+13.24%); ranges
1480.37–1624.92 and 1620.57–1843.21 overlap slightly. Instructions were unchanged,
while median cycles fell from 23.159B to 20.270B. Varied text was effectively
neutral: 3508.02 to 3536.47 MiB/s (+0.81%), with overlapping ranges
3330.83–3533.76 and 3457.43–3548.35. Strict unit tests passed. Longer confirmation,
larger-input controls, acceptance and sanitizer gates remain before retention.

| Experimental compression case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Fast aligned literals, 64-KiB mixed, level 1 | 1795.71 | 1897.26 | 94.65% | 4.10 | 4.01 | 102.29% | 122.50 | 628.16 | 19.50% |
| Fast aligned literals, 64-KiB varied text, level 1 | 3536.47 | 3534.66 | 100.05% | 4.12 | 4.01 | 102.83% | 122.50 | 628.16 | 19.50% |

The longer confirmation used the same binaries, reversed trial order and twice
the work (262144 frames / 16 GiB per trial). Mixed throughput improved from
1597.26 to 1798.20 MiB/s (+12.58%), with disjoint baseline/candidate ranges
1576.63–1621.10 and 1740.35–1836.53. Instructions remained unchanged; median
cycles fell from 45.499B to 40.505B. Varied text was neutral: 3546.60 to
3547.89 MiB/s (+0.04%), overlapping ranges 3486.80–3597.16 and
3468.79–3574.44. All exact-frame and allocation guards passed. The small-mixed
gain is repeatable, but larger-input and broader correctness gates are still
required before integration.

| Confirmation compression case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Fast aligned literals, 64-KiB mixed, level 1 | 1798.20 | 1893.55 | 94.96% | 4.12 | 4.01 | 102.83% | 122.50 | 628.16 | 19.50% |
| Fast aligned literals, 64-KiB varied text, level 1 | 3547.89 | 3498.21 | 101.42% | 4.12 | 4.00 | 102.98% | 122.50 | 628.16 | 19.50% |

Larger-input controls (six balanced 8-GiB trials each) were much less sensitive:
256-KiB mixed changed by −0.07% (2413.84 to 2412.25 MiB/s), 256-KiB varied by
−0.65% (3436.13 to 3413.77), 1-MiB level-2 mixed by +1.84% (2766.56 to 2817.51),
and 1-MiB level-2 varied by −1.18% (3249.68 to 3211.35). All baseline/candidate
ranges overlap, all frame/allocation guards pass, and instruction counts are
essentially unchanged. The possible level-2 varied-text downside needs longer
confirmation. The disabled 1-MiB level-1 minMatch-7 path has byte-identical
machine-code text. Production remains unchanged.

| Larger-input compression control | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Fast aligned literals, 256-KiB mixed, level 1 | 2412.25 | 2479.96 | 97.27% | 4.50 | 4.48 | 100.39% | 122.50 | 628.16 | 19.50% |
| Fast aligned literals, 256-KiB varied text, level 1 | 3413.77 | 4015.32 | 85.02% | 4.37 | 4.37 | 100.00% | 122.50 | 628.16 | 19.50% |
| Fast aligned literals, 1-MiB mixed, level 2 | 2817.51 | 2544.42 | 110.73% | 5.62 | 6.26 | 89.76% | 122.51 | 628.16 | 19.50% |
| Fast aligned literals, 1-MiB varied text, level 2 | 3211.35 | 3450.00 | 93.08% | 5.21 | 6.10 | 85.43% | 122.51 | 628.16 | 19.50% |

The longer level-2 varied-text downside check (six reversed-order 16-GiB
trials, unchanged binaries) was nearly neutral: 3240.50 to 3234.08 MiB/s
(−0.20%). Baseline/candidate ranges 3115.38–3281.44 and 3107.65–3275.98 overlap;
instructions are unchanged and median cycles were 22.628B / 22.788B. Exact-frame
and allocation checks passed. The earlier 1.18% slowdown did not reproduce.

| Downside confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Fast aligned literals, 1-MiB varied text, level 2 | 3234.08 | 3471.42 | 93.16% | 5.25 | 6.14 | 85.41% | 122.51 | 628.16 | 19.50% |

Artifacts: `out/aligned-strategy-control/fast-align-{65536,262144,1048576}-*` and
`fast-align-confirm-65536-*`, with diagnostic
call stacks and copy tracing under `index32-small-copy-*`. These are candidate
measurements, not a replacement for the integrated full-level report.

### Narrow FSE transition indices (2026-09-27, retained)

The scoped grouped-FSE change is now integrated. The production header matches
the measured candidate exactly. Retention is based on repeated mixed-input
median gains and a broader screen without a material regression; overlapping
ranges mean the small percentage gains should not be treated as universal.
The earlier post-index-change full-matrix report included this change. Its 88 measurements
(2,000 frames each), all nine metrics in all 44 README rows, raw worker CSVs,
actual executable sizes, corpus size, and all 22 exact compressed-frame pairs
have been independently verified. Raw results are under
`out/full-metrics-20260927/after-narrow-fse-indices`.

On that historical 1-MiB mixed-corpus run, compression led reference at 20 of 22 levels.
Level 7 measured 98.92% and level 14 measured 82.92% of reference in that screen.
Six alternating confirmation pairs did not reproduce the level-7 deficit:
C++ won all six, with median rates of 1,610.47 versus 1,404.42 MiB/s.
Level 14 remains uncertain: median rates were 378.07 versus 389.76 MiB/s,
but each implementation won three pairs, and ranges overlapped widely
(C++ 336.02–459.35; reference 306.35–417.72). The median paired ratio was
105.65%, distinct from the 97.00% ratio of median rates reported below.
The narrow-index path is not active at either level, so these
measurements do not establish a regression caused by this optimization.
Decompression led at all 22 levels (109.10–170.01% of reference).
Known small-input and varied-text gaps still prevent an overall completion claim.

| Confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Mixed compression level 14 | 378.07 | 389.76 | 97.00% | 21.20 | 21.99 | 96.40% | 122.65 | 628.16 | 19.53% |
| Mixed compression level 7 | 1610.47 | 1404.42 | 114.67% | 10.25 | 11.00 | 93.20% | 122.82 | 628.16 | 19.55% |

All confirmation frame/allocation checks passed. Level-14 median instructions
were 32.290B versus 36.609B, with cycles 23.798B versus 23.101B. Subsequent
profiling and the rejected DUBT experiment are documented above. These historical confirmations supplement rather
than replace the original full-matrix rows. Raw files:
`out/aligned-strategy-control/index32-matrix-l{7,14}-confirm-*`.

Validation passed: full GCC ASan/UBSan units; 3,676,128 randomized/negative-one
state-index checks plus transition/RLE checks; custom fast-parameter and
Huffman-history cross-decoding tests; MSVC units/acceptance/custom-frame tests
(correctness-only debug-runtime link); and all five integrated GCC CTest suites.
The expanded oracle is registered permanently as `fse_transition_tests`.
The non-grouped level-3 worker's instruction text is unchanged.

This independent candidate keeps the production writer unchanged and computes
grouped-FSE transition indices with 32-bit unsigned arithmetic. Valid transitions
have nonnegative indices at most 511; the generic transition remains unchanged.
Sanitizer comparisons cover every state/symbol combination in the tested tables,
random transition chains, RLE reuse, and invalid-symbol rejection. Strict GCC unit
and all-level acceptance tests passed.

Six balanced 8-GiB trials per case gave small positive medians, but both
baseline/candidate ranges overlap. This is **not yet a confirmed improvement**:
256-KiB mixed measured +2.13% versus production's 2,436.85 MiB/s
(2,220.06–2,519.43 versus 2,469.11–2,520.90); 64-KiB varied text measured +0.67%
versus 3,525.90 MiB/s (3,427.74–3,580.42 versus 3,533.28–3,568.20).
Instructions increased 0.27% and 0.29%, respectively. All frame comparisons and
allocation guards passed. The initial and confirmation measurements are retained
below as the evidence for the integrated change.

| Diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Narrow FSE indices, 256-KiB mixed, level 1 | 2488.73 | 2499.97 | 99.55% | 4.49 | 4.49 | 100.17% | 122.50 | 628.16 | 19.50% |
| Narrow FSE indices, 64-KiB varied text, level 1 | 3549.59 | 3509.76 | 101.13% | 4.08 | 4.00 | 101.80% | 122.50 | 628.16 | 19.50% |

Raw artifacts: `out/aligned-strategy-control/fse-index32-*`.

The first two broader controls (six balanced 8-GiB trials each) also favored
the candidate: 64-KiB genuine mixed input +2.43% versus production's
1,571.48 MiB/s; 256-KiB varied text +1.55% versus 3,292.77 MiB/s.
Ranges overlap: 1,532.75–1,609.76 versus 1,561.59–1,633.78 for mixed, and
3,133.16–3,406.15 versus 3,244.15–3,423.11 for text. Instructions increased
0.46% and 0.30%; median cycles decreased 1.97% and 0.80%.
Both remain substantially below reference throughput. All frame/allocation
checks passed.

| Diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Narrow FSE indices control, 64-KiB mixed, level 1 | 1609.64 | 1897.60 | 84.83% | 4.11 | 4.00 | 102.73% | 122.50 | 628.16 | 19.50% |
| Narrow FSE indices control, 256-KiB varied text, level 1 | 3343.82 | 3995.30 | 83.69% | 4.35 | 4.38 | 99.29% | 122.50 | 628.16 | 19.50% |

The 1-MiB controls are complete as well: level 2 mixed +2.65% versus production's
2,582.50 MiB/s; level 2 varied -0.13% versus 3,071.34; compact-storage level 1
mixed -0.01% versus 2,162.51; and level 1 varied +0.43% versus 3,101.54.
All timing ranges overlap. This screen found no material regression, but the
small gains remain workload-dependent; broader sanitizer validation is underway.

| Diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Narrow FSE indices control, 1-MiB mixed, level 2 | 2651.02 | 2429.33 | 109.13% | 5.62 | 6.24 | 90.05% | 122.51 | 628.16 | 19.50% |
| Narrow FSE indices control, 1-MiB varied text, level 2 | 3067.37 | 3266.19 | 93.91% | 5.22 | 6.11 | 85.55% | 122.51 | 628.16 | 19.50% |
| Narrow FSE indices control, 1-MiB mixed, level 1 | 2162.36 | 2070.68 | 104.43% | 5.38 | 6.12 | 87.84% | 126.71 | 628.16 | 20.17% |
| Narrow FSE indices control, 1-MiB varied text, level 1 | 3114.96 | 3271.97 | 95.20% | 5.12 | 5.88 | 87.02% | 126.71 | 628.16 | 20.17% |

Longer reversed-order confirmation (six 16-GiB trials per case) again favored
the candidate on mixed input: +2.82% versus production's 2,289.09 MiB/s.
Ranges still overlap widely (production 2,218.98–2,504.82; candidate
2,184.06–2,477.86), and instructions increased 0.27%. Median cycles decreased
from 31.907 to 31.319 billion. Small varied text was neutral at +0.03%
versus production's 3,518.75 MiB/s, with overlapping ranges
(3,323.16–3,559.66 versus 3,498.34–3,549.95) and essentially unchanged cycles.
This supports further controls, not integration or a universal speedup claim.

| Diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Narrow FSE indices confirmation, 256-KiB mixed, level 1 | 2353.67 | 2484.89 | 94.72% | 4.45 | 4.49 | 99.13% | 122.50 | 628.16 | 19.50% |
| Narrow FSE indices confirmation, 64-KiB varied text, level 1 | 3519.70 | 3482.98 | 101.05% | 4.10 | 3.98 | 102.89% | 122.50 | 628.16 | 19.50% |

### External-buffer writer specialization (2026-09-27, experimental)

An isolated candidate removes the internal/external buffer selection at 32-bit
flushes only in the grouped-FSE path, where the caller has already selected an
external output. Capacity growth, packing, and the generic writer remain unchanged.
The first six balanced 8-GiB trials on genuine 256-KiB mixed input showed +4.31%
against production (2,237.92 MiB/s), but ranges overlapped widely
(production 2,004.44–2,407.15; candidate 2,312.34–2,402.59 MiB/s).
Instructions increased 0.60%, so this is **not a confirmed improvement**.

| Diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| External writer candidate, 256-KiB mixed, level 1 | 2334.36 | 2418.08 | 96.54% | 4.49 | 4.50 | 99.91% | 122.48 | 628.16 | 19.50% |

The candidate passed 17,952 ASan/UBSan boundary/reuse cases and the GCC unit and
all-level acceptance tests. All timing frame comparisons and allocation guards
passed. A six-trial reversed-order confirmation at 16 GiB per trial also favored
the candidate: +4.09% versus production's 2,282.01 MiB/s. Ranges still overlapped
(production 2,127.22–2,409.88; candidate 2,161.15–2,459.54 MiB/s). Median cycles fell
from 31.731 to 30.289 billion despite 0.60% more instructions. This warrants broader
controls, not integration yet; production remains unchanged.

| Diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| External writer confirmation, 256-KiB mixed, level 1 | 2375.30 | 2434.08 | 97.59% | 4.48 | 4.49 | 99.78% | 122.48 | 628.16 | 19.50% |

Raw artifacts: `out/aligned-strategy-control/external-writer-*`.

Broader six-balanced-trial controls (8 GiB per trial) have not reproduced the
original gain in the first two completed cases: 64-KiB genuine mixed input
measured -0.63% versus production's 1,591.81 MiB/s, while 256-KiB varied text
measured +0.05% versus 3,289.59 MiB/s. Both before/after ranges overlap.
Instructions increased 0.80% and 0.48%, respectively. Remaining controls are
pending; these results do not support claiming a general speedup.

| Diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| External writer control, 64-KiB mixed, level 1 | 1581.73 | 1885.59 | 83.89% | 4.12 | 4.00 | 102.83% | 122.48 | 628.16 | 19.50% |
| External writer control, 256-KiB varied text, level 1 | 3291.25 | 3928.57 | 83.78% | 4.37 | 4.38 | 99.82% | 122.48 | 628.16 | 19.50% |
| External writer control, 64-KiB varied text, level 1 | 3353.02 | 3421.60 | 98.00% | 4.04 | 4.00 | 101.12% | 122.48 | 628.16 | 19.50% |

The next completed control, 64-KiB varied text, measured -3.17% versus production's
3,462.79 MiB/s. Ranges overlap (production 3,403.45–3,478.32; candidate
3,260.29–3,477.58), but median cycles increased 2.32% and instructions 0.48%.
This downside must be resolved before any retention decision.

The four 1-MiB controls are also complete. Level 2 mixed input favored the
candidate (+5.88%), but level 2 varied text (-0.75%), level 1 mixed (-1.32%),
and level 1 varied (+0.91%) do not establish a broad improvement. All ranges
overlapped. Production medians were 2,432.05, 3,061.87, 2,281.48, and
3,218.94 MiB/s, respectively. All frame/allocation checks passed.

| Diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| External writer control, 1-MiB mixed, level 2 | 2575.14 | 2441.12 | 105.49% | 5.62 | 6.25 | 89.90% | 122.48 | 628.16 | 19.50% |
| External writer control, 1-MiB varied text, level 2 | 3038.87 | 3356.93 | 90.53% | 5.25 | 6.13 | 85.62% | 122.48 | 628.16 | 19.50% |
| External writer control, 1-MiB mixed, level 1 | 2251.43 | 2145.30 | 104.95% | 5.34 | 6.12 | 87.26% | 126.77 | 628.16 | 20.18% |
| External writer control, 1-MiB varied text, level 1 | 3248.33 | 3453.02 | 94.07% | 5.09 | 5.88 | 86.56% | 126.77 | 628.16 | 20.18% |

The longer reversed-order 64-KiB varied-text check (six 16-GiB trials) measured
-0.74% versus production's 3,516.98 MiB/s. The initial -3.17% magnitude was not
reproduced, but the sign remained negative; ranges overlap (production
3,448.78–3,570.71; candidate 3,396.27–3,514.69). Instructions increased 0.48%
and cycles 0.74%. The candidate remains unintegrated while its code-generation
tradeoffs are investigated.

| Diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| External writer confirmation, 64-KiB varied text, level 1 | 3490.89 | 3500.02 | 99.74% | 4.12 | 3.98 | 103.33% | 122.48 | 628.16 | 19.50% |

### Sequence-emitter isolation diagnostic (2026-09-27, not retained)

Extracting the unchanged grouped-FSE emission loop into a separate non-inlined function
did not demonstrate a gain. Six balanced baseline/candidate/reference permutations,
8 GiB per trial, on the genuine 256-KiB mixed corpus measured production at
2,476.09 MiB/s versus the candidate's 2,463.23 MiB/s (-0.52%). Their ranges overlapped
(2,321.77–2,501.55 versus 2,423.64–2,487.85 MiB/s). Instructions increased 0.47%;
the extracted emitter still used a substantial stack frame.

| Diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Isolated sequence emitter, 256-KiB mixed, level 1 | 2463.23 | 2496.78 | 98.66% | 4.47 | 4.49 | 99.57% | 122.55 | 628.16 | 19.51% |

These are candidate diagnostics, **not** updated production results. Strict GCC unit
and all-level acceptance tests passed, as did every benchmark's exact-frame comparison
and allocation guard. Production was left unchanged. Raw results are under
`out/aligned-strategy-control/sequence-outline-262144-scaled-mixed-*`.

### GCC13 scalar FSE follow-up

Forced inlining of the scalar FSE transition is now limited to GCC13's fast,
double-fast, and greedy sequence writers. Other compilers keep the original
method, and lazy/optimal writers retain ordinary inlining. The unrestricted
experiment regressed level 7 and was not retained.

Five alternating baseline/candidate/reference trials, 2,000 frames each, generic
GCC13 `-O3 -DNDEBUG`, WSL CPU 2, improved levels 3/4/5 by **9.8% / 13.5% / 8.5%**
over saved binaries. Every frame matches reference and passes the allocation guard.
The retained implementation's complete compression metrics are:

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 3 | 1684.47 | 1709.24 | 98.55% | 6.25 | 6.72 | 92.97% | 123.33 | 628.16 | 19.63% |
| 4 | 1680.54 | 1583.43 | 106.13% | 7.75 | 8.02 | 96.69% | 123.33 | 628.16 | 19.63% |
| 5 | 2160.74 | 1923.15 | 112.35% | 8.62 | 8.53 | 101.01% | 122.68 | 628.16 | 19.53% |
| 7 | 2325.03 | 1246.43 | 186.54% | 11.75 | 11.01 | 106.74% | 122.77 | 628.16 | 19.55% |
| 18 | 42.87 | 30.28 | 141.56% | 22.34 | 22.38 | 99.84% | 136.70 | 628.16 | 21.76% |

Level 18 used 100 frames per trial; its median was 1.7% below the saved baseline,
with overlapping ranges, while level 7 recovered from the unrestricted change.
The level-18 comparison also includes the earlier unused-table memory reduction.
Candidate/reference throughput ranges were 1648.74–1745.05 / 1615.44–1728.08
(level 3), 1567.17–1699.18 / 1511.16–1663.11 (4), 2131.32–2223.94 /
1815.72–1995.00 (5), 2060.66–2364.80 / 969.78–1388.07 (7), and
42.37–44.24 / 26.71–36.96 (18). Memory is median process peak RSS; code is the
complete worker executable; percentages use unrounded values. Raw data:
`out/level16-probe/fse-scoped-l*-*.csv`. Level 3's remaining narrow gap and the
broader-corpus parity differences are still open; this is not an all-level win.

### Binary-tree memory follow-up

Binary-tree strategies no longer allocate the byte-tag table used only by the
hash-chain parser. At level 13 this removes a 2 MiB table and reduces measured
median peak RSS from **23.22 to 21.25 MiB** (8.5%). Five alternating 1,000-frame
baseline/candidate/reference trials, generic GCC13 `-O3 -DNDEBUG`, WSL CPU 2,
preserve exact reference frames and pass post-warmup allocation checks.
GCC and MSVC unit/acceptance suites pass. Throughput medians are 410.51 before
and 420.00 MiB/s after; broad ranges (302.09–458.28 and 383.98–455.62) do not
establish a reliable throughput gain from this memory reduction.

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 13 | 420.00 | 368.89 | 113.86% | 21.25 | 21.99 | 96.63% | 126.51 | 628.16 | 20.14% |

Memory is median process peak RSS; code is the full worker executable. Reference
throughput ranged from 294.40 to 386.79 MiB/s. Data: `out/level16-probe/tagless-*.csv`.
The same table removal applies to binary-tree levels 13–22, but this follow-up
measured level 13 only. Separate five-pair 2,000-frame checks confirm compression
gaps at levels 3/4/5 (89.01%/88.58%/97.53% of reference), not level 13.

Historical full-metric snapshot (2026-09-26 refresh, including row-matcher integration
and greedy batch-local insertion): all workers rebuilt with generic GCC13
`-O3 -DNDEBUG`, tracing disabled, WSL CPU2,200 timed frames per worker.
This is one screening run, not the median of alternating pairs. Do not compare
it with historical snapshots as a controlled speedup. Raw data:
`out/full-metrics-20260926-refresh/metrics.csv`; an independent verifier checks
all44 table rows and all nine metrics against the88 raw measurements.

Compression levels13,16,17 are below reference in this screen (84.07%,93.19%,
88.95%). Level3 decompression is a near-tie at99.62%; other decompression rows
are ahead. These apparent gaps require repeated paired validation. All22
mixed-corpus compressed frames are reference-identical, decoded outputs match
the input, and allocation guards pass. This does not establish an all-input,
all-level performance victory. Earlier snapshots and confirmations remain in
`out/full-metrics-20260926`; their numbers do not describe this refreshed run.

Five alternating-pair confirmations weaken the mixed-corpus alarms: compression
level13 is413.17 /391.31 MiB/s (105.59% of reference), level16 is92.01 /92.93
(99.00%), and level17 is88.62 /84.47 (104.90%). Level3 decompression is7348.71 /
7001.40 MiB/s (104.96%). Ranges overlap in all four cases; these are neither
controlled speedup claims nor proof of an all-level win. Compression trials use
1000 frames at13 and300 at16/17; decompression uses5000 frames. Artifacts:
`out/full-metrics-20260926-refresh/{paired-l*-*.csv,decode3-*.csv}`.

The refreshed20-frame varied-text screen also passes exact reference bytes and
reciprocal decoding at all22 levels. Its fastest workers run too briefly for
firm performance conclusions; longer paired confirmations are needed. Artifacts:
`out/full-metrics-20260926-refresh/varied-l*`.

Longer varied-text confirmations (five alternating pairs,5000 frames at3,
500 at6/8) identify stronger remaining gaps. At3, C++ spans1860.32–2033.26
MiB/s versus reference2580.79–2601.53; at6,190.46–240.94 versus275.31–306.25.
These ranges do not overlap. Level8 ranges overlap, so its smaller gap is less
conclusive. Medians, peak RSS and complete executable sizes follow:

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3 / varied text | 2025.72 | 2586.24 | 78.33 | 5.96 | 6.58 | 90.68 | 123.25 | 628.16 | 19.62 |
| 6 / varied text | 227.45 | 300.20 | 75.77 | 7.49 | 8.38 | 89.46 | 122.69 | 628.16 | 19.53 |
| 8 / varied text | 168.99 | 184.34 | 91.67 | 9.99 | 10.84 | 92.21 | 122.60 | 628.16 | 19.52 |

Raw paired data: `out/full-metrics-20260926-refresh/varied-paired-l*-*.csv`.
All compared frames remain byte-identical. These gaps, alongside the known
varied-text optimal-parser deficits, keep the overall performance goal open.

### Double-fast frame-base experiment (2026-09-26)

GCC 13 Release, CPU 2 pinned, five alternating baseline/candidate/reference
trials of 10,000 frames on each 1 MiB corpus. The candidate caches the invariant
frame-index base during block parsing. Numbers below are medians; memory is peak
process RSS and code is the complete executable size, not header size.

| Corpus / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Varied text / 3 | 2115.83 | 2584.36 | 81.87% | 5.99 | 6.64 | 90.24% | 122.94 | 628.16 | 19.57% |
| Varied text / 4 | 2127.25 | 2527.79 | 84.15% | 7.49 | 7.87 | 95.23% | 122.94 | 628.16 | 19.57% |
| Mixed / 3 | 1892.02 | 1772.78 | 106.73% | 6.24 | 6.78 | 92.05% | 122.94 | 628.16 | 19.57% |
| Mixed / 4 | 1900.70 | 1712.33 | 111.00% | 7.74 | 8.02 | 96.59% | 122.94 | 628.16 | 19.57% |

Compared with the saved pre-change C++ executable, median changes are +1.62%,
+2.62%, +4.50%, and +6.50%, respectively. Only mixed level 3 has disjoint
before/after trial ranges (1778.80–1821.93 versus 1842.22–1932.78 MiB/s).
The other cases remain noisy despite positive medians in both the initial and
longer runs. All frame comparisons and allocation guards passed, as did GCC
unit and expanded reciprocal-decoding acceptance tests. ASan/UBSan units and
an assertion-enabled level-3 varied-text/reset check also passed. MSVC Debug
unit/expanded acceptance passed; additional reset tests cover minimum-match
lengths 4/5/6, clipped windows and frame-index wraparound under GCC, MSVC and
ASan/UBSan. Rebuilt level 5 is byte-identical to its saved pre-change executable.
The frame-base hoist is retained after the small-source controls below passed,
with improvements on multiple workloads and no established regression in the
tested controls. This is not an all-level improvement claim or an MSVC speed
claim. Raw results: `out/full-metrics-20260926/double-local-confirm*`.

Small-source varied-text follow-up (same GCC 13 optimization and CPU pinning,
five alternating trials, 1 GiB processed per worker/trial):

| Input / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 KiB / 2 | 1766.53 | 1738.49 | 101.61% | 4.11 | 3.87 | 106.26% | 118.54 | 628.16 | 18.87% |
| 8 KiB / 3 | 1467.39 | 1612.94 | 90.98% | 4.09 | 3.89 | 105.12% | 122.78 | 628.16 | 19.55% |
| 64 KiB / 2 | 3035.01 | 3486.28 | 87.06% | 4.24 | 4.09 | 103.63% | 118.36 | 628.16 | 18.84% |
| 64 KiB / 3 | 2495.17 | 3102.97 | 80.41% | 4.46 | 4.31 | 103.53% | 122.93 | 628.16 | 19.57% |
| 256 KiB / 2 | 2492.79 | 3507.03 | 71.08% | 4.37 | 4.47 | 97.64% | 122.93 | 628.16 | 19.57% |
| 256 KiB / 3 | 2048.23 | 2734.42 | 74.91% | 4.87 | 4.86 | 100.24% | 122.80 | 628.16 | 19.55% |

At 256 KiB, level 2 improved 4.69% over the isolated pre-hoist build, with
disjoint trial ranges (2322.64–2400.38 versus 2452.62–2574.83 MiB/s).
Other before/after ranges overlap. Level 2 at 8/64 KiB selects fast rather
than double-fast and serves as an unaffected control. Exact frames and worker
allocation checks passed. Balanced small-corpus confirmation is complete below.
Initial `mixed-*` files in `out/double-small-control` are zero-only prefixes,
not balanced mixed-corpus evidence; properly generated controls use `balanced-*`.

Balanced small-corpus controls (size-specific zero/text/structured/random quarters;
five alternating trials, 4 GiB processed per worker/trial):

| Input / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 KiB / 2 | 721.51 | 993.41 | 72.63% | 4.11 | 3.88 | 105.94% | 118.54 | 628.16 | 18.87% |
| 8 KiB / 3 | 527.27 | 752.99 | 70.02% | 4.12 | 3.89 | 106.03% | 122.78 | 628.16 | 19.55% |
| 64 KiB / 2 | 1327.27 | 1816.78 | 73.06% | 4.23 | 4.14 | 102.17% | 118.36 | 628.16 | 18.84% |
| 64 KiB / 3 | 1168.59 | 1520.15 | 76.87% | 4.62 | 4.25 | 108.64% | 122.93 | 628.16 | 19.57% |
| 256 KiB / 2 | 1828.27 | 2028.02 | 90.15% | 4.62 | 4.47 | 103.41% | 122.93 | 628.16 | 19.57% |
| 256 KiB / 3 | 1382.35 | 1896.34 | 72.90% | 5.12 | 4.86 | 105.47% | 122.80 | 628.16 | 19.55% |

Level 3 before/after median changes are +3.03%, +5.17%, and +2.98% as size
increases. Only the 256 KiB case has disjoint ranges (1302.41–1371.77 versus
1374.62–1406.41 MiB/s). Level 2 changes are +0.19%, -0.90%, and -0.66%, all
with overlapping ranges. All exact-frame and allocation checks passed. These
results expose remaining small-input deficits despite the retained improvement.
Artifacts: `out/double-small-control/balanced-*`.

### Literal-workspace offset experiments (2026-09-26, rejected)

Both fixed and conditional encoder-prefix variants were removed after the
broader controls below failed to establish a sufficiently consistent benefit.
The measurements remain here as experimental evidence, not current performance.

Shifting non-precollected literals by 64 bytes within initially reserved scratch
storage tests the nearly identical page offsets observed in the hot raw-literal
copy. GCC 13 Release, CPU 2, five alternating baseline/candidate/reference trials,
16,384 frames per trial, 256 KiB level-3 input:

| Corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Balanced | 1581.65 | 1966.42 | 80.43% | 5.12 | 4.86 | 105.31% | 122.80 | 628.16 | 19.55% |
| Varied text | 2102.26 | 2762.49 | 76.10% | 4.88 | 4.89 | 99.60% | 122.80 | 628.16 | 19.55% |

Balanced throughput improved 11.90% over the current pre-offset C++ baseline
(1413.49 MiB/s); ranges are disjoint: 1391.98–1437.55 versus1530.61–1632.59.
Varied text changed -0.89%, with overlapping ranges. GCC unit and expanded
interoperability tests, exact-frame comparisons, and allocation guards pass.
ASan/UBSan units and an assertion-enabled reference-frame/reset check pass.
Address tracing confirms source/destination page offsets changed from16/19
to80/19 for the large raw copy. This initial result required broader controls;
the timing alone does not prove a particular hardware aliasing mechanism.
Artifacts: `out/double-small-control/offset64-*`.

The first 1 MiB follow-up (five alternating 5,000-frame trials) is mixed:

| Corpus / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Varied / 3 | 1973.90 | 2481.70 | 79.54% | 5.96 | 6.65 | 89.72% | 122.94 | 628.16 | 19.57% |
| Varied / 4 | 1951.96 | 2360.41 | 82.70% | 7.49 | 7.90 | 94.86% | 122.94 | 628.16 | 19.57% |
| Mixed / 3 | 1701.34 | 1536.17 | 110.75% | 6.21 | 6.63 | 93.75% | 122.94 | 628.16 | 19.57% |
| Mixed / 4 | 1758.56 | 1640.08 | 107.22% | 7.75 | 7.97 | 97.21% | 122.94 | 628.16 | 19.57% |

Before/after changes are -3.81%, -0.17%, -4.08%, and +0.04%, respectively,
with overlapping ranges in every case. Frame/allocation checks pass, but this
did **not** support global retention. The subsequent controls below led to
removing the encoder-prefix approach. Artifacts:
`out/full-metrics-20260926/offset64-large*`.

Longer level-3 confirmation (five alternating 10,000-frame trials):

| Corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied | 2055.25 | 2534.23 | 81.10% | 6.00 | 6.65 | 90.25% | 122.94 | 628.16 | 19.57% |
| 1 MiB mixed | 1770.91 | 1599.54 | 110.71% | 6.21 | 6.72 | 92.45% | 122.94 | 628.16 | 19.57% |

Changes versus pre-offset C++ are -2.12% and -1.35%, with overlapping ranges.
Separate single-run CPU counters show fewer cycles for the 256 KiB balanced
case (25.76B to 22.78B) and 1 MiB mixed case (25.09B to 24.20B), but those
single-run counters do not override the repeated timing results. The fixed
offset was not retained; the subsequent large-literal-only test is shown below.
Artifacts: `offset64-confirm*` under `out/full-metrics-20260926` and
`offset-counter-*` under `out/double-small-control`.

Conditional offset follow-up: apply the 64-byte prefix only with at least 4 KiB
of trailing literals. Same five-trial methodology; 16,384 frames for 256 KiB
cases and 5,000 frames for 1 MiB cases:

| Input / corpus / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 256 KiB / balanced / 3 | 1623.97 | 1983.39 | 81.88% | 5.12 | 4.85 | 105.72% | 122.80 | 628.16 | 19.55% |
| 256 KiB / varied / 3 | 2065.89 | 2813.45 | 73.43% | 4.88 | 4.86 | 100.32% | 122.80 | 628.16 | 19.55% |
| 1 MiB / varied / 3 | 2005.49 | 2490.48 | 80.53% | 6.00 | 6.65 | 90.19% | 122.94 | 628.16 | 19.57% |
| 1 MiB / varied / 4 | 1905.16 | 2364.84 | 80.56% | 7.50 | 7.86 | 95.47% | 122.94 | 628.16 | 19.57% |
| 1 MiB / mixed / 3 | 1621.06 | 1636.40 | 99.06% | 6.25 | 6.71 | 93.13% | 122.94 | 628.16 | 19.57% |
| 1 MiB / mixed / 4 | 1674.97 | 1494.34 | 112.09% | 7.75 | 7.98 | 97.11% | 122.94 | 628.16 | 19.57% |

Versus the pre-offset C++ baseline, changes are +12.64%, -4.41%, -2.32%,
-1.24%, -6.57%, and +6.34%. Only the small balanced gain has disjoint trial
ranges (1435.92–1484.37 versus1596.31–1648.21). All other ranges overlap;
large mixed results are especially noisy. Correctness and allocation checks
pass, but the conditional variant did not resolve the broader trade-off and
was removed. Raw data: `out/double-small-control/conditional-*` and
`out/full-metrics-20260926/conditional-offset*`.

### Cache-aligned literal allocation (2026-09-26, provisional)

This separate experiment uses the existing 64-byte-aligned allocator for the
literal vector, with its original logical size and unchanged encoder indexing.
GCC 13 Release, CPU 2, five alternating trials; 16,384 frames for 256 KiB inputs
and 5,000 frames for 1 MiB inputs. Memory is peak RSS and code is whole-executable size.

| Input / corpus / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 256 KiB / balanced / 3 | 1590.32 | 1966.89 | 80.85% | 5.12 | 4.84 | 105.81% | 122.80 | 628.16 | 19.55% |
| 256 KiB / varied / 3 | 2132.50 | 2800.23 | 76.15% | 4.88 | 4.88 | 100.00% | 122.80 | 628.16 | 19.55% |
| 1 MiB / varied / 3 | 2146.27 | 2490.42 | 86.18% | 5.99 | 6.63 | 90.45% | 123.02 | 628.16 | 19.58% |
| 1 MiB / varied / 4 | 2103.79 | 2408.45 | 87.35% | 7.48 | 7.81 | 95.75% | 123.02 | 628.16 | 19.58% |
| 1 MiB / mixed / 3 | 1739.05 | 1680.60 | 103.48% | 6.25 | 6.75 | 92.59% | 123.02 | 628.16 | 19.58% |
| 1 MiB / mixed / 4 | 1817.86 | 1622.84 | 112.02% | 7.75 | 7.88 | 98.36% | 123.02 | 628.16 | 19.58% |

Changes versus the pre-offset, ordinary-allocator baseline are +10.07%, -1.00%,
+5.75%, +3.10%, +1.36%, and +3.67%. Only the first has disjoint ranges
(1429.34–1482.18 versus1547.46–1614.33 MiB/s); others remain noisy.
GCC unit/expanded interoperability tests and every frame/allocation guard pass.
Alignment/reuse tests pass, as do ASan/UBSan units, the assertion-enabled
reference-frame/reset check, and full MSVC Debug tests. Diagnostic tracing
shows the large copy at page offsets64/19 (source/destination), rather than16/19.
Other strategy performance controls remain pending; this is not yet a broadly
retained optimization or an MSVC performance claim.
Artifacts: `out/double-small-control/aligned-*` and
`out/full-metrics-20260926/aligned-literals*`.

Aligned-allocation strategy controls in progress (isolated baseline differs only
in the literal allocator; same GCC 13 `-O3 -DNDEBUG` flags, 1 MiB inputs):

| Corpus / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Varied / 1 | 2879.44 | 3338.32 | 86.25% | 5.12 | 5.86 | 87.46% | 122.78 | 628.16 | 19.55% |
| Mixed / 1 | 2359.31 | 2108.22 | 111.91% | 5.49 | 6.13 | 89.55% | 122.78 | 628.16 | 19.55% |
| Varied / 5 | 171.41 | 180.77 | 94.82% | 7.50 | 8.34 | 89.84% | 122.59 | 628.16 | 19.52% |
| Mixed / 5 | 1904.77 | 1629.83 | 116.87% | 7.86 | 8.46 | 92.89% | 122.59 | 628.16 | 19.52% |
| Varied / 9 | 149.69 | 163.02 | 91.82% | 15.00 | 15.90 | 94.33% | 122.58 | 628.16 | 19.51% |
| Mixed / 9 | 812.12 | 724.70 | 112.06% | 15.24 | 16.00 | 95.26% | 122.58 | 628.16 | 19.51% |
| Varied / 15 | 54.71 | 47.87 | 114.30% | 21.00 | 21.87 | 96.03% | 122.58 | 628.16 | 19.51% |
| Mixed / 15 | 211.59 | 256.97 | 82.34% | 21.25 | 22.01 | 96.56% | 122.58 | 628.16 | 19.51% |
| Varied / 22 | 2.37 | 1.54 | 154.18% | 21.70 | 22.25 | 97.52% | 141.31 | 628.16 | 22.50% |
| Mixed / 22 | 48.96 | 38.98 | 125.61% | 22.32 | 22.37 | 99.81% | 141.31 | 628.16 | 22.50% |

Five alternating trials per row: 1,000 frames for levels 1/5/9, 300 for level15,
and 20 varied/150 mixed for level22. Before/after median changes are +1.91%,
-0.08%, -4.37%, -5.47%, +2.06%, -10.47%, +3.79%, -20.23%, +1.34%, and
-1.89%; all ranges overlap. Mixed level15 candidate trials span179.07–420.76
MiB/s, so the negative median is not a reliable regression estimate. All
frame/allocation checks pass. The initial audit is complete; longer controls
with CPU-time/cycle/context-switch measurements are running for the questionable
level5/9/15 cases. Alignment remains provisional until these are resolved.
Raw evidence: `out/aligned-strategy-control`.

Longer alignment controls (five alternating 2,000-frame trials with perf CPU
measurements; normal address randomization):

| Corpus / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Varied / 5 | 174.54 | 184.27 | 94.72% | 7.49 | 8.38 | 89.46% | 122.59 | 628.16 | 19.52% |
| Mixed / 5 | 2106.04 | 1955.90 | 107.68% | 7.86 | 8.50 | 92.51% | 122.59 | 628.16 | 19.52% |
| Mixed / 9 | 963.13 | 850.77 | 113.21% | 15.25 | 15.96 | 95.55% | 122.58 | 628.16 | 19.51% |
| Mixed / 15 | 348.79 | 344.67 | 101.20% | 21.21 | 21.98 | 96.52% | 122.58 | 628.16 | 19.51% |

Before/after medians changed +1.80%, +10.44%, -5.30%, and +8.33%, all with
overlapping ranges. The earlier losses at levels5/15 did not repeat. Level9
remains unresolved: baseline ranged550.11–1149.71 MiB/s and candidate
860.61–1065.59. Both had approximately99.8% CPU utilization; median cycles
were8.71B/9.06B (baseline/candidate) despite slightly fewer candidate instructions.
This does not establish scheduling delays as the cause or settle the regression
question. A process-local fixed-address-layout diagnostic is running separately;
it is not a substitute for normal-environment performance. Alignment remains
provisional. Artifacts: `out/aligned-strategy-control/noisy-*`.

Fixed-address-layout diagnostic only (per-process `setarch -R`, five alternating
10,000-frame mixed level-9 trials; normal system settings unchanged):

| C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 967.03 | 887.45 | 108.97% | 15.25 | 16.04 | 95.08% | 122.58 | 628.16 | 19.51% |

The pre-alignment baseline median was1033.09 MiB/s: -6.39%, with overlapping
ranges857.73–1099.62 versus844.78–1015.72. Fixed addresses did not settle the
variability or remove the lower median. Assembly inspection also found changed
FSE inlining: the sequence encoder grew from5044 to6461 bytes and went from
three out-of-line state-update calls to one. An explicit call-boundary control
restored the original function size and three calls but did not recover mixed
level-9 throughput, so it was removed. Global alignment remains provisional;
the next experiment is a compile-time strategy-specific allocation policy.
These diagnostic numbers must
not replace the normal-address-layout benchmarks.

### Strategy-specific literal allocation (2026-09-26, validation in progress)

The workspace selects its literal allocator at compile time. The first policy
trial below used cache-line alignment for all double-fast configurations and
ordinary allocation for other strategies. There is
no runtime policy branch or second literal buffer. This supersedes the global
alignment experiment above; its historical measurements are not measurements
of this new implementation.

GCC 13 Release (`-O3 -DNDEBUG`), five alternating baseline/policy/reference
trials pinned to CPU 2, normal address layout. Baseline is the saved ordinary
allocator implementation. Level 3 uses a balanced 256 KiB corpus and 16,384
frames per trial; level 9 uses the mixed 1 MiB corpus and 4,000 frames.
Memory is peak RSS and code is complete executable size.

| Case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Level 3, 256 KiB balanced | 1576.29 | 1896.76 | 83.10% | 5.13 | 4.85 | 105.64% | 122.88 | 628.16 | 19.56% |
| Level 9, 1 MiB mixed | 927.81 | 741.45 | 125.14% | 15.25 | 15.99 | 95.36% | 122.66 | 628.16 | 19.53% |

Level 3 improves **12.53%** over baseline 1400.82 MiB/s, with disjoint
baseline/policy ranges 1384.46–1453.57 / 1505.81–1627.01. Level 9 changes
**-0.99%** from 937.12 MiB/s; ranges 884.37–982.84 / 822.80–937.99 overlap.
This does not prove unchanged performance for every ordinary-allocation
strategy: templating the shared helpers can still change generated code.
Both cases passed exact reference-frame comparisons and post-warmup allocation
guards. GCC unit and reciprocal acceptance tests passed (levels 1–22).
ASan/UBSan unit tests and an assertion-enabled level-3 repeated-frame/reset
check also passed. Windows MSVC Debug units and full reciprocal acceptance
passed (106.20 seconds); this is correctness evidence, not a Windows speed
measurement. Larger double-fast and broader strategy gates remain before
final retention. Level 3 still trails the reference; the all-level
performance objective is not complete. Raw artifacts:
`out/aligned-strategy-control/policy-*`.

The broader 1 MiB audit is complete. Rows below use five alternating trials
(2,000 frames for levels 1/3/4/5, 300 for level 15, 20 for level 22)
with the same compiler/settings and saved
ordinary-allocation baselines. For levels 1/3/4, these are control results,
not established improvements: all baseline/policy ranges overlap. Median changes for levels
1/3/4 are respectively -0.83%/+2.37%/-7.98% on varied text and
-1.14%/-4.64%/-6.33% on mixed input. Level-4 varied policy throughput ranged
958.72–2000.28 MiB/s, so the lower medians do not establish a precise regression.
They also do not justify retaining alignment for all double-fast presets.
Level 5 mixed improves 5.74%, with disjoint baseline/policy ranges
1682.94–1807.76 / 1811.58–1937.80 MiB/s; varied improves 2.05%, with overlapping
ranges. Level 5 uses ordinary allocation, so this result cannot be attributed
to aligned literals; the workspace/helper template refactor also changes code
generation. Level 15 changes +3.48% on varied text and -10.48% on mixed input,
both with overlapping ranges (mixed baseline217.93–312.70 versus policy
221.19–298.88 MiB/s). Level 22 changes +0.06% varied and +21.33% mixed,
both with overlapping ranges; its short mixed trials are particularly noisy
(baseline31.13–45.86 versus policy33.83–47.22 MiB/s).

| Case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Level 1, varied | 2823.30 | 3244.05 | 87.03% | 5.12 | 5.87 | 87.23% | 123.00 | 628.16 | 19.58% |
| Level 1, mixed | 2319.10 | 2077.65 | 111.62% | 5.50 | 6.14 | 89.50% | 123.00 | 628.16 | 19.58% |
| Level 3, varied | 2016.42 | 2401.80 | 83.95% | 5.99 | 6.65 | 90.08% | 123.11 | 628.16 | 19.60% |
| Level 3, mixed | 1677.84 | 1671.40 | 100.39% | 6.24 | 6.74 | 92.58% | 123.11 | 628.16 | 19.60% |
| Level 4, varied | 1740.47 | 2274.15 | 76.53% | 7.46 | 7.87 | 94.84% | 123.11 | 628.16 | 19.60% |
| Level 4, mixed | 1588.99 | 1547.59 | 102.68% | 7.75 | 7.97 | 97.21% | 123.11 | 628.16 | 19.60% |
| Level 5, varied | 164.22 | 164.46 | 99.86% | 7.46 | 8.31 | 89.80% | 122.59 | 628.16 | 19.52% |
| Level 5, mixed | 1882.70 | 1576.40 | 119.43% | 7.84 | 8.50 | 92.15% | 122.59 | 628.16 | 19.52% |
| Level 15, varied | 54.42 | 45.78 | 118.86% | 20.94 | 21.88 | 95.71% | 122.59 | 628.16 | 19.52% |
| Level 15, mixed | 257.10 | 251.40 | 102.27% | 21.18 | 21.95 | 96.48% | 122.59 | 628.16 | 19.52% |
| Level 22, varied | 2.40 | 1.55 | 154.71% | 21.70 | 22.27 | 97.42% | 137.16 | 628.16 | 21.84% |
| Level 22, mixed | 45.48 | 28.18 | 161.42% | 22.32 | 22.36 | 99.84% | 137.16 | 628.16 | 21.84% |

All completed trials passed exact-frame and post-warmup allocation checks.
Artifacts: `out/aligned-strategy-control/policy-broad-*`.

Follow-up in progress: limit alignment to double-fast with minimum match 4;
minimum-match-5 presets return to ordinary allocation. This follows the
demonstrated 256 KiB gain rather than assuming it transfers to larger-source
presets. The tables above describe the preceding all-double-fast policy,
not new measurements of this follow-up. The 256 KiB level-3 executable is
byte-for-byte identical to the measured policy build, and GCC units/full
reciprocal acceptance passed (8.42 seconds). Larger-input performance checks
completed. Five alternating 5,000-frame trials for each 1 MiB case give:

| Case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Level 3, varied | 1899.15 | 2359.38 | 80.49% | 5.96 | 6.59 | 90.46% | 123.02 | 628.16 | 19.58% |
| Level 3, mixed | 1543.76 | 1551.83 | 99.48% | 6.24 | 6.72 | 92.91% | 123.02 | 628.16 | 19.58% |
| Level 4, varied | 1795.86 | 2391.05 | 75.11% | 7.49 | 7.86 | 95.38% | 123.02 | 628.16 | 19.58% |
| Level 4, mixed | 1642.04 | 1523.90 | 107.75% | 7.75 | 7.97 | 97.21% | 123.02 | 628.16 | 19.58% |

Changes versus the saved ordinary baseline are +0.14%/-7.81% for level 3
varied/mixed and +1.55%/-0.12% for level 4. All ranges overlap. Restoring
ordinary storage did not reliably restore level-3 mixed throughput, so the
workspace/helper refactor's broader generated-code effects remain unresolved.
All exact-frame and allocation checks pass. Artifacts:
`out/aligned-strategy-control/min4-*`. This remains provisional, not an
all-level performance win.

Rejected experiment: a prototype avoided copying raw literal payloads into the
intermediate compressed-block vector for double-fast. It emits the borrowed
literal span synchronously between the literal header and sequence stream.
The benchmark tables above predate this prototype. Its first five-trial
256 KiB level-3 screen (16,384 frames/trial, same optimized GCC settings)
found no established speedup: balanced baseline1515.86 versus1522.42 MiB/s
(+0.43%, ranges1502.72–1576.43 versus1298.30–1584.59), varied2036.28
versus2041.28 (+0.25%, ranges1988.85–2098.06 versus1993.42–2096.71).
Both ranges overlap. Exact frames and allocation guards pass; larger-input
controls below also failed to establish a benefit. The prototype and its
descriptor-specific tests were removed; the ordinary contiguous block
encoding is restored. These are historical experiment measurements.

| Case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Deferred literals, 256 KiB balanced L3 | 1522.42 | 1926.37 | 79.03% | 4.99 | 4.86 | 102.74% | 122.65 | 628.16 | 19.53% |
| Deferred literals, 256 KiB varied L3 | 2041.28 | 2720.62 | 75.03% | 4.87 | 4.90 | 99.28% | 122.65 | 628.16 | 19.53% |

The 1 MiB controls used five alternating 3,000-frame trials. Baseline/candidate
changes were -0.41%/+0.45% for level3 varied/mixed and +0.66%/-9.42% for
level4. All ranges overlap, but there is no demonstrated speedup to justify
the extra scatter-emission complexity. Exact-frame/allocation checks passed.

| Case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Deferred literals, 1 MiB varied L3 | 1988.50 | 2423.18 | 82.06% | 5.99 | 6.63 | 90.34% | 123.08 | 628.16 | 19.59% |
| Deferred literals, 1 MiB mixed L3 | 1711.74 | 1561.72 | 109.61% | 6.24 | 6.72 | 92.85% | 123.08 | 628.16 | 19.59% |
| Deferred literals, 1 MiB varied L4 | 1961.37 | 2381.55 | 82.36% | 7.50 | 7.83 | 95.76% | 123.08 | 628.16 | 19.59% |
| Deferred literals, 1 MiB mixed L4 | 1599.40 | 1499.19 | 106.68% | 7.71 | 7.99 | 96.58% | 123.08 | 628.16 | 19.59% |

Removing a copy alone was insufficient. A future caller-owned direct-output
path must be evaluated separately; these results do not measure that design.

### Four-byte hash specializations (2026-09-26, retained work reduction)

A fresh profile found that double-fast's compile-time minimum-match-4 hash
still entered the runtime 5/6/7/default switch. An explicit four-byte
specialization removes that repeated dispatch; generated assembly now uses
the constant 32-bit multiply directly. A six-frame, multi-block equivalence
test compares it with runtime hash selection, and GCC units/full reciprocal
acceptance pass. This is not yet an established throughput improvement.

Five alternating 16,384-frame trials, 256 KiB level 3, optimized GCC13:
balanced baseline1487.01/candidate1521.81 MiB/s (+2.34%, overlapping
1384.84–1539.85 / 1489.83–1532.12); varied2095.97/2120.53 (+1.17%,
overlapping1963.52–2129.37 / 2079.79–2142.30). Exact frames and post-warmup
allocation checks pass. ASan/UBSan units and the assertion-enabled repeated-frame
check pass. MSVC Debug units/full reciprocal acceptance also pass (109.01
seconds); that verifies correctness, not Windows throughput. Broader
small-input performance gates remain.

| Case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Level 3, 256 KiB balanced | 1521.81 | 1855.21 | 82.03% | 5.12 | 4.88 | 105.05% | 122.95 | 628.16 | 19.57% |
| Level 3, 256 KiB varied | 2120.53 | 2660.92 | 79.69% | 4.84 | 4.87 | 99.36% | 122.95 | 628.16 | 19.57% |

Artifacts: `out/aligned-strategy-control/short4-*`. The all-level goal remains
unmet; these cases still trail the reference.

The broader small-input audit is complete. Binary hashes show only 8 KiB L3
and 256 KiB L3 change: 8 KiB L2, 64 KiB L2/L3, and 256 KiB L2 are exactly
identical before/after and serve only as timing-noise controls. The completed
8 KiB L3 balanced case (five alternating 2 GiB/trial runs) measures baseline
537.24 versus candidate552.00 MiB/s (+2.75%), with narrowly overlapping
ranges498.06–545.26 / 543.35–563.14. This is encouraging, not conclusive.
The 8 KiB varied control is neutral: baseline1460.82/candidate1460.32 MiB/s
(-0.03%, overlapping1431.45–1477.62 / 1298.10–1481.16).

| Case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Level 3, 8 KiB balanced | 552.00 | 733.91 | 75.21% | 4.12 | 3.89 | 105.93% | 122.95 | 628.16 | 19.57% |
| Level 3, 8 KiB varied | 1460.32 | 1596.17 | 91.49% | 4.12 | 3.85 | 107.11% | 122.95 | 628.16 | 19.57% |

The independent 256 KiB confirmation in this audit (five 2 GiB/trial runs)
measures balanced baseline1505.11/candidate1557.08 MiB/s (+3.45%) and
varied2013.44/2101.33 (+4.37%). Ranges still overlap:1458.51–1543.95 /
1488.35–1571.48 and1951.31–2036.45 /2004.22–2155.85 respectively.
Identical-binary 64 KiB L2 controls moved -1.93% balanced and -2.61% varied,
demonstrating the timing noise floor. Repeated positive 256 KiB medians are
encouraging but do not establish a precise speedup. All checks passed.

| Case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Confirmation, 256 KiB balanced L3 | 1557.08 | 1873.38 | 83.12% | 5.12 | 4.89 | 104.80% | 122.95 | 628.16 | 19.57% |
| Confirmation, 256 KiB varied L3 | 2101.33 | 2602.72 | 80.74% | 4.88 | 4.89 | 99.60% | 122.95 | 628.16 | 19.57% |

A separate fast-parser experiment adds the same missing
compile-time four-byte hash branch while preserving its existing input-load
width. The 8 KiB level-2 preset exercises it; the tables above do not measure
that change. Its first 8 KiB level-2 screen uses five alternating 2 GiB/trial
runs. Balanced baseline684.43/candidate699.35 MiB/s (+2.18%, overlapping
640.76–699.03 /687.64–718.70); varied1671.65/1675.98 (+0.26%, overlapping
1449.45–1688.13 /1661.92–1693.59). GCC equivalence/reciprocal tests and
exact-frame/allocation checks pass. Speedup remains imprecise; hardware-counter
controls below establish reduced work. The fast-parser change also passed
ASan/UBSan and MSVC Debug units/full reciprocal acceptance (107.96 seconds).
Both missing specializations are retained for the verified work reduction;
this is not a claim of a large or universal throughput gain.

| Case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast hash, 8 KiB balanced L2 | 699.35 | 943.40 | 74.13% | 4.08 | 3.89 | 104.92% | 118.44 | 628.16 | 18.85% |
| Fast hash, 8 KiB varied L2 | 1675.98 | 1676.13 | 99.99% | 4.11 | 3.88 | 105.94% | 118.44 | 628.16 | 18.85% |

Five alternating hardware-counter pairs on balanced input (2 GiB per run)
confirm less work: fast instructions40.988B→37.558B (-8.37%), branches
6.015B→4.699B (-21.88%), median cycles12.743B→12.605B (-1.09%);
double-fast instructions17.510B→16.107B (-8.01%), branches2.496B→1.986B
(-20.44%), median cycles5.800B→5.625B (-3.03%). These counters support the
missing-specialization fix, not an 8% throughput claim. Existing throughput
ranges and all-level gaps still apply. Artifacts:
`out/aligned-strategy-control/hash4-*`.

### Rejected FSE-history-copy experiment (2026-09-26)

Skipping history-table/count copies in fast table selection preserved encoded
bytes and allocation behavior, but did not establish a throughput benefit.
Five alternating 2 GiB/trial runs gave baseline/candidate changes of -0.20%
balanced and +1.71% varied for 8 KiB L2, and -1.44%/-1.89% for 256 KiB L3.
All ranges overlap. The extra policy and its policy-specific tests were
removed; full history behavior is restored. These are historical measurements,
not the current implementation's benchmark numbers.

| Case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 KiB balanced L2 | 706.20 | 979.43 | 72.10% | 4.11 | 3.86 | 106.27% | 118.45 | 628.16 | 18.86% |
| 8 KiB varied L2 | 1550.63 | 1708.35 | 90.77% | 4.08 | 3.89 | 104.81% | 118.45 | 628.16 | 18.86% |
| 256 KiB balanced L3 | 1578.91 | 1913.28 | 82.52% | 5.13 | 4.88 | 105.13% | 122.80 | 628.16 | 19.55% |
| 256 KiB varied L3 | 2146.14 | 2678.74 | 80.12% | 4.88 | 4.79 | 101.71% | 122.80 | 628.16 | 19.55% |

Artifacts: `out/aligned-strategy-control/history-*`. Direct in-place FSE-table
construction is a separate future experiment, not measured here.

### In-place FSE construction (2026-09-26, experimental)

The first prototype builds sequence FSE tables into reusable storage but still
clears their full capacity. Five alternating 2 GiB/trial comparisons against
the pre-experiment build show mixed medians: 8 KiB L2 balanced +0.98%, varied
+5.70%; 256 KiB L3 balanced -1.32%, varied -2.08%. Every baseline/candidate
range overlaps, so retention is not justified yet. GCC poisoned/reused-table
tests, reciprocal acceptance and benchmark frame/allocation checks pass.

| Case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Full clear, 8 KiB balanced L2 | 709.88 | 991.42 | 71.60% | 4.11 | 3.87 | 106.16% | 118.48 | 628.16 | 18.86% |
| Full clear, 8 KiB varied L2 | 1719.51 | 1678.08 | 102.47% | 4.12 | 3.85 | 107.01% | 118.48 | 628.16 | 18.86% |
| Full clear, 256 KiB balanced L3 | 1573.03 | 1913.40 | 82.21% | 5.09 | 4.84 | 105.17% | 123.10 | 628.16 | 19.60% |
| Full clear, 256 KiB varied L3 | 2126.20 | 2750.48 | 77.30% | 4.87 | 4.90 | 99.36% | 123.10 | 628.16 | 19.60% |

The four-way follow-up tested writing only active states/transforms, leaving
unused capacity untouched. It did not demonstrate an incremental benefit:
active-only versus full-clear medians changed +2.39%/+0.12% for L2
balanced/varied and -0.94%/-4.08% for L3, all with overlapping ranges.
Active-only clearing was rejected; full clearing is restored.

The full-clear in-place variant in that same five-trial, 2 GiB/run comparison
improved 8 KiB L2 varied throughput **6.82%** versus the original (1593.75 to
1702.37 MiB/s), with disjoint ranges1388.58–1635.66 /1671.72–1735.13.
Other baseline changes were -0.94% L2 balanced, +1.54% L3 balanced, +2.11%
L3 varied, with overlapping ranges. This supports broader validation of the
simpler full-clear builder, not an all-strategy retention decision yet.

| Full-clear confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 KiB balanced L2 | 706.33 | 975.65 | 72.40% | 4.08 | 3.88 | 105.14% | 118.48 | 628.16 | 18.86% |
| 8 KiB varied L2 | 1702.37 | 1650.56 | 103.14% | 4.11 | 3.88 | 105.95% | 118.48 | 628.16 | 18.86% |
| 256 KiB balanced L3 | 1543.97 | 1872.94 | 82.44% | 5.12 | 4.86 | 105.39% | 123.10 | 628.16 | 19.60% |
| 256 KiB varied L3 | 2129.37 | 2601.45 | 81.85% | 4.86 | 4.84 | 100.40% | 123.10 | 628.16 | 19.60% |

Full GCC cross-decoding and benchmark frame/allocation checks passed before
this comparison. Expanded tests for both low-probability-count forms pass,
as do ASan/UBSan and MSVC Debug units/full reciprocal acceptance (109.24
seconds). Larger/deeper strategy controls are now running before retention.
Artifacts: `out/aligned-strategy-control/inplace-*`.

The broader 1 MiB builder audit completed with exact-frame and allocation
checks passing throughout. Levels 1 and 3 used
five alternating 2,000-frame trials, with an isolated pre-builder header
baseline retaining the other optimizations. Baseline/candidate changes are
-1.87%/-5.14% for level1 varied/mixed and +5.30%/+1.18% for level3. All ranges
overlap; level3 varied baseline spans1094.47–1950.94 MiB/s. These results do
not yet support global retention. Exact frames and allocation guards pass.

| Builder audit | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L1 | 2638.31 | 3159.11 | 83.51% | 5.09 | 5.89 | 86.40% | 122.88 | 628.16 | 19.56% |
| 1 MiB mixed L1 | 2078.67 | 1997.73 | 104.05% | 5.44 | 6.11 | 89.00% | 122.88 | 628.16 | 19.56% |
| 1 MiB varied L3 | 1908.41 | 2264.22 | 84.29% | 5.99 | 6.64 | 90.29% | 123.06 | 628.16 | 19.59% |
| 1 MiB mixed L3 | 1633.00 | 1619.30 | 100.85% | 6.25 | 6.77 | 92.33% | 123.06 | 628.16 | 19.59% |

Additional completed controls: level5 varied/mixed changed -1.63%/+0.88%
against the pre-builder baseline; level9 varied changed -1.60%. All three
baseline/candidate ranges overlap (L5 varied178.96–187.57/169.17–182.67,
L5 mixed2082.91–2157.92/1936.53–2200.06, L9 varied149.30–164.74/150.73–158.48
MiB/s). These do not establish a gain or a clear regression. In particular,
varied-text levels5 and9 still trail reference despite the mixed-corpus lead.

| Builder audit continuation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L5 | 179.01 | 187.37 | 95.54% | 7.49 | 8.36 | 89.58% | 122.70 | 628.16 | 19.53% |
| 1 MiB mixed L5 | 2147.50 | 2043.27 | 105.10% | 7.84 | 8.48 | 92.36% | 122.70 | 628.16 | 19.53% |
| 1 MiB varied L9 | 156.61 | 163.14 | 95.99% | 14.96 | 15.90 | 94.10% | 122.70 | 628.16 | 19.53% |
| 1 MiB mixed L9 | 991.28 | 915.02 | 108.33% | 15.25 | 15.99 | 95.36% | 122.70 | 628.16 | 19.53% |
| 1 MiB varied L15 | 55.41 | 48.82 | 113.50% | 20.94 | 21.88 | 95.70% | 122.70 | 628.16 | 19.53% |
| 1 MiB mixed L15 | 320.65 | 293.72 | 109.17% | 21.19 | 21.98 | 96.39% | 122.70 | 628.16 | 19.53% |
| 1 MiB varied L22 | 2.43 | 1.66 | 146.39% | 21.70 | 22.25 | 97.52% | 137.20 | 628.16 | 21.84% |
| 1 MiB mixed L22 | 49.45 | 34.98 | 141.36% | 22.32 | 22.33 | 99.93% | 137.20 | 628.16 | 21.84% |

Level9 mixed finished at -6.33% against the pre-builder baseline1058.22
MiB/s. Ranges1003.64–1111.48/843.61–1088.05 overlap, but this downside
requires a confirmation comparison before global retention. Level15 used
300 frames per trial: varied/mixed changes versus baseline were -1.47%/-0.44%,
with overlapping ranges53.10–58.86/53.13–55.95 and252.18–341.46/303.16–366.64
MiB/s respectively. Level22 used20 frames per trial; varied changed -4.44%
(baseline2.546/candidate2.433 MiB/s, overlapping ranges2.503–2.587/2.395–2.566),
mixed +4.28% (47.422/49.452, overlapping39.029–53.145/40.671–50.916).
The mixed L22 runs are short and noisy; these are not evidence of a builder
speedup. The broader audit does not establish a general win. Saved-binary
confirmation runs for the L1/L9 mixed downside use8,000 frames per trial and
hardware counters, with no source changes. Artifacts:
`out/aligned-strategy-control/builder-*`.

The longer L1 mixed confirmation completed five8,000-frame trials under
`perf stat`. Baseline/candidate throughput2312.84/2266.22 MiB/s (-2.02%),
with overlapping ranges2145.16–2375.64/2155.36–2332.39. Median instructions
57.0741B/57.0711B are essentially unchanged; cycles15.1686B/15.5907B (+2.78%).
The earlier -5.14% timing drop is not reproduced at that magnitude. This
does not yet distinguish code-layout effects from environmental variability.
L9 confirmation also finished: baseline983.31/candidate958.83 MiB/s (-2.49%),
overlapping ranges965.76–1033.01/896.31–984.12. Instructions86.9188B/86.8827B
changed little, while cycles35.7024B/36.6936B rose2.78%. All frame and
allocation checks passed. Given repeated median downside in larger controls
and only localized small-input gains, the global in-place builder was
**reverted**. These tables document the rejected experiment, not current
production performance. Both earlier four-byte hash specializations remain.

| Builder confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB mixed L1 | 2266.22 | 2077.89 | 109.06% | 5.37 | 6.14 | 87.47% | 122.88 | 628.16 | 19.56% |
| 1 MiB mixed L9 | 958.83 | 830.15 | 115.50% | 15.25 | 16.00 | 95.29% | 122.70 | 628.16 | 19.53% |

### Bounded sequence-alphabet scan experiment

Sequence-table selection now experimentally scans only the known LL36/OF29/ML53
alphabet instead of256 entries; counting, clearing, and table construction are
unchanged. Generic and partition-cost callers keep their full alphabet. GCC13
unit and full reciprocal acceptance pass7.14 seconds. Five alternating2GiB
trials per case pass exact-frame and allocation checks. Compared with the
saved pre-experiment binaries, changes were -1.46%/+6.43% for8KiB L2
balanced/varied and +1.53%/-0.77% for256KiB L3. Only L2 varied had disjoint
ranges: baseline1644.84–1754.68 versus candidate1763.80–1827.88 MiB/s.
This supports broader testing, not global retention; three sequence-table
specializations also increase executable size.

| Bounded scan | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 KiB balanced L2 | 705.29 | 1011.25 | 69.74% | 4.12 | 3.90 | 105.61% | 122.99 | 628.16 | 19.58% |
| 8 KiB varied L2 | 1803.10 | 1729.55 | 104.25% | 4.13 | 3.88 | 106.45% | 122.99 | 628.16 | 19.58% |
| 256 KiB balanced L3 | 1679.84 | 1975.28 | 85.04% | 5.13 | 4.85 | 105.72% | 127.44 | 628.16 | 20.29% |
| 256 KiB varied L3 | 2224.44 | 2829.57 | 78.61% | 4.88 | 4.85 | 100.64% | 127.44 | 628.16 | 20.29% |

ASan/UBSan units and an assertion-enabled1MiB L3 worker also pass. Larger
five1,000-frame alternating controls completed for L1/L3: changes versus
baseline were -0.28%/-4.58% for L1 varied/mixed and +0.73%/+0.55% for L3.
All ranges overlap; L1 mixed baseline2328.56–2394.76 versus candidate
2204.58–2398.70 MiB/s warrants further investigation. L3 mixed baseline was
especially noisy (1292.42–1859.02). No global retention decision yet.

| Larger bounded scan | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L1 | 2906.22 | 3397.66 | 85.54% | 5.12 | 5.84 | 87.63% | 127.98 | 628.16 | 20.37% |
| 1 MiB mixed L1 | 2268.13 | 2112.82 | 107.35% | 5.50 | 6.12 | 89.97% | 127.98 | 628.16 | 20.37% |
| 1 MiB varied L3 | 2057.59 | 2480.35 | 82.96% | 6.00 | 6.61 | 90.83% | 127.78 | 628.16 | 20.34% |
| 1 MiB mixed L3 | 1831.24 | 1683.08 | 108.80% | 6.25 | 6.75 | 92.65% | 127.78 | 628.16 | 20.34% |
| 1 MiB varied L9 | 154.05 | 168.06 | 91.66% | 14.99 | 15.93 | 94.09% | 127.30 | 628.16 | 20.27% |
| 1 MiB mixed L9 | 1038.37 | 901.71 | 115.16% | 15.25 | 15.98 | 95.43% | 127.30 | 628.16 | 20.27% |
| 1 MiB varied L15 | 54.71 | 49.03 | 111.60% | 21.00 | 21.88 | 96.00% | 131.22 | 628.16 | 20.89% |
| 1 MiB mixed L15 | 315.73 | 284.95 | 110.80% | 21.25 | 21.98 | 96.66% | 131.22 | 628.16 | 20.89% |

Artifacts: `out/aligned-strategy-control/bounded-scan-*` and `bounded-broad-*`.
The larger audit finished with all frame/allocation checks passing. L9
varied/mixed changes were -3.06%/+0.67%, L15 -4.99%/+12.24%; all ranges
overlap, and L15 mixed is particularly noisy (baseline233.69–317.18,
candidate251.24–330.58 MiB/s). L15 uses150 frames per trial. Binary inspection
confirmed three full table-selection bodies versus one in the baseline.
The alphabet-template variant is not retained globally: a new experiment
passes the bound as an argument to allow the larger routine to remain shared.
The above tables describe the template-bound variant, not the new experiment.
MSVC validation and independent confirmation remain required for retention.

### Shared bounded-scan follow-up

Passing the alphabet bound as an argument restores one table-selection body
in the inspected L2 binary, rather than three. GCC units/full reciprocal
acceptance pass7.30 seconds. Five alternating2GiB trials per case pass all
frame/allocation checks. Changes against the original full-scan baseline:
L2 balanced+0.55%, varied+3.87%; L3 balanced+0.73%, varied-0.31%. All ranges
overlap, including L2 varied1600.64–1723.75/1688.13–1807.99 MiB/s. The code-size
reduction versus the alphabet-template variant is verified; a robust speed
gain is not yet established. Larger comparisons and further gates are pending.

| Shared bounded scan | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 KiB balanced L2 | 724.62 | 974.54 | 74.36% | 4.08 | 3.90 | 104.60% | 118.63 | 628.16 | 18.88% |
| 8 KiB varied L2 | 1755.37 | 1667.44 | 105.27% | 4.07 | 3.90 | 104.40% | 118.63 | 628.16 | 18.88% |
| 256 KiB balanced L3 | 1602.71 | 1922.25 | 83.38% | 5.09 | 4.88 | 104.24% | 122.80 | 628.16 | 19.55% |
| 256 KiB varied L3 | 2187.32 | 2822.94 | 77.48% | 4.87 | 4.90 | 99.36% | 122.80 | 628.16 | 19.55% |

Artifacts: `out/aligned-strategy-control/bounded-shared-*`.

The shared version also passes ASan/UBSan units and the assertion-enabled
1MiB L3 worker. Larger five1,000-frame controls show L1 varied-0.39%,
mixed-3.48%; L3 varied-0.26%, mixed+2.54%. L1 mixed now has disjoint ranges:
baseline2343.24–2394.42 versus candidate2233.94–2339.94 MiB/s. Thus restoring
code sharing did not remove that downside, and this variant is not suitable
for global retention on current evidence. Other ranges overlap; the L1
varied candidate and L3 mixed reference have large low outliers. L9/L15
controls finished without frame or allocation failures. L9 varied/mixed
changes were +1.72%/+0.50%; L15 +0.48%/+13.03%, all with overlapping ranges.
L15 mixed is particularly noisy (baseline345.20–399.81, candidate348.69–474.14
MiB/s). These results do not offset the verified L1 downside: both scan-only
variants have been reverted. Tables in this section are historical experiments,
not the current production implementation.

| Larger shared scan | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L1 | 2835.79 | 3382.56 | 83.84% | 5.12 | 5.89 | 86.94% | 123.04 | 628.16 | 19.59% |
| 1 MiB mixed L1 | 2288.81 | 2140.06 | 106.95% | 5.37 | 6.11 | 87.91% | 123.04 | 628.16 | 19.59% |
| 1 MiB varied L3 | 1992.36 | 2453.27 | 81.21% | 5.96 | 6.62 | 90.08% | 123.03 | 628.16 | 19.59% |
| 1 MiB mixed L3 | 1843.68 | 1734.27 | 106.31% | 6.20 | 6.77 | 91.52% | 123.03 | 628.16 | 19.59% |
| 1 MiB varied L9 | 162.81 | 164.23 | 99.14% | 14.96 | 15.86 | 94.38% | 122.66 | 628.16 | 19.53% |
| 1 MiB mixed L9 | 1125.22 | 887.00 | 126.86% | 15.21 | 15.95 | 95.35% | 122.66 | 628.16 | 19.53% |
| 1 MiB varied L15 | 57.64 | 50.86 | 113.34% | 20.96 | 21.86 | 95.92% | 122.59 | 628.16 | 19.52% |
| 1 MiB mixed L15 | 422.48 | 349.82 | 120.77% | 21.19 | 22.00 | 96.29% | 122.59 | 628.16 | 19.52% |

### Earlier striped sequence counting experiment

A standalone histogram crossover benchmark showed that skewed inputs benefit
from four independent counters much earlier than broadly distributed inputs.
The experimental sequence-table path now switches at128 codes instead of1500;
literal histograms retain1500. Full clearing/scanning and table construction
are unchanged. GCC units/full reciprocal acceptance pass7.35 seconds. Five
alternating2GiB trials per case pass frame/allocation checks. Changes against
baseline: L2 balanced+2.24%, varied+0.31%; L3 balanced-0.16%, varied+0.26%.
All ranges overlap; no retained speedup is established. Longer L2 balanced
confirmation with hardware counters is the next gate.

| Early sequence stripes | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 KiB balanced L2 | 746.24 | 1004.15 | 74.32% | 4.12 | 3.90 | 105.51% | 118.44 | 628.16 | 18.85% |
| 8 KiB varied L2 | 1702.87 | 1696.83 | 100.36% | 4.12 | 3.80 | 108.21% | 118.44 | 628.16 | 18.85% |
| 256 KiB balanced L3 | 1617.80 | 1931.68 | 83.75% | 5.09 | 4.85 | 105.00% | 122.98 | 628.16 | 19.58% |
| 256 KiB varied L3 | 2179.38 | 2777.01 | 78.48% | 4.88 | 4.88 | 100.00% | 122.98 | 628.16 | 19.58% |

Artifacts: `out/aligned-strategy-control/sequence-stripes-*`.

Five alternating4GiB L2 balanced confirmation trials measured715.98 to735.81
MiB/s (+2.77%), still overlapping ranges671.14–728.97/710.78–750.05. Hardware
counters support further testing: median cycles25.2312B to24.4323B (-3.17%),
branches9.3972B to9.0983B (-3.18%), instructions75.1142B to75.0764B (-0.05%).
All frame/allocation checks pass. MSVC Debug units and full reciprocal
acceptance also pass (101.95 seconds total); this is a correctness gate, not
a Windows performance measurement. ASan/UBSan units and the assertion-enabled
1MiB L3 worker also pass. Larger strategy controls remain the next retention
gate; this is not an all-level win.

| Early stripes confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 KiB balanced L2 | 735.81 | 969.77 | 75.87% | 4.11 | 3.87 | 106.26% | 118.44 | 628.16 | 18.85% |

Larger five1,000-frame alternating comparisons have completed at L1/L3.
Baseline/candidate changes are -0.36%/-0.14% for L1 varied/mixed and
-2.54%/+3.97% for L3. All ranges overlap; L3 varied baseline is especially
noisy (1406.83–2094.85 MiB/s, candidate1824.69–2012.01). These neither prove
a broader gain nor reproduce the earlier scan-only L1 regression. L9/L15
also completed with exact frames and allocation guards passing. L9
varied/mixed changes were -1.60%/+2.37%; L15 +1.63%/+8.90%, all overlapping
ranges (L15 mixed remains particularly noisy). Early sequence striping is
retained for the repeated small-input benefit and reduced cycles/branches,
not as a universal speedup. A fresh full22-level matrix has been generated;
its single-run mixed-corpus results are a screening snapshot, not proof
that every level/corpus beats reference.

| Larger early stripes | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L1 | 2857.59 | 3348.86 | 85.33% | 5.12 | 5.88 | 87.11% | 123.04 | 628.16 | 19.59% |
| 1 MiB mixed L1 | 2364.68 | 2152.53 | 109.86% | 5.37 | 6.15 | 87.36% | 123.04 | 628.16 | 19.59% |
| 1 MiB varied L3 | 1897.87 | 2460.27 | 77.14% | 6.00 | 6.63 | 90.46% | 123.02 | 628.16 | 19.58% |
| 1 MiB mixed L3 | 1799.18 | 1704.82 | 105.53% | 6.25 | 6.76 | 92.43% | 123.02 | 628.16 | 19.58% |
| 1 MiB varied L9 | 152.74 | 162.31 | 94.10% | 14.96 | 15.93 | 93.92% | 122.66 | 628.16 | 19.53% |
| 1 MiB mixed L9 | 995.29 | 815.70 | 122.02% | 15.25 | 16.02 | 95.17% | 122.66 | 628.16 | 19.53% |
| 1 MiB varied L15 | 55.23 | 48.55 | 113.77% | 20.93 | 21.86 | 95.75% | 122.59 | 628.16 | 19.52% |
| 1 MiB mixed L15 | 332.25 | 278.55 | 119.28% | 21.19 | 21.96 | 96.50% | 122.59 | 628.16 | 19.52% |

The refreshed200-iteration matrix below completed all22levels with exact
compression parity and verified decoding. Apparent deficits are compression
L3(98.18%),L14(91.65%),L17(96.97%) and decompressionL3(89.93%),L5(96.83%).
Short single runs cannot establish their magnitude. Longer alternating
decompression controls completed and did not reproduce either deficit.
Earlier varied-input deficits also
remain relevant: a favorable mixed-corpus row does not establish the goal.

Five alternating32,768-frame trials per implementation use the same reference
frame and1MiB expected output. L3 C++ range7076.46–7640.15 versus reference
7019.94–7291.69 MiB/s overlaps; L5 ranges7461.14–7878.47/6907.90–7283.31
are disjoint. Decoded output and warmup allocation guards pass. These are
repeat measurements of unchanged code, not a new optimization. Compression
L3/L14/L17 longer controls also completed with exact frames/allocation checks:
five alternating trials with4000/1200/300 frames respectively. C++/reference
ranges overlap in all three cases:1588.63–1770.14/1592.79–1785.22 for L3,
284.71–437.03/241.63–376.92 for L14,52.56–58.47/48.14–62.58 for L17.
The mixed-corpus snapshot deficits were not confirmed. Varied-text L3 remains
a repeated substantial gap and is the next profiling target.

| Compression confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB mixed L3 | 1739.76 | 1694.90 | 102.65% | 6.21 | 6.77 | 91.81% | 123.02 | 628.16 | 19.58% |
| 1 MiB mixed L14 | 351.41 | 324.27 | 108.37% | 21.19 | 22.00 | 96.31% | 122.59 | 628.16 | 19.52% |
| 1 MiB mixed L17 | 55.74 | 55.22 | 100.94% | 21.82 | 21.93 | 99.54% | 136.63 | 628.16 | 21.75% |

| Decompression confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Mixed frame L3 | 7259.76 | 7062.50 | 102.79% | 5.84 | 5.98 | 97.58% | 98.41 | 202.89 | 48.50% |
| Mixed frame L5 | 7656.36 | 7156.12 | 106.99% | 5.84 | 5.98 | 97.58% | 98.41 | 202.89 | 48.50% |

### Huffman word-writer experiment

The fresh varied-L3 profile attributed7.67% of C++ samples to the Huffman
stream writer (reference2.07%; separate-process shares are not speed ratios).
Pairwise32-bit flushing was inconclusive. The new trial appends four symbols,
stores a padded64-bit word, advances complete bytes and retains at most7 bits.
The reservoir holds at most51 bits, and existing workspace padding avoids
new allocations. Expanded scalar bit-packing oracles, GCC reciprocal acceptance
(7.94 seconds), ASan/UBSan units and the assertion-enabled worker pass.

Five alternating4,000-frame1MiB L3 trials measured varied+4.98% versus
baseline2014.54 MiB/s, mixed+3.45% versus1751.84 MiB/s. Ranges overlap:
varied1931.75–2059.30/2043.90–2143.32; mixed1701.88–1785.79/1729.75–1863.02.
All frames/allocation guards pass. Longer hardware-counter confirmation,
MSVC and other-level controls remain required before retention.

| Huffman word trial | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L3 | 2114.77 | 2389.03 | 88.52% | 5.96 | 6.60 | 90.41% | 123.11 | 628.16 | 19.60% |
| 1 MiB mixed L3 | 1812.21 | 1683.71 | 107.63% | 6.21 | 6.78 | 91.70% | 123.11 | 628.16 | 19.60% |

MSVC Debug units/full reciprocal acceptance passed101.96 seconds. Five
alternating8,192-frame varied-L3 confirmation trials measured2051.95 to2139.99
MiB/s (+4.29%). Ranges918.34–2160.30/894.71–2174.94 contain large low
outliers and overlap; the exact throughput effect is uncertain. Hardware
medians are more encouraging: cycles17.6341B to16.8404B (-4.50%),
instructions49.1228B to48.8969B (-0.46%), branches6.6534B to6.2814B (-5.59%),
branch misses60.3879M to45.7739M (-24.20%). All frame/allocation checks pass.
Other-level controls are complete; the retention decision is recorded below.
Completed L1 controls (five alternating1,000-frame trials) show varied-0.20%
and mixed+0.70% versus baseline, with overlapping ranges. Thus the new writer
does not yet show a general L1 gain or regression.
L5 also completed: varied+1.60%, mixed+0.88% versus baseline, both with
overlapping ranges. Varied L5 remains slightly below reference in this run.
L9 completed with varied-0.98% and mixed+5.28%; ranges overlap in both
cases (varied136.18–153.95/146.49–153.39, mixed774.50–1065.71/878.65–1076.69
MiB/s). Neither result establishes a reliable L9 effect. L15's150-frame
controls show varied-2.05% and mixed+7.06%, also overlapping ranges:
56.43–59.13/55.05–59.26 and426.31–454.48/384.58–500.39 MiB/s. The varied
downside is worth watching; no broad speedup is established. L22 completed
five20-frame trials per case: varied-0.76%, mixed-2.26%, both overlapping
ranges. Mixed L22 is short and noisy. Longer L15 varied confirmation with
hardware counters is now complete: five alternating 500-frame trials measured
55.29 to 55.92 MiB/s (+1.14%), with overlapping ranges of 51.11–55.60 and
52.63–57.22. The earlier L15 downside was not reproduced. Median cycles fell
from 39.7319B to 39.0570B (-1.70%).

The word writer is retained for the repeated L3 improvement and reduced cycle
count, with GCC, MSVC, sanitizer, exact-frame and allocation checks passing.
The broader controls do not establish a universal speedup or rule out smaller
regressions; short L22 timings remain noisy. The all-level goal is not met.

| Huffman word confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L3 | 2139.99 | 2396.05 | 89.31% | 6.00 | 6.63 | 90.40% | 123.11 | 628.16 | 19.60% |
| 1 MiB varied L15, long confirmation | 55.92 | 48.58 | 115.11% | 20.96 | 21.88 | 95.82% | 122.85 | 628.16 | 19.56% |

| Huffman word broader controls | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L1 | 2893.84 | 3288.74 | 87.99% | 5.12 | 5.90 | 86.82% | 122.85 | 628.16 | 19.56% |
| 1 MiB mixed L1 | 2318.66 | 2082.40 | 111.35% | 5.45 | 6.12 | 89.02% | 122.85 | 628.16 | 19.56% |
| 1 MiB varied L5 | 177.25 | 180.56 | 98.17% | 7.49 | 8.36 | 89.58% | 122.59 | 628.16 | 19.52% |
| 1 MiB mixed L5 | 2058.38 | 1879.36 | 109.53% | 7.86 | 8.53 | 92.17% | 122.59 | 628.16 | 19.52% |
| 1 MiB varied L9 | 148.58 | 161.65 | 91.91% | 15.00 | 15.87 | 94.53% | 122.66 | 628.16 | 19.53% |
| 1 MiB mixed L9 | 973.13 | 811.42 | 119.93% | 15.25 | 15.99 | 95.36% | 122.66 | 628.16 | 19.53% |
| 1 MiB varied L15 | 56.31 | 48.78 | 115.44% | 20.93 | 21.88 | 95.64% | 122.85 | 628.16 | 19.56% |
| 1 MiB mixed L15 | 470.33 | 365.48 | 128.69% | 21.19 | 21.98 | 96.39% | 122.85 | 628.16 | 19.56% |
| 1 MiB varied L22 | 2.40 | 1.69 | 141.46% | 21.70 | 22.24 | 97.56% | 137.02 | 628.16 | 21.81% |
| 1 MiB mixed L22 | 59.47 | 39.53 | 150.45% | 22.32 | 22.37 | 99.81% | 137.02 | 628.16 | 21.81% |

Artifacts: `out/aligned-strategy-control/huffman-word-*`. The full-matrix
snapshot below now includes the retained word writer and direct-parser policy.

### Isolated Huffman BMI2 diagnostic

A GCC-only include-overlay experiment targeted just `encode_huffman_stream`
with BMI2; disassembly confirmed `shlx`/`shrx` in that routine. Production
headers were not changed. Five alternating 4,000-frame L3 trials against the
retained portable word writer gave varied 2193.54 to 2139.70 MiB/s (-2.45%)
and mixed 1799.88 to 1884.32 MiB/s (+4.69%). Both ranges overlap
(2149.56–2213.25 / 1994.04–2315.70 and 1713.62–1887.25 / 1729.37–1924.10).
All exact-frame and allocation checks passed. This does not demonstrate a
gain on the targeted varied-text deficit, so BMI2 dispatch is not adopted.
Artifacts: `out/aligned-strategy-control/huffman-bmi2-*`.

| BMI2 diagnostic, not retained | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L3 | 2139.70 | 2572.49 | 83.18% | 6.00 | 6.55 | 91.65% | 123.11 | 628.16 | 19.60% |
| 1 MiB mixed L3 | 1884.32 | 1738.25 | 108.40% | 6.24 | 6.77 | 92.21% | 123.11 | 628.16 | 19.60% |

### Independent Huffman pair packing (not retained)

A fresh post-word-writer varied-L3 profile measured 48.77% of C++ samples
in double-fast parsing, 15.84% in sequence-block encoding, and 5.79% in the
Huffman stream writer. Reference shares were 58.86%, 9.87%, and 2.14%,
respectively; these are separate-process sample shares, not direct speed ratios.

An experiment constructed two independent symbol pairs before merging them
with the carried bits. GCC units and full reciprocal acceptance passed (7.24
seconds). Five alternating 4,000-frame trials showed varied +0.35% versus the
retained writer (2160.90 to 2168.55 MiB/s), but mixed -5.62% (1887.47 to
1781.33 MiB/s). Both ranges overlap. All frame/allocation checks passed, but
there is no convincing targeted gain to justify retention. The original writer
was restored and verified byte-for-byte against its saved source.

| Independent-pair diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L3 | 2168.55 | 2502.97 | 86.64% | 6.00 | 6.63 | 90.57% | 123.11 | 628.16 | 19.60% |
| 1 MiB mixed L3 | 1781.33 | 1683.19 | 105.83% | 6.21 | 6.75 | 92.07% | 123.11 | 628.16 | 19.60% |

Artifacts: `out/aligned-strategy-control/word-varied3-*` and
`out/aligned-strategy-control/huffman-independent-*`.

### Deferred double-fast tag loads (not retained)

The double-fast loop loaded both old hash tags before checking a repeat match,
although a successful repeat never uses them. This experiment defers those
loads until the repeat check fails, preserving all table writes and match order.
GCC units/full reciprocal acceptance passed (7.47 seconds). Five alternating
4,000-frame L3 trials gave varied +0.63% (2160.45 to 2173.97 MiB/s) and mixed
+5.36% (1789.93 to 1885.90). Ranges overlap in both cases: varied
2116.84–2166.87 / 2096.96–2231.19; mixed 1709.71–1891.63 / 1728.97–1914.40.
All exact-frame/allocation checks passed. These are provisional observations,
not established speedups. Longer confirmation is recorded below.

| Deferred-tag screen | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L3 | 2173.97 | 2516.40 | 86.39% | 5.95 | 6.64 | 89.54% | 123.11 | 628.16 | 19.60% |
| 1 MiB mixed L3 | 1885.90 | 1715.77 | 109.92% | 6.21 | 6.73 | 92.34% | 123.11 | 628.16 | 19.60% |

Five alternating 8,192-frame confirmation trials measured varied +2.32%
(2147.03 to 2196.76 MiB/s) and mixed +0.40% (1826.14 to 1833.51), still with
overlapping ranges. Varied median cycles fell only 0.28% and instructions
0.38%; mixed cycles fell 3.28% with essentially unchanged instructions.
The first mixed throughput gain was not reproduced. This is insufficiently
consistent evidence to retain the change; the source was restored exactly.

| Deferred-tag confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L3 | 2196.76 | 2539.90 | 86.49% | 6.00 | 6.62 | 90.61% | 123.11 | 628.16 | 19.60% |
| 1 MiB mixed L3 | 1833.51 | 1717.97 | 106.73% | 6.25 | 6.68 | 93.62% | 123.11 | 628.16 | 19.60% |

Artifacts: `out/aligned-strategy-control/deferred-tags-*`.

### Branch-miss attribution and loop-unswitching diagnostic

Hardware branch-miss sampling of the retained writer attributed 67.51% of C++
samples to double-fast parsing and 8.87% to match counting. The reference's
double-fast parser accounted for 8.01% of its much smaller miss sample total
(18,362 C++ samples versus 2,285 reference samples at one sample per 10,000
misses). No samples were reported lost and output frames matched. Many sampled
addresses are non-branch instructions, so sampling skid prevents attribution
to individual checks. WSL rejected branch-stack sampling as unsupported.

An isolated GCC include-overlay disabled loop unswitching only in the parser.
Five alternating 4,000-frame trials measured varied -0.69% and mixed -2.52%
versus the retained build, with overlapping ranges. Both cases added
instructions, branches and branch misses. Exact frames and allocation checks
passed, but the compiler override is not retained; production was unchanged.

| No-unswitch diagnostic | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L3 | 2207.02 | 2552.72 | 86.46% | 6.00 | 6.65 | 90.25% | 123.11 | 628.16 | 19.60% |
| 1 MiB mixed L3 | 1854.35 | 1730.47 | 107.16% | 6.25 | 6.72 | 92.97% | 123.11 | 628.16 | 19.60% |

Artifacts: `out/aligned-strategy-control/branch-varied3-*` and `no-unswitch-*`.

### Direct-candidate double-fast specialization

**Retention decision:** direct parsing is now used throughout GCC13 minMatch5
double-fast frames. This replaces the interim sticky selector described below
and prevents stale-tag transitions without an extra state flag or classifier.
The choice favors closing the measured varied/small-input deficits; it is not
a claim that every corpus improves. The earlier repeated-text/zero-heavy
regressions versus the old classifier remain documented tradeoffs, with those
cases still above reference throughput. Other compilers and minimum-match
specializations retain their prior paths.

An isolated GCC13 minMatch5 parser overlay bypasses byte-tag filtering on all
blocks instead of using the current diversity classifier. Five alternating
4,000-frame L3 trials measured varied 2210.61 to 2372.93 MiB/s (+7.34%), with
disjoint ranges 2093.98–2230.25 / 2308.69–2394.58. Median cycles fell 5.97%
(8.04415B to 7.56366B), instructions 4.82%, and branch misses only 2.06%.
Thus this evidence favors reduced parser work, not elimination of the large
branch-miss gap. Mixed measured -0.60% (1782.57 to 1771.87), with overlapping
ranges 1555.36–1891.50 / 1542.10–1792.56. Exact frames and allocation checks
passed. Production is unchanged; full tests, longer confirmation and broader
corpus/affected-preset controls are required before changing the tag policy.

| Direct-candidate screen | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L3 | 2372.93 | 2579.79 | 91.98% | 6.00 | 6.65 | 90.19% | 122.80 | 628.16 | 19.55% |
| 1 MiB mixed L3 | 1771.87 | 1647.40 | 107.56% | 6.21 | 6.73 | 92.29% | 122.80 | 628.16 | 19.55% |

The overlay's full unit and reciprocal acceptance suites passed. Five
alternating 8,192-frame confirmation trials reproduced the varied gain:
2200.92 to 2329.31 MiB/s (+5.83%), with disjoint ranges
2126.58–2227.12 / 2315.83–2357.79. Median cycles fell 5.96% and instructions
4.82%. Mixed measured -1.75% (1804.03 to 1772.43), with overlapping ranges
1723.68–1903.61 / 1697.32–1818.81. This possible mixed downside remains a
retention concern. Sanitizers and broader corpus/level-4 controls are running;
the classifier has not been changed in production.

| Direct-candidate confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L3 | 2329.31 | 2546.10 | 91.49% | 6.00 | 6.58 | 91.21% | 122.80 | 628.16 | 19.55% |
| 1 MiB mixed L3 | 1772.43 | 1694.74 | 104.58% | 6.25 | 6.72 | 93.02% | 122.80 | 628.16 | 19.55% |

ASan/UBSan units and an assertion-enabled worker passed. Broader controls
(five alternating 2,000-frame trials) found disjoint-range regressions on
repeated text (L3 -11.42%, L4 -9.51%) and zero-heavy input (L3 -14.95%,
L4 -14.48%). Structured input improved about 10.3% at both levels, and varied
L4 improved 12.71%, also with disjoint ranges. Random and mixed ranges overlap.
The unconditional policy is not adopted. A same-executable comparison of
automatic, forced-tagged and direct modes is underway to distinguish policy
effects from code-layout differences before considering a new selector.

That comparison is now complete (five alternating 8,192-frame trials per
corpus, identical executable, all frames matching reference). Automatic /
forced-tagged / direct medians in MiB/s were: varied 2139.26 / 2115.87 /
2343.28; mixed 1758.76 / 1516.08 / 1706.89; repeated text 12511.05 /
12681.97 / 12613.14; structured 5241.10 / 5498.37 / 5705.88; zero-heavy
14714.79 / 15033.00 / 14978.37. The large repeated/zero regressions did not
reproduce with a shared binary, suggesting a code-layout contribution rather
than a general need for tags on those inputs. Varied and structured still
favor direct mode; mixed still favors automatic selection (direct -2.95%,
overlapping ranges). These are diagnostic policy comparisons, not new
reference benchmark rows. No production policy has been changed.

| Direct-candidate broader controls | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| L3 repeated text | 13190.01 | 8578.06 | 153.76% | 6.00 | 6.63 | 90.51% | 122.80 | 628.16 | 19.55% |
| L3 seeded random | 3511.59 | 2660.31 | 132.00% | 6.99 | 6.75 | 103.65% | 122.80 | 628.16 | 19.55% |
| L3 structured | 5913.99 | 4378.33 | 135.07% | 6.00 | 6.59 | 91.05% | 122.80 | 628.16 | 19.55% |
| L3 zero-heavy | 15948.82 | 7398.21 | 215.58% | 5.96 | 6.61 | 90.30% | 122.80 | 628.16 | 19.55% |
| L4 varied | 2369.43 | 2484.58 | 95.37% | 7.50 | 7.79 | 96.24% | 122.80 | 628.16 | 19.55% |
| L4 mixed | 1759.03 | 1564.04 | 112.47% | 7.75 | 7.97 | 97.21% | 122.80 | 628.16 | 19.55% |
| L4 repeated text | 13147.52 | 8653.69 | 151.93% | 7.46 | 7.80 | 95.74% | 122.80 | 628.16 | 19.55% |
| L4 seeded random | 3379.23 | 2533.77 | 133.37% | 8.50 | 8.01 | 106.10% | 122.80 | 628.16 | 19.55% |
| L4 structured | 5761.46 | 4307.15 | 133.76% | 7.46 | 7.84 | 95.17% | 122.80 | 628.16 | 19.55% |
| L4 zero-heavy | 16232.14 | 7474.10 | 217.18% | 7.49 | 7.87 | 95.23% | 122.80 | 628.16 | 19.55% |

Artifacts: `out/aligned-strategy-control/direct-tags-*`.

The expanded transition tests also pass for unconditional direct mode. Small
input controls compared it against the correct sticky selector, using five
alternating 1-GiB trials per case. 64-KiB L3 balanced improved 17.04% and varied
21.52%; 256-KiB L2 varied improved 11.20%, all with disjoint ranges. 256-KiB L2
balanced improved 2.62%, with overlapping ranges. These presets also select
minMatch5 double-fast; smaller minMatch4 presets are unaffected.

| Direct-candidate small controls | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 64 KiB L3 balanced | 1458.65 | 1558.55 | 93.59% | 4.62 | 4.34 | 106.47% | 122.80 | 628.16 | 19.55% |
| 64 KiB L3 varied | 3062.34 | 3209.84 | 95.40% | 4.46 | 4.25 | 105.06% | 122.80 | 628.16 | 19.55% |
| 256 KiB L2 balanced | 2007.76 | 2123.77 | 94.54% | 4.62 | 4.52 | 102.25% | 122.80 | 628.16 | 19.55% |
| 256 KiB L2 varied | 2824.76 | 3546.47 | 79.65% | 4.38 | 4.51 | 97.05% | 122.80 | 628.16 | 19.55% |

### Correctness prerequisite: tag-history transitions

The transition audit found a real match-selection bug in GCC13 minMatch5
double-fast parsing: direct mode updated hash indices without updating their
tags, but automatic selection could later return to tagged mode and reject
valid history using stale tags. A deterministic diverse/text two-block probe
diverged immediately on its second block (one sequence versus 51 in the
always-maintained-tag control).

The interim fix kept direct mode active until frame reset once selected. This avoided
new tag writes, allocations or a table-rebuild pass. The original first-block
classifier initially remained. The subsequent retained direct specialization
above eliminates the switch entirely. The 16-seed reproducer passes. New unit tests exercise
eight alternating blocks and three frame resets, and acceptance tests verify
reference-identical bytes plus reciprocal decoding at levels 3 and 4. The
expanded GCC suite passed in 7.24 seconds, and MSVC Debug units/full acceptance
passed in 108.18 seconds. ASan/UBSan units and the assertion-enabled worker
also passed. Five alternating 4,000-frame L3 controls measured varied -5.15%
versus the prior build (2223.75 to 2109.24 MiB/s, disjoint ranges
2198.51–2255.53 / 2035.19–2168.71) and mixed -3.30% (1806.85 to 1747.26,
overlapping ranges). The incorrect prior transition behavior will not be
restored for speed. Unconditional direct parsing is being tested against this
correct baseline, including affected small-input presets, before choosing a
less costly correct policy.

| Correct sticky-policy control | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB varied L3 | 2109.24 | 2531.27 | 83.33% | 6.00 | 6.52 | 91.97% | 123.11 | 628.16 | 19.60% |
| 1 MiB mixed L3 | 1747.26 | 1679.20 | 104.05% | 6.25 | 6.75 | 92.59% | 123.11 | 628.16 | 19.60% |

### Two-way greedy row insertion (retained for GCC13 minimum-match-5)

Profiling identified row-table insertion as the dominant level-5 varied-input
hotspot. A GCC13-only two-way unroll preserves insertion order, storage and
the lazy-strategy path. Five alternating trials (1,000 varied frames or 4,000
mixed frames) measured retained C++ / candidate / reference medians of
207.84 / 291.25 / 231.39 MiB/s varied and 2218.41 / 2206.94 / 2144.71 mixed.
That is +40.14% varied and -0.52% mixed versus retained C++; trial ranges
overlap, so longer confirmation is required before retention.

Varied median cycles fell 28.37% (21.6759B to 15.5256B) while instructions
fell 1.05% (41.3849B to 40.9524B). Mixed cycles changed +0.62% and instructions
+0.12%. All output frames matched reference and allocation guards passed.
Expanded scalar-comparison tests cover odd/even gaps, lookahead wrap, skip
thresholds, candidate ordering, entire table contents and frame/index resets;
GCC units/all-level acceptance passed (6.97 s). Longer timing and broader
validation are pending. The generated full matrix excludes this candidate.

| Row-unroll candidate | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| L5 varied | 291.25 | 231.39 | 125.87% | 7.46 | 8.33 | 89.55% | 122.59 | 628.16 | 19.52% |
| L5 mixed | 2206.94 | 2144.71 | 102.90% | 7.86 | 8.51 | 92.42% | 122.59 | 628.16 | 19.52% |

Longer confirmation used five alternating trials of 4,000 varied frames or
16,384 mixed frames. Varied throughput improved 33.16% over retained C++
(209.64 to 279.17 MiB/s), with disjoint ranges 203.32–255.45 versus
261.09–295.96. Median cycles fell 24.05% (84.9946B to 64.5566B), with
instructions down 1.05%. Mixed throughput was effectively unchanged (+0.08%),
with overlapping ranges; cycles changed -0.12%, instructions +0.12%.
All frame/allocation checks passed. ASan/UBSan expanded units and assertion-enabled
L5 varied/mixed workers passed, as did Windows units/all-level acceptance
(104.85 s). Other affected preset and corpus controls are now underway before retention.

| Row-unroll long confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| L5 varied | 279.17 | 237.66 | 117.46% | 7.50 | 8.38 | 89.51% | 122.59 | 628.16 | 19.52% |
| L5 mixed | 2222.04 | 2128.41 | 104.40% | 7.87 | 8.54 | 92.18% | 122.59 | 628.16 | 19.52% |

The expanded preset check found a downside at 64-KiB L5 (minimum match 4):
five alternating 2-GiB trials changed balanced throughput -1.18% and varied
throughput -11.77% versus retained C++. Varied ranges overlap
(382.45–448.43 / 391.48–404.47 MiB/s), but this is a retention risk.
Both frames match reference. The 256-KiB L5 unchanged-path control binaries
are byte-identical. At 256-KiB L4 (minimum match 5), varied median throughput
improved 35.72% (306.05 to 415.37 MiB/s), while balanced throughput changed
-4.07% (2091.57 to 2006.40). Both ranges overlap; the balanced slowdown remains
a tradeoff to assess. All broader trials have completed. The unroll
is deliberately not enabled across all greedy specializations.
The byte-identical 256-KiB L5 control measured -0.40% balanced and +1.27%
varied between its two labels; these differences are measurement variation,
not optimization gains.

| Row-unroll small-preset check | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 64-KiB L5 balanced | 1053.87 | 1036.65 | 101.66% | 4.74 | 4.52 | 104.93% | 118.58 | 628.16 | 18.88% |
| 64-KiB L5 varied | 395.23 | 385.26 | 102.59% | 4.74 | 4.70 | 100.91% | 118.58 | 628.16 | 18.88% |
| 256-KiB L4 balanced | 2006.40 | 1983.39 | 101.16% | 5.00 | 5.15 | 96.97% | 122.59 | 628.16 | 19.52% |
| 256-KiB L4 varied | 415.37 | 375.64 | 110.58% | 4.86 | 4.98 | 97.65% | 122.59 | 628.16 | 19.52% |
| 256-KiB L5 balanced, unchanged control | 1938.84 | 1938.57 | 100.01% | 5.62 | 5.76 | 97.63% | 122.67 | 628.16 | 19.53% |
| 256-KiB L5 varied, unchanged control | 278.20 | 280.05 | 99.34% | 5.50 | 5.60 | 98.12% | 122.67 | 628.16 | 19.53% |

The remaining five-trial 1-MiB L5 controls passed exact-frame and allocation
checks. Candidate changes versus pre-unroll C++ were +1.25% repeated text,
+4.90% seeded random, +6.56% structured, and +0.33% zero-heavy. All baseline
and candidate timing ranges overlap; these controls do not establish precise
speedups. Source scope is now restricted to minimum-match-5 to exclude the
minimum-match-4 downside above. GCC13 Release binaries are byte-identical to
the measured candidates at 256-KiB L4 and 1-MiB L5, and to the pre-unroll
baseline at 64-KiB L5. The scoped unit/all-level cross-decoding suite passed
(7.51 s). Final-scope ASan/UBSan unit and worker checks also passed. The
generated 22-level compression/decompression matrix below now includes this
scoped optimization (200 iterations, optimized GCC13, profiling disabled).
This single mixed-corpus snapshot has deficits at compression levels 2 and 16
and decompression level 18; longer alternating confirmation is needed, and
the all-level objective is not complete.

Longer alternating confirmation uses the same saved optimized binaries, not
a new source change. Five 16,384-frame L2 compression trials yielded C++
2552.88–2809.38 MiB/s versus reference 2460.90–2594.70, with the medians
below. The ranges overlap slightly; the short snapshot's L2 deficit did not
persist. Five 1,000-frame L16 compression trials still put the C++ median
2.20% behind reference, with broad overlapping ranges 76.79–90.33 versus
77.10–94.54 MiB/s. Five 131,072-frame L18 decompression trials gave
13582.22 versus 12973.02 MiB/s medians, with overlapping ranges
12084.65–14289.94 versus 12013.76–13187.61. Its short-snapshot deficit
also did not persist. All confirmation frame/decode/allocation checks passed.

| Mixed-corpus confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| L2 compression | 2793.42 | 2530.80 | 110.38% | 5.62 | 6.25 | 89.94% | 122.27 | 628.16 | 19.47% |
| L16 compression | 83.66 | 85.54 | 97.80% | 21.45 | 21.86 | 98.12% | 140.70 | 628.16 | 22.40% |
| L18 decompression | 13582.22 | 12973.02 | 104.70% | 5.84 | 5.98 | 97.65% | 98.41 | 202.89 | 48.50% |

| Row-unroll 1-MiB controls | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| L5 repeated text | 7010.49 | 5199.88 | 134.82% | 7.46 | 8.39 | 88.83% | 122.59 | 628.16 | 19.52% |
| L5 seeded random | 2442.86 | 1777.42 | 137.44% | 8.49 | 8.41 | 100.93% | 122.59 | 628.16 | 19.52% |
| L5 structured | 3734.54 | 3169.48 | 117.83% | 7.49 | 8.36 | 89.54% | 122.59 | 628.16 | 19.52% |
| L5 zero-heavy | 7978.39 | 4750.91 | 167.93% | 7.49 | 8.37 | 89.50% | 122.59 | 628.16 | 19.52% |

### Exact adjacent tree-comparison cache (experimental)

An isolated GCC13 Release candidate caches 128 prior comparison results in
1536 bytes of fixed per-instance storage, cleared at each optimal-parser block.
Within that block, advancing both compared strings by one byte turns a positive
exact match length into that length minus one. A matching cache key therefore
avoids recounting bytes; normal tree decisions and skip-distance updates remain.
Production headers are unchanged.

Five alternating CPU-pinned 100-frame L16 varied-input trials measured
7.8680 MiB/s for retained C++ versus 8.9642 for the candidate (+13.93%).
Ranges were 7.6965–7.8859 versus 8.8483–9.0916; reference measured
8.3672–8.8305. Median cycles fell from 57.8034B to 50.0903B (-13.34%),
although instructions rose from 160.3992B to 173.5967B (+8.23%).
All six initial corpus frame checks and completed timing-frame/allocation
guards passed. Five alternating 1,000-frame mixed-input trials changed the
median from 91.21 to 87.38 MiB/s (-4.20%), with wide overlapping ranges
82.48–92.48 versus 75.69–97.09. Median cycles rose 4.56%, while instructions
rose only 0.08%. This possible regression needs longer confirmation; the
varied-input gain is not sufficient for retention. Sanitizer, broader
level/corpus and final implementation checks also remain.
RSS includes runtime noise: this optimization adds fixed cache storage, not
a memory reduction.

Longer confirmation (five alternating 4,000-frame mixed trials using the same
binaries) changed the median from 85.95 to 85.00 MiB/s (-1.11%), with overlapping
ranges 80.21–87.79 versus 81.31–90.87. Reference measured 86.90–94.58.
Candidate cycles increased 1.57% and instructions 0.08%. This narrows, but
does not dismiss, the mixed-input risk; neither C++ median beat reference here.
All confirmation frame/allocation checks passed. The cache remains experimental.

| Exact-cache candidate | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| L16 varied | 8.96 | 8.80 | 101.81% | 21.20 | 21.75 | 97.45% | 140.85 | 628.16 | 22.42% |
| L16 mixed | 87.38 | 88.63 | 98.59% | 21.45 | 21.82 | 98.30% | 140.85 | 628.16 | 22.42% |
| L16 mixed, long confirmation | 85.00 | 91.21 | 93.19% | 21.45 | 21.85 | 98.19% | 140.85 | 628.16 | 22.42% |

A separate candidate moves the cache to the end of the matcher object, preserving
the original fields' offsets. Its five 100-frame varied trials measured
7.7719 MiB/s baseline versus 8.9252 candidate (+14.84%), with disjoint ranges
7.7077–7.8734 versus 8.6220–8.9549. Reference ranged 8.3545–8.8000.
This repeats the baseline improvement, but is not a direct comparison with the
earlier cache placement. Five 1,000-frame mixed trials measured 84.80 baseline
versus 93.24 candidate (+9.95%), with overlapping ranges 81.94–91.01 versus
86.94–96.91. Median cycles fell 8.53%, while instructions rose 0.08%.
All frame/allocation checks passed. These results justify safety and broader
level testing, not production retention or an all-level performance claim.

Expanded reset/streaming validation found a new encoding-parity gap on a
deterministic patterned-and-mutated L16 input (seed 3 of the new reuse test).
The pre-fix baseline and cache candidate emitted the same 22,413-byte frame,
while reference emits 23,613 bytes; the first difference is at byte 2,873.
Both cross-decodes recover the original input. This is an existing encoding
decision mismatch, not an observed cache regression or data corruption.
The baseline now matches that 23,613-byte reference frame after enabling
reference-equivalent optimal pre-splitting and unifying repeat/tree candidate
collection. Levels 16–18 pass the new pre-split acceptance fixture; level 16
also passes all 12 reuse-test seeds with both one-shot and irregular streaming
input. Focused unit tests verify sufficient-repeat early returns and skipped
regions. A second fix applies the reference's strategy-specific minimum gain
to the Huffman payload, excluding section headers; this resolves the remaining
pre-split parity failures at levels 19–22. GCC unit and full acceptance tests
pass, including a focused two-byte-saving Huffman regression. The level-3
sequence diagnostic still differs, but its final frames match and it is not
an acceptance failure. Final ASan/UBSan unit and level-16 reuse checks passed;
MSVC unit and full acceptance tests also passed (258.42 seconds total).
The cache has not been integrated or revalidated
against these fixes; the performance numbers below describe pre-fix binaries.

| Tail-placed exact-cache candidate | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| L16 varied | 8.93 | 8.76 | 101.93% | 21.20 | 21.69 | 97.75% | 140.85 | 628.16 | 22.42% |
| L16 mixed | 93.24 | 86.15 | 108.23% | 21.45 | 21.84 | 98.21% | 140.85 | 628.16 | 22.42% |

### Linear canonical Huffman construction (rejected global experiment)

New diagnostic subphase timers put Huffman/literal encoding at about 43% of
entropy time on the varied 256-KiB L2 and 1-MiB L3 cases, versus 15–16% for
literal gathering and sequence-code conversion. These instrumented measurements
guide optimization; they are not headline throughput measurements.

The candidate replaces repeated alphabet scans during canonical-code assignment
with code-length counts, rank starting values, and one symbol-order pass, as in
the reference. GCC units and all-level reciprocal acceptance passed (6.98 s).
Five alternating uninstrumented trials per case produced the medians below;
small cases use 16,384 frames/trial and large cases 4,000. All output frames
matched reference and post-warmup allocation guards passed.

Versus retained C++, varied medians improved 3.04% at L2 and 2.22% at L3.
Balanced L2 changed -0.29%; mixed L3 fell 4.87%. All before/after ranges overlap
(L2 varied only narrowly), prompting confirmation, especially of
the mixed-input downside. The generated full matrix does not include this
experimental change. Memory is peak RSS; code is whole worker executable size.

| Canonical-rank candidate | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 256-KiB L2 balanced | 2022.90 | 2125.59 | 95.17% | 4.63 | 4.47 | 103.58% | 122.80 | 628.16 | 19.55% |
| 256-KiB L2 varied | 2899.61 | 3663.48 | 79.15% | 4.33 | 4.49 | 96.43% | 122.80 | 628.16 | 19.55% |
| 1-MiB L3 mixed | 1783.03 | 1759.33 | 101.35% | 6.26 | 6.73 | 93.03% | 122.80 | 628.16 | 19.55% |
| 1-MiB L3 varied | 2374.77 | 2516.16 | 94.38% | 6.01 | 6.63 | 90.58% | 122.80 | 628.16 | 19.55% |

Longer confirmation used five alternating trials processing 16 GiB per worker
per trial (65,536 small frames or 16,384 large frames). Varied gains repeated:
L2 +2.84%, L3 +1.60%; mixed L3 changed -1.36%. All ranges still overlap.
Median instructions fell 2.02%/1.97% on varied L2/L3; cycles fell 2.88%/0.93%.
Mixed L3 instructions were almost unchanged (-0.06%), with cycles +0.69%.
These support a modest varied-workload benefit, not universal improvement;
Windows unit/all-level acceptance tests subsequently passed (105.25 s), as did
ASan/UBSan unit tests and a reference-equal varied-input compression run.
Broader workload validation and the final rejection are recorded below.

| Canonical-rank long confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 256-KiB L2 varied | 2918.55 | 3530.21 | 82.67% | 4.36 | 4.53 | 96.29% | 122.80 | 628.16 | 19.55% |
| 1-MiB L3 mixed | 1781.99 | 1733.99 | 102.77% | 6.21 | 6.68 | 92.99% | 122.80 | 628.16 | 19.55% |
| 1-MiB L3 varied | 2343.30 | 2522.28 | 92.90% | 6.00 | 6.64 | 90.41% | 122.80 | 628.16 | 19.55% |

The broader retention check has completed levels 1, 4, 5, 9, 15, and 22 (five alternating
trials per case: 4,000 frames through L9, 500 at L15, and 30 varied/400 mixed
at L22). L1/L4 varied medians improved 3.93% and 1.33%; mixed
medians improved 0.37% and 0.97%. All before/after ranges overlap. The L4 varied
candidate is close to reference in this run, but its 99.78% ratio is not a win.
Level 5 varied regressed 5.34% in median (230.74 to 218.42 MiB/s), with wide
overlapping ranges (206.58–240.39 baseline, 201.29–250.06 candidate); mixed
changed -0.20%. This prompted targeted downside confirmation rather than
global retention. Level 9 varied improved 2.40% in median, while mixed
changed -1.26%; both before/after ranges overlap. The varied case remains
below reference (96.10%), despite the improvement over retained C++.
Level 15 changed -1.63% varied and -5.48% mixed, with overlapping ranges;
both remain faster than reference, but the regressions prompted the
confirmation below. Level 22 changed +0.02% varied and +4.57%
mixed, with overlapping ranges. All broader-run frame/allocation checks passed.

| Canonical-rank broad check | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| L1 varied | 3001.14 | 3433.78 | 87.40% | 5.12 | 5.90 | 86.76% | 122.85 | 628.16 | 19.56% |
| L1 mixed | 2327.51 | 2124.39 | 109.56% | 5.50 | 6.11 | 89.90% | 122.85 | 628.16 | 19.56% |
| L4 varied | 2473.52 | 2478.99 | 99.78% | 7.50 | 7.86 | 95.43% | 122.80 | 628.16 | 19.55% |
| L4 mixed | 1911.95 | 1653.72 | 115.61% | 7.75 | 8.01 | 96.73% | 122.80 | 628.16 | 19.55% |
| L5 varied | 218.42 | 252.62 | 86.46% | 7.49 | 8.36 | 89.62% | 122.67 | 628.16 | 19.53% |
| L5 mixed | 2230.03 | 2112.83 | 105.55% | 7.84 | 8.49 | 92.31% | 122.67 | 628.16 | 19.53% |
| L9 varied | 167.45 | 174.24 | 96.10% | 14.97 | 15.89 | 94.22% | 122.75 | 628.16 | 19.54% |
| L9 mixed | 1128.40 | 995.18 | 113.39% | 15.25 | 16.02 | 95.15% | 122.75 | 628.16 | 19.54% |
| L15 varied | 58.81 | 51.57 | 114.03% | 20.94 | 21.89 | 95.66% | 122.66 | 628.16 | 19.53% |
| L15 mixed | 449.83 | 416.94 | 107.89% | 21.19 | 21.99 | 96.34% | 122.66 | 628.16 | 19.53% |
| L22 varied | 2.61 | 1.76 | 148.45% | 21.70 | 22.25 | 97.54% | 137.02 | 628.16 | 21.81% |
| L22 mixed | 70.93 | 56.59 | 125.35% | 22.29 | 22.38 | 99.60% | 137.02 | 628.16 | 21.81% |

The targeted L5 varied confirmation reproduced the downside: five alternating
4,000-frame trials measured retained C++ 231.05 versus candidate 202.47 MiB/s
(-12.37%). Ranges overlap (202.28–238.26 / 190.05–216.91), but the repeated
negative result and median cycles increasing 12.99% (77.8722B to 87.9885B)
argue against global retention. Instructions decreased slightly, 0.27%
(165.4101B to 164.9642B); fewer source-level operations did not improve total
execution. This does not establish a particular cache or compiler-layout cause.
All frame/allocation checks passed. Longer L15 mixed confirmation also reproduced
the downside: 466.36 to 439.21 MiB/s (-5.82%), with overlapping ranges
409.43–484.46 / 428.98–472.86. Median cycles increased 5.05%
(39.5700B to 41.5670B), despite instructions changing -0.03%.
The global candidate was therefore reverted; the header matches its saved
pre-experiment snapshot. The new scalar-oracle tests and optional diagnostic
timers remain. Restored GCC units/all-level acceptance passed (7.32 s).
All tables in this section describe the rejected candidate, not retained code.

| Canonical-rank downside confirmation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| L5 varied | 202.47 | 258.47 | 78.33% | 7.49 | 8.38 | 89.41% | 122.67 | 628.16 | 19.53% |
| L15 mixed | 439.21 | 397.25 | 110.56% | 21.24 | 22.00 | 96.57% | 122.66 | 628.16 | 19.53% |

### Unused direct-parser tag storage (rejected experiment)

The direct specialization no longer needs its two tag buffers. An experiment
shares one compile-time policy between parsing and allocation, leaving tagged
specializations unchanged. New tests verify allocation/deallocation counts,
reset reuse and index-wrap behavior; GCC units/full acceptance pass (7.85 s).

Five alternating 4,000-frame trials show lower peak RSS: L3 varied 6.00 to
5.71 MiB, mixed 6.25 to 5.96; L4 varied 7.50 to 6.96, mixed 7.74 to 7.25.
These are measured RSS changes, not exact buffer-size accounting. Throughput
changes versus the retained allocation policy were L3 varied +4.64%, mixed
-6.02%, L4 varied +0.21%, mixed -1.62%. Only the L3 mixed ranges are disjoint
(1850.65–1909.81 versus 1763.99–1829.07 MiB/s); the other ranges overlap.
All frame/allocation checks pass. Longer L3 mixed counter-based confirmation
did not reproduce the slowdown: five alternating 8,192-frame trials measured
1769.95 to 1814.43 MiB/s (+2.51%), with overlapping ranges
1629.38–1856.13 / 1589.04–1859.65. Median cycles fell 0.92%
(20.6868B to 20.4962B), with essentially unchanged instructions. Generic cache
misses increased about 7%, so these counters do not support a simple cache-miss
explanation for the initial slowdown. Median RSS remained lower (6.24 to
6.00 MiB). Subsequent Windows, sanitizer and small-preset results are recorded
below; no reliable throughput gain is claimed. The generated full matrix below predates this
allocation experiment and includes the retained direct-parser policy.

| Tag-storage experiment | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| L3 varied | 2361.65 | 2543.06 | 92.87% | 5.71 | 6.60 | 86.57% | 122.80 | 628.16 | 19.55% |
| L3 mixed | 1782.10 | 1762.56 | 101.11% | 5.96 | 6.77 | 88.11% | 122.80 | 628.16 | 19.55% |
| L4 varied | 2412.69 | 2518.62 | 95.79% | 6.96 | 7.84 | 88.79% | 122.80 | 628.16 | 19.55% |
| L4 mixed | 1735.20 | 1629.14 | 106.51% | 7.25 | 8.00 | 90.58% | 122.80 | 628.16 | 19.55% |
| L3 mixed, long confirmation | 1814.43 | 1757.25 | 103.25% | 6.00 | 6.74 | 88.99% | 122.80 | 628.16 | 19.55% |

Windows units/full acceptance passed (106.29 s), as did ASan/UBSan checks.
Small-input controls nevertheless found disjoint-range balanced-input losses:
64-KiB L3 -8.85% (1518.83 to 1384.35 MiB/s), 256-KiB L2 -9.38%
(2018.73 to 1829.39). Varied medians changed -0.26% and -0.80%, with
overlapping ranges. The allocation removal is therefore not retained as-is.
A completed follow-up combined it with aligned literal storage. Five alternating
four-way trials compared the retained baseline, allocation removal alone,
removal plus alignment, and reference. Alignment still lost 8.23% on 64-KiB
L3 balanced input (1470.84 to 1349.76 MiB/s, disjoint ranges). The 256-KiB
L2 balanced median improved 9.51% versus baseline, but ranges overlapped;
its varied median improved only 1.16%. At 1 MiB, L3 mixed changed +0.57%
and varied -0.43%, both overlapping. This does not establish a general speed
benefit or a causal cache-layout explanation. Both experimental source changes
were reverted; the retained direct parser and allocation/reuse tests remain.

The following are experimental medians, not the retained implementation's
benchmark results. Small cases process 1 GiB per trial; 1-MiB cases use
4,000 frames per trial. Code size is whole worker executable size; memory is
peak RSS. Saved frames from all six cases match reference output.

| Rejected removal + alignment | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 64-KiB L3 balanced | 1349.76 | 1536.14 | 87.87% | 4.50 | 4.37 | 102.95% | 122.80 | 628.16 | 19.55% |
| 64-KiB L3 varied | 2908.04 | 3109.23 | 93.53% | 4.50 | 4.37 | 103.04% | 122.80 | 628.16 | 19.55% |
| 256-KiB L2 balanced | 2053.63 | 2030.76 | 101.13% | 4.62 | 4.49 | 102.87% | 122.80 | 628.16 | 19.55% |
| 256-KiB L2 varied | 2761.25 | 3349.60 | 82.44% | 4.37 | 4.47 | 97.81% | 122.80 | 628.16 | 19.55% |
| 1-MiB L3 mixed | 1817.62 | 1661.25 | 109.41% | 5.96 | 6.73 | 88.51% | 122.80 | 628.16 | 19.55% |
| 1-MiB L3 varied | 2277.71 | 2480.36 | 91.83% | 5.71 | 6.65 | 85.91% | 122.80 | 628.16 | 19.55% |

| Tag-storage small controls | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 64 KiB L3 balanced | 1384.35 | 1605.69 | 86.22% | 4.50 | 4.32 | 104.25% | 122.80 | 628.16 | 19.55% |
| 64 KiB L3 varied | 3064.12 | 3228.02 | 94.92% | 4.49 | 4.35 | 103.32% | 122.80 | 628.16 | 19.55% |
| 256 KiB L2 balanced | 1829.39 | 2120.31 | 86.28% | 4.59 | 4.52 | 101.64% | 122.80 | 628.16 | 19.55% |
| 256 KiB L2 varied | 2811.61 | 3423.98 | 82.12% | 4.37 | 4.50 | 97.14% | 122.80 | 628.16 | 19.55% |

<!-- SPH_ZSTDPP_EVAL_BEGIN -->

## Current evaluation

Generated by `zstdpp_eval` using **GNU 13.3.0**, Release, 6 alternating trials of 2000 timed iterations each, and a deterministic 1048576-byte mixed corpus. **Key: `C++` is sph-zstd++; `Reference` is reference zstd.** Code size is the complete worker executable; memory is peak resident set size, so both intentionally include runtime overhead. Each percentage is C++ / Reference * 100, computed before display rounding. Higher is better for throughput; lower is better for memory and code size.

Throughput is derived from median elapsed time; memory is the median of process peak RSS. For even trial counts, central values are averaged with integer rounding down. Each trial alternates which implementation runs first, for both operations. All measurements are preserved in `trials.csv` and per-trial worker CSVs; `metrics.csv` contains aggregates.

Compression workers receive identical input. Decompression workers both consume the same reference-zstd-generated frame at each level, so their throughput and memory results use identical compressed bytes.

### Compression

| Level | C++ size (KiB) | Reference size (KiB) | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 258.38 | 258.38 | 2235.99 | 2069.11 | 108.07% | 5.50 | 6.12 | 89.80% | 126.71 | 628.16 | 20.17% |
| 2 | 273.71 | 273.71 | 2685.56 | 2454.17 | 109.43% | 5.62 | 6.27 | 89.60% | 122.51 | 628.16 | 19.50% |
| 3 | 273.66 | 273.66 | 1792.78 | 1685.67 | 106.35% | 6.21 | 6.73 | 92.28% | 126.94 | 628.16 | 20.21% |
| 4 | 273.66 | 273.66 | 1733.49 | 1607.10 | 107.86% | 7.75 | 7.99 | 97.04% | 126.94 | 628.16 | 20.21% |
| 5 | 273.62 | 273.62 | 1947.92 | 1819.61 | 107.05% | 7.87 | 8.54 | 92.22% | 126.66 | 628.16 | 20.16% |
| 6 | 259.00 | 259.00 | 1756.84 | 1526.54 | 115.09% | 7.75 | 8.50 | 91.16% | 122.73 | 628.16 | 19.54% |
| 7 | 259.00 | 259.00 | 1480.05 | 1378.78 | 107.34% | 10.25 | 11.01 | 93.07% | 122.73 | 628.16 | 19.54% |
| 8 | 259.06 | 259.06 | 1272.46 | 1113.55 | 114.27% | 10.25 | 11.00 | 93.21% | 122.73 | 628.16 | 19.54% |
| 9 | 259.06 | 259.06 | 1072.34 | 926.56 | 115.73% | 15.23 | 15.97 | 95.33% | 122.73 | 628.16 | 19.54% |
| 10 | 259.06 | 259.06 | 977.99 | 855.99 | 114.25% | 15.24 | 15.99 | 95.36% | 122.74 | 628.16 | 19.54% |
| 11 | 259.06 | 259.06 | 805.27 | 711.11 | 113.24% | 15.25 | 15.97 | 95.51% | 122.74 | 628.16 | 19.54% |
| 12 | 259.06 | 259.06 | 836.76 | 708.16 | 118.16% | 15.24 | 15.99 | 95.33% | 122.74 | 628.16 | 19.54% |
| 13 | 259.06 | 259.06 | 551.18 | 470.63 | 117.12% | 21.19 | 21.99 | 96.38% | 126.65 | 628.16 | 20.16% |
| 14 | 259.06 | 259.06 | 541.87 | 492.76 | 109.97% | 21.19 | 21.96 | 96.50% | 126.65 | 628.16 | 20.16% |
| 15 | 259.06 | 259.06 | 422.93 | 371.95 | 113.71% | 21.21 | 21.97 | 96.52% | 126.65 | 628.16 | 20.16% |
| 16 | 257.48 | 257.48 | 61.73 | 57.72 | 106.95% | 21.46 | 21.87 | 98.13% | 144.44 | 628.16 | 22.99% |
| 17 | 257.48 | 257.48 | 68.79 | 78.72 | 87.38% | 21.85 | 21.89 | 99.82% | 144.44 | 628.16 | 22.99% |
| 18 | 260.60 | 260.60 | 49.91 | 40.02 | 124.71% | 22.31 | 22.37 | 99.76% | 140.65 | 628.16 | 22.39% |
| 19 | 260.60 | 260.60 | 74.92 | 62.59 | 119.71% | 22.33 | 22.36 | 99.86% | 140.65 | 628.16 | 22.39% |
| 20 | 260.60 | 260.60 | 63.12 | 43.81 | 144.10% | 22.33 | 22.37 | 99.86% | 140.65 | 628.16 | 22.39% |
| 21 | 260.60 | 260.60 | 74.70 | 61.95 | 120.58% | 22.33 | 22.39 | 99.73% | 140.65 | 628.16 | 22.39% |
| 22 | 260.60 | 260.60 | 60.78 | 42.64 | 142.53% | 22.33 | 22.35 | 99.90% | 140.65 | 628.16 | 22.39% |

### Decompression

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4466.17 | 4026.99 | 110.91% | 5.84 | 5.97 | 97.81% | 98.41 | 202.89 | 48.50% |
| 2 | 8264.63 | 7061.77 | 117.03% | 5.84 | 5.99 | 97.55% | 98.41 | 202.89 | 48.50% |
| 3 | 7917.71 | 7090.53 | 111.67% | 5.84 | 5.98 | 97.65% | 98.41 | 202.89 | 48.50% |
| 4 | 7830.56 | 7073.17 | 110.71% | 5.84 | 5.98 | 97.55% | 98.41 | 202.89 | 48.50% |
| 5 | 8311.36 | 7120.41 | 116.73% | 5.83 | 5.98 | 97.45% | 98.41 | 202.89 | 48.50% |
| 6 | 26288.25 | 15972.76 | 164.58% | 5.84 | 5.97 | 97.84% | 98.41 | 202.89 | 48.50% |
| 7 | 26187.78 | 16178.34 | 161.87% | 5.84 | 5.86 | 99.60% | 98.41 | 202.89 | 48.50% |
| 8 | 26951.57 | 16293.20 | 165.42% | 5.84 | 5.86 | 99.73% | 98.41 | 202.89 | 48.50% |
| 9 | 26649.71 | 16317.09 | 163.32% | 5.83 | 5.90 | 98.87% | 98.41 | 202.89 | 48.50% |
| 10 | 26108.52 | 16411.20 | 159.09% | 5.84 | 5.86 | 99.53% | 98.41 | 202.89 | 48.50% |
| 11 | 26013.31 | 16339.17 | 159.21% | 5.83 | 5.92 | 98.45% | 98.41 | 202.89 | 48.50% |
| 12 | 26322.66 | 16357.92 | 160.92% | 5.84 | 5.96 | 97.94% | 98.41 | 202.89 | 48.50% |
| 13 | 27117.81 | 16855.48 | 160.88% | 5.84 | 5.98 | 97.62% | 98.41 | 202.89 | 48.50% |
| 14 | 26939.27 | 16688.64 | 161.42% | 5.84 | 5.91 | 98.84% | 98.41 | 202.89 | 48.50% |
| 15 | 26923.67 | 16632.37 | 161.88% | 5.82 | 5.86 | 99.33% | 98.41 | 202.89 | 48.50% |
| 16 | 19229.41 | 15800.42 | 121.70% | 5.83 | 5.85 | 99.73% | 98.41 | 202.89 | 48.50% |
| 17 | 19424.70 | 16335.08 | 118.91% | 5.82 | 5.93 | 98.22% | 98.41 | 202.89 | 48.50% |
| 18 | 14078.33 | 12741.34 | 110.49% | 5.84 | 5.97 | 97.87% | 98.41 | 202.89 | 48.50% |
| 19 | 14830.96 | 13311.54 | 111.41% | 5.84 | 5.98 | 97.62% | 98.41 | 202.89 | 48.50% |
| 20 | 14635.63 | 13018.80 | 112.42% | 5.84 | 5.96 | 97.90% | 98.41 | 202.89 | 48.50% |
| 21 | 14954.24 | 13097.07 | 114.18% | 5.84 | 5.98 | 97.55% | 98.41 | 202.89 | 48.50% |
| 22 | 14204.18 | 12982.08 | 109.41% | 5.84 | 5.95 | 98.10% | 98.41 | 202.89 | 48.50% |

### Trial variability

| Level | Operation | C++ min–max (MiB/s) | Reference min–max (MiB/s) | C++ paired wins / trials |
|---:|---|---:|---:|---:|
| 1 | Compression | 2213.45–2272.44 | 1998.76–2192.87 | 6 / 6 |
| 1 | Decompression | 4123.49–4537.23 | 3956.40–4081.73 | 6 / 6 |
| 2 | Compression | 2616.13–2714.58 | 2385.91–2481.62 | 6 / 6 |
| 2 | Decompression | 8172.40–8423.19 | 6797.42–7111.80 | 6 / 6 |
| 3 | Compression | 1677.21–1825.57 | 1427.10–1743.05 | 6 / 6 |
| 3 | Decompression | 7655.40–7989.15 | 6699.65–7272.58 | 6 / 6 |
| 4 | Compression | 1564.61–1829.13 | 1452.82–1662.67 | 6 / 6 |
| 4 | Decompression | 7442.13–8013.64 | 6856.70–7204.58 | 6 / 6 |
| 5 | Compression | 1814.90–2027.59 | 1730.06–1955.66 | 6 / 6 |
| 5 | Decompression | 8086.69–8449.25 | 6827.05–7188.62 | 6 / 6 |
| 6 | Compression | 1630.52–1872.11 | 1303.14–1591.89 | 6 / 6 |
| 6 | Decompression | 25170.43–26517.12 | 15207.00–16236.42 | 6 / 6 |
| 7 | Compression | 1192.89–1813.23 | 1273.12–1585.66 | 4 / 6 |
| 7 | Decompression | 25779.73–26640.67 | 15827.80–16275.56 | 6 / 6 |
| 8 | Compression | 1208.26–1315.08 | 1009.04–1138.03 | 6 / 6 |
| 8 | Decompression | 26663.22–27071.73 | 15994.62–16554.16 | 6 / 6 |
| 9 | Compression | 1052.16–1096.41 | 879.08–945.95 | 6 / 6 |
| 9 | Decompression | 23107.09–27072.73 | 16054.71–16524.21 | 6 / 6 |
| 10 | Compression | 896.83–1042.69 | 788.39–962.04 | 6 / 6 |
| 10 | Decompression | 25422.82–26826.88 | 15885.71–16539.52 | 6 / 6 |
| 11 | Compression | 762.69–846.48 | 667.14–729.92 | 6 / 6 |
| 11 | Decompression | 22322.61–26766.28 | 15303.80–16561.46 | 6 / 6 |
| 12 | Compression | 799.56–964.32 | 677.62–967.07 | 5 / 6 |
| 12 | Decompression | 25378.74–26694.26 | 16082.06–16767.17 | 6 / 6 |
| 13 | Compression | 535.30–566.27 | 444.43–486.00 | 6 / 6 |
| 13 | Decompression | 26634.12–27323.82 | 16670.68–16931.99 | 6 / 6 |
| 14 | Compression | 454.15–573.95 | 378.28–508.81 | 5 / 6 |
| 14 | Decompression | 25957.41–27549.37 | 16409.63–16917.39 | 6 / 6 |
| 15 | Compression | 382.83–451.36 | 327.00–388.91 | 6 / 6 |
| 15 | Decompression | 13144.67–27506.44 | 16334.57–16828.49 | 5 / 6 |
| 16 | Compression | 57.67–68.90 | 50.33–75.72 | 5 / 6 |
| 16 | Decompression | 16253.93–19682.21 | 11502.51–16301.37 | 6 / 6 |
| 17 | Compression | 57.37–88.84 | 59.16–85.27 | 2 / 6 |
| 17 | Decompression | 18649.62–20163.76 | 14917.79–16455.78 | 6 / 6 |
| 18 | Compression | 49.02–65.24 | 34.62–49.71 | 6 / 6 |
| 18 | Decompression | 13983.00–14646.55 | 10616.31–13149.36 | 6 / 6 |
| 19 | Compression | 46.36–90.03 | 45.70–70.07 | 6 / 6 |
| 19 | Decompression | 14618.06–14997.57 | 12866.30–13423.60 | 6 / 6 |
| 20 | Compression | 48.52–79.16 | 41.16–59.20 | 6 / 6 |
| 20 | Decompression | 14414.55–14724.53 | 12544.56–13346.36 | 6 / 6 |
| 21 | Compression | 59.12–89.94 | 44.01–73.13 | 6 / 6 |
| 21 | Decompression | 14728.85–15136.75 | 12512.10–13446.82 | 6 / 6 |
| 22 | Compression | 45.12–66.11 | 36.10–58.30 | 6 / 6 |
| 22 | Decompression | 13491.08–14810.17 | 12327.50–13261.61 | 6 / 6 |

<!-- SPH_ZSTDPP_EVAL_END -->













### Optimal-parser correctness fixes: performance follow-up

Results from five alternating optimized GCC 13 runs, pinned to CPU 2,
with byte-identical frames and allocation checks. These include unified repeat
candidate collection and the corrected literal-gain threshold, without the
experimental comparison cache. The older full matrix above has not yet been
refreshed for these fixes.

| Compression case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| L16 varied, 100 frames/trial | 7.42 | 8.29 | 89.48% | 21.20 | 21.76 | 97.45% | 140.63 | 628.16 | 22.39% |
| L16 mixed, 1,000 frames/trial | 79.57 | 81.50 | 97.63% | 21.45 | 21.88 | 98.07% | 140.63 | 628.16 | 22.39% |
| L19 varied, 100 frames/trial | 2.65 | 1.78 | 148.93% | 21.70 | 22.25 | 97.52% | 141.02 | 628.16 | 22.45% |
| L19 mixed, 1,000 frames/trial | 59.37 | 52.48 | 113.13% | 22.33 | 22.44 | 99.50% | 141.02 | 628.16 | 22.45% |

The saved baseline median was 7.67 MiB/s: the updated median is 3.31% lower,
with overlapping ranges (baseline 7.34–7.83, updated 7.29–7.69). Median
instructions decreased 0.60%, while cycles increased 2.63%. This is not a
speedup claim; the fixes restore reference-equivalent behavior. Mixed-input
throughput is 1.72% below its saved baseline of 80.96 MiB/s, with substantial
run-to-run variation: baseline 55.78–86.38, updated 51.95–89.33, and reference
51.63–92.34. Median instructions rose 3.17% and cycles rose 0.92%; these noisy
timings do not establish a small speed difference. Level-19 varied throughput
is 1.03% above the saved baseline of 2.63 MiB/s, with overlapping ranges
(baseline 2.613–2.658, updated 2.610–2.685). Reference ranged 1.762–1.796;
the C++ implementation is clearly faster on this case, but the small change
versus the baseline is inconclusive. Median cycles decreased 0.45% and
instructions increased 0.22%. Level-19 mixed throughput regressed 9.51% from
65.62 MiB/s, with disjoint ranges (baseline 63.80–66.48, updated 56.53–62.41).
Median cycles rose 10.03% and instructions rose 10.07%. Although the updated
implementation remains faster than reference on this case, this is a real
regression to investigate, not an optimization success. The corrections add
reference-required behavior absent from the old baseline; they cannot simply
be dropped to recover its speed. All completed frame/allocation checks passed.
Memory is process peak RSS and
code size is executable-file size, consistent with the earlier reports.

### Experimental uniform-block split shortcut

An isolated candidate verifies that an entire block is uniform before skipping
optimal-strategy split histograms. Uniform chunks have identical normalized
fingerprints, so the reference splitter cannot choose a cut. Nonuniform input
still runs the unchanged splitter. The baseline below includes all preceding
correctness fixes; the candidate is not yet integrated into production.

Five alternating optimized GCC 13 trials, CPU 2, with exact-frame and allocation
checks:

| Compression case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| L16 mixed, 1,000 frames/trial | 96.02 | 89.46 | 107.34% | 21.45 | 21.94 | 97.79% | 140.63 | 628.16 | 22.39% |
| L16 varied, 30 frames/trial | 7.81 | 8.79 | 88.87% | 21.20 | 21.71 | 97.64% | 140.63 | 628.16 | 22.39% |
| L19 mixed, 1,000 frames/trial | 62.57 | 54.07 | 115.73% | 22.29 | 22.36 | 99.69% | 141.05 | 628.16 | 22.45% |
| L19 varied, 30 frames/trial | 2.68 | 1.79 | 150.33% | 21.70 | 22.26 | 97.49% | 141.05 | 628.16 | 22.45% |

The corrected baseline median was 92.38 MiB/s, making this a provisional 3.94%
increase. Ranges overlap (baseline 89.31–105.55, candidate 86.97–96.92).
Median cycles decreased 1.75% and instructions decreased 1.75%. Varied input
is 1.02% above its 7.74 MiB/s baseline, with overlapping ranges (baseline
7.51–7.94, candidate 7.67–7.87); it remains slower than reference.
Level-19 mixed throughput is provisionally 4.90% above its 59.65 MiB/s
baseline, with overlapping ranges (baseline 55.07–65.64, candidate 58.74–63.34).
Median cycles decreased 4.70% and instructions decreased 1.22%. Level-19
varied throughput is 0.99% above its 2.66 MiB/s baseline, with overlapping
ranges (baseline 2.60–2.70, candidate 2.58–2.69). All timing trials passed
frame/allocation checks. Broader boundary testing found a pre-existing
level-16 parity gap: an otherwise zero-filled 1 MiB input with byte 131,072
changed to one produces a 59-byte frame differing from reference at byte 30.
The then-current baseline and isolated shortcut emitted identical frames for
this case. Further investigation found three missing optimal-parser rules:
bounded block-start tree catch-up, the reference's first-match-over-4-KiB stop,
and tree/price updates for blocks emitted as RLE. These are now corrected.
GCC unit and full acceptance tests pass, as do 78 uniform/near-uniform cases
at each of levels 16, 19 and 22 (including level 22 under ASan/UBSan).
Permanent unit regressions cover the three failures. The isolated shortcut
also passes the boundary checks and sanitizer run after rebasing; Windows
unit and full acceptance tests pass for the corrected baseline (252.16 seconds).
Fresh performance comparisons are running; the table above describes the prior
saved binaries and must not be treated as current-source performance.
This is not an all-level performance claim.

### Uniform-block shortcut after all parser-history fixes

Fresh five-run alternating GCC 13 measurements, with the same parser-history
fixes in baseline and candidate. The uniform-block shortcut is now retained
after acceptance, reuse/boundary and sanitizer checks. Final production GCC
unit and acceptance tests pass (17.21 seconds), as does the final MSVC rerun
(247.36 seconds).
No improvement is claimed for the inconclusive cases.

| Compression case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| L16 mixed, 1,000 frames/trial | 62.34 | 61.84 | 100.80% | 21.47 | 21.89 | 98.11% | 140.22 | 628.16 | 22.32% |
| L16 varied, 30 frames/trial | 7.52 | 8.25 | 91.14% | 21.20 | 21.74 | 97.52% | 140.22 | 628.16 | 22.32% |
| L19 mixed, 1,000 frames/trial | 74.30 | 59.45 | 124.99% | 22.32 | 22.39 | 99.72% | 136.72 | 628.16 | 21.77% |
| L19 varied, 30 frames/trial | 2.82 | 1.80 | 156.99% | 21.70 | 22.30 | 97.29% | 136.72 | 628.16 | 21.77% |
| L16 zero-heavy, 8,192 frames/trial | 6033.68 | 1535.41 | 392.97% | 21.20 | 21.73 | 97.55% | 140.22 | 628.16 | 22.32% |
| L19 zero-heavy, 8,192 frames/trial | 6873.22 | 1539.27 | 446.52% | 21.70 | 22.30 | 97.29% | 136.72 | 628.16 | 21.77% |

Baseline throughput was 61.98 MiB/s: the candidate's 0.58% increase is
inconclusive, with overlapping ranges (baseline 57.82–66.77, candidate
58.95–68.93). Instructions decreased 2.20%, while cycles increased 0.16%.
Varied throughput is 2.04% above the 7.37 MiB/s baseline, again with overlapping
ranges (baseline 6.54–7.56, candidate 7.01–7.86), and remains below reference.
Level-19 mixed throughput is 0.40% above its 74.01 MiB/s baseline, with
overlapping ranges (baseline 69.77–81.21, candidate 70.74–79.53).
Instructions decreased 1.52% and cycles decreased 0.48%; the small timing gain
is inconclusive. Level-19 varied throughput increased 5.58% over its 2.67 MiB/s
baseline, with disjoint ranges (baseline 2.542–2.686, candidate 2.782–2.888).
Cycles decreased 5.61%, while instruction count was effectively unchanged
(+0.0002%). This suggests an execution/code-layout effect, not evidence that
histogram skipping itself produced this varied-text gain. All frame/allocation
checks passed. The zero-heavy fixture contains eight fully uniform 128 KiB
blocks. On level 16, the candidate improves 311.52% over its 1466.19 MiB/s
baseline, with disjoint ranges (baseline 1422.85–1486.43, candidate
5991.61–6139.68). This confirms a substantial benefit when the shortcut fires,
not a general mixed-input speedup. Level 19 improves 384.05% over its
1419.94 MiB/s baseline, also with disjoint ranges (baseline 1414.68–1428.44,
candidate 6618.11–7096.22). All five trials at both levels passed frame and
allocation checks. The previous pre-correction
timing gains do not establish a gain on this corrected baseline.

The full 22-level optimized matrix above was refreshed after retaining these
fixes. All frame-equality, decoding and allocation checks passed. Its short
200-iteration snapshot shows deficits at level-7 compression (89.25% of
reference) and level-2 decompression (84.59%). Longer confirmations below
reverse both deficits. The separate varied-input level-16 deficit remains
open. The all-level performance goal is not met.

### Longer mixed-corpus confirmation (2026-09-26)

Five alternating, CPU-2-pinned trials of the same optimized GCC 13 binaries,
with 16,384 compression or 65,536 decompression iterations per trial. Values
are medians; memory is peak RSS and code is executable size. This is stronger
evidence for these two rows, not a replacement for the entire matrix above.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Compression / 7 | 1974.24 | 1626.82 | 121.36% | 10.24 | 11.00 | 93.08% | 122.91 | 628.16 | 19.57% |
| Decompression / 2 | 7881.60 | 7294.15 | 108.05% | 5.83 | 5.98 | 97.52% | 98.41 | 202.89 | 48.50% |

Compression ranges: C++ 1707.22–2063.35, reference 1534.97–1683.01 MiB/s.
Decompression ranges: C++ 7746.61–7921.27, reference 7227.32–7438.96 MiB/s.
Both are disjoint. All frame-equality, decode and allocation guards passed.
Raw results: `out/aligned-strategy-control/uniform-matrix-confirm-*`.
No implementation change occurred between the short snapshot and these runs;
this resolves measurement uncertainty, not a newly measured optimization.

Subsequent correctness work found and fixed Huffman-history handling across
raw-literal blocks: raw literals must preserve the previously emitted table,
and a rejected Huffman candidate must not replace it. The level-19 seed-11
regression now matches reference bytes in the expanded reset/streaming oracle.
Permanent regression tests were added. Updated GCC tests and Windows unit/full
acceptance tests pass (Windows acceptance: 279.04 seconds). The isolated cache
also passes GCC acceptance, reset/streaming and boundary checks at levels
16, 19 and 22, plus the level-16 address/undefined-behavior sanitizer oracle.
Controlled performance measurements are in progress; it is not yet retained.
The benchmark tables above predate this correction and are not measurements
of the new cache experiment, which remains isolated pending validation.

### Scoped adjacent-insertion cache experiment (2026-09-26)

Rebased onto the corrected parser and Huffman-history implementation. The cache
reuses exact match lengths for adjacent binary-tree insertions within the same
block. It uses 1536 bytes of fixed storage only in optimal strategies, with no
new heap allocation; disabled-cache object sizes match the baseline on GCC and
MSVC. This remains an isolated experiment, not a retained optimization.

Five alternating CPU-2-pinned trials, optimized GCC 13, 100 frames per varied-input
trial. Throughput and peak RSS are medians; code size is the worker executable.

| Compression case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Level 16, varied | 9.04 | 8.76 | 103.19% | 21.20 | 21.74 | 97.52% | 140.52 | 628.16 | 22.37% |
| Level 16, mixed | 98.85 | 93.21 | 106.05% | 21.45 | 21.88 | 98.07% | 140.52 | 628.16 | 22.37% |
| Level 19, varied | 2.79 | 1.80 | 155.22% | 21.70 | 22.24 | 97.56% | 140.74 | 628.16 | 22.41% |
| Level 19, mixed | 75.99 | 58.54 | 129.81% | 22.33 | 22.36 | 99.86% | 140.74 | 628.16 | 22.41% |
| Level 22, varied | 2.74 | 1.75 | 156.31% | 21.66 | 22.21 | 97.52% | 140.74 | 628.16 | 22.41% |
| Level 22, mixed | 68.69 | 57.25 | 119.99% | 22.33 | 22.40 | 99.67% | 140.74 | 628.16 | 22.41% |
| Level 15 control, varied | 61.10 | 51.62 | 118.38% | 20.95 | 21.90 | 95.63% | 122.69 | 628.16 | 19.53% |
| Level 15 control, mixed | 478.52 | 358.25 | 133.57% | 21.19 | 21.98 | 96.39% | 122.69 | 628.16 | 19.53% |

The corrected baseline measured 7.68 MiB/s: candidate improvement **17.76%**.
Ranges are disjoint: baseline 7.593–7.781, candidate 8.834–9.118, reference
8.432–8.814 MiB/s. Median cycles fell 15.41% despite instructions increasing
10.13%. All five frame-equality and allocation guards passed. Equal measured
RSS does not mean zero storage cost: the fixed cache still adds 1536 bytes.
The five 1000-frame mixed-input trials show a regression risk: baseline
106.02 versus candidate 98.85 MiB/s (**6.77% slower**). Their ranges overlap
(baseline 103.16–113.66, candidate 96.83–114.04, reference 70.75–98.14),
but median cycles increase 5.85% and instructions increase 2.01%. The varied
gain does not justify ignoring this downside; the candidate is not retained.
All mixed frame-equality/allocation guards passed. Level-19 varied input shows
no gain: baseline 2.8167 versus candidate 2.7906 MiB/s (**0.93% slower**),
with a small overlap between baseline 2.8087–2.8539 and candidate 2.7434–2.8131.
Reference ranges from 1.7692 to 1.8065 MiB/s. Cycles increase 1.22%, while
instructions are effectively unchanged (+0.0075%); all five frame/allocation
guards passed. Level-19 mixed input has a 5.59% higher median than its
71.97 MiB/s baseline, with cycles down 5.06% and instructions down 1.38%.
However, wide overlapping ranges (baseline 69.49–78.53, candidate 60.16–78.80,
reference 55.10–60.37 MiB/s) make this gain inconclusive. All five mixed
frame/allocation checks passed. Level-22 varied input regresses 0.89% from
its 2.7656 MiB/s baseline. Ranges are narrowly disjoint: baseline
2.7538–2.7906, candidate 2.6902–2.7524, reference 1.7493–1.7700 MiB/s.
Cycles increase 1.27%, instructions 0.0185%; all five frame/allocation guards
passed. This further argues against retaining the current all-optimal-strategy
cache policy. Mixed level 22 is also slower by median: 68.69 versus the
70.66 MiB/s baseline (**2.79% lower**), with overlapping ranges (baseline
65.62–76.58, candidate 66.28–72.52, reference 48.16–60.81). Cycles increase
1.10% despite instructions falling 1.38%, so this does not establish a gain.
All five mixed frame/allocation guards passed. The complete benchmark run
finished successfully. The cache-disabled level-15 control has overlapping
ranges: varied baseline 59.28–61.03 versus candidate 60.72–62.08 MiB/s
(medians 59.90 versus 61.10, +2.01%); mixed baseline 443.43–509.77 versus
candidate 330.97–503.19 (medians 490.16 versus 478.52, -2.37%). These do not
establish a consistent control speed change. All frame/allocation checks passed.
The current policy is not retained: its level-16 varied benefit must be
preserved while resolving mixed-input overhead and avoiding the level-22
regression. Windows candidate semantic tests remain pending.
These results do not establish the all-level performance goal.
Raw results: `out/aligned-strategy-control/scoped-cache-varied-l16-*`.
Mixed results: `out/aligned-strategy-control/scoped-cache-mixed-l16-*`.
Level-19 results: `out/aligned-strategy-control/scoped-cache-varied-l19-*`.
Level-19 mixed results: `out/aligned-strategy-control/scoped-cache-mixed-l19-*`.
Level-22 varied results: `out/aligned-strategy-control/scoped-cache-varied-l22-*`.
Level-22 mixed results: `out/aligned-strategy-control/scoped-cache-mixed-l22-*`.
Control results: `out/aligned-strategy-control/scoped-cache-*-l15-*`.

### Adjacent-insertion guard experiment (2026-09-26)

Diagnostic counters found that 45.10% of mixed level-16 cache probes followed
non-adjacent insertions and could not hit, versus 1.42% on varied input.
A separate candidate checks adjacency once per insertion and bypasses those
lookups, retaining block invalidation and per-entry keys. It passed all-level
frame parity, level-16 reset/boundary cases and the sanitizer oracle.

Five alternating CPU-2-pinned four-way trials compare the corrected baseline,
original cache, guarded cache and reference. Mixed trials use 2000 frames.
The table reports the guarded candidate; RSS and throughput are medians.

| Compression case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Level 16, mixed | 93.54 | 87.80 | 106.54% | 21.45 | 21.87 | 98.11% | 140.56 | 628.16 | 22.38% |
| Level 16, varied | 8.67 | 8.72 | 99.33% | 21.20 | 21.75 | 97.49% | 140.56 | 628.16 | 22.38% |

Same-run baseline and original-cache medians are 88.15 and 92.85 MiB/s.
The guard's +6.11% versus baseline and +0.74% versus original cache are
inconclusive: ranges overlap widely (baseline 77.78–103.57, original cache
87.82–100.61, guard 75.27–104.04, reference 74.62–92.42). Against the original
cache, cycles increase 0.067% and instructions increase 10.08%; skipping
impossible lookups has not established a net speedup. All frame/allocation
checks passed. The completed five 100-frame varied trials reject the guard:
baseline/original-cache/guard/reference medians are 7.7658/8.9900/8.6657/8.7243
MiB/s. The guard loses 3.61% against the original cache with disjoint ranges
(original 8.7981–9.0813, guard 7.8561–8.7187); baseline ranges 7.6498–7.8463
and reference 8.6304–8.7981. Cycles rise 3.08% and instructions 3.15% versus
the original cache. All frame/allocation guards passed, but reducing lookup
count did not yield a net improvement. The guard is rejected; the original
cache remains isolated pending a narrower strategy policy and validation.
Raw results: `out/aligned-strategy-control/cache-adjacent-mixed-l16-*`.
Varied results: `out/aligned-strategy-control/cache-adjacent-varied-l16-*`.

The follow-up policy enables the original cache only for `binary_tree_optimal`,
excluding the ultra strategies that showed regressions. Its level-16 machine
code matches the original cache byte-for-byte. GCC and MSVC full acceptance,
level-17 reset/streaming checks, GCC boundary tests and the level-17 sanitizer
oracle pass. MSVC runs here are correctness checks, not performance comparisons.
Optimized GCC level-17 and cache-disabled level-22 control measurements are
running; the strategy-scoped candidate remains isolated pending those results.

### Btopt-only cache measurements (2026-09-26)

Five alternating CPU-2-pinned optimized GCC 13 trials; 100 frames per varied
trial. The candidate enables the original cache only for the `btopt` strategy.
Throughput and RSS are medians; code size is the worker executable.

| Compression case | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Level 17, varied | 8.76 | 8.29 | 105.61% | 21.20 | 21.75 | 97.47% | 140.52 | 628.16 | 22.37% |
| Level 17, mixed | 105.04 | 85.08 | 123.46% | 21.85 | 21.89 | 99.80% | 140.52 | 628.16 | 22.37% |
| Level 22 control, varied | 2.73 | 1.76 | 154.53% | 21.73 | 22.24 | 97.68% | 136.81 | 628.16 | 21.78% |
| Level 22 control, mixed | 73.91 | 57.00 | 129.66% | 22.32 | 22.42 | 99.58% | 136.81 | 628.16 | 21.78% |

The candidate improves **13.25%** over its 7.7330 MiB/s baseline. Ranges are
disjoint: baseline 7.4512–7.8245, candidate 8.5769–8.8144, reference
8.1663–8.4126 MiB/s. Cycles fall 12.63% despite instructions increasing 10.73%.
All frame-equality and allocation guards passed. The five 1000-frame mixed
trials have a 7.75% higher median than their 97.49 MiB/s baseline, with cycles
down 6.71% and instructions up 1.87%. Baseline and candidate ranges overlap
(88.30–108.33 versus 103.40–112.20 MiB/s), so that improvement is less certain;
the reference range, 82.78–101.58, is wholly below the candidate's. All mixed
frame/allocation guards passed. The candidate remains isolated while the
cache-disabled level-22 mixed control finishes; the full all-level performance
goal is not yet verified. The varied level-22 control median is 1.31% below
its 2.7616 MiB/s baseline. Ranges narrowly overlap: baseline 2.7546–2.7733,
candidate 2.7037–2.7564, reference 1.7536–1.7769. Cycles increase 1.44% and
instructions 0.098%. The cache is disabled in this case; code-generation/layout
changes are a plausible cause, not proven by these counters alone. All five
frame/allocation checks passed. The small baseline downside remains a tradeoff
to assess against the larger gains at levels 16/17, not a claimed speedup.
Raw results: `out/aligned-strategy-control/cache-btopt-varied-l17-*`.
Mixed results: `out/aligned-strategy-control/cache-btopt-mixed-l17-*`.
Control results: `out/aligned-strategy-control/cache-btopt-varied-l22-*`.
Mixed control results: `out/aligned-strategy-control/cache-btopt-mixed-l22-*`.

The mixed control completed at 73.91 versus baseline 68.88 MiB/s (+7.30%),
but broad overlapping ranges (baseline 60.97–78.32, candidate 60.89–79.66,
reference 50.51–59.47) make the baseline gain inconclusive. All frame/allocation
checks passed. **The btopt-only cache is retained**, accepting the small measured
varied-control downside in exchange for clear varied gains at levels 16/17;
the controls remain substantially faster than reference. No claim is made
that every input improves over the preceding C++ implementation. The retained
headers match the tested candidate exactly. Integrated GCC/MSVC unit and
acceptance tests passed. The full-matrix tables now measure this retained
implementation: optimized GCC 13 Release builds on both sides, 2,000 iterations
per row, with frame, decoding, and allocation checks passing at all 22 levels.
Raw results are in `out/full-metrics-20260926/after-btopt-cache`.
The sequential matrix still shows deficits at compression level 13 (92.31%
of reference) and decompression level 3 (94.82%). Longer alternating runs are
checking those results and decompression level 4's narrow 100.97% lead; the
all-level performance goal is not yet established.

### Retained implementation: longer matrix confirmation

Five alternating CPU-2 trials of 8,192 mixed-corpus frames reverse the
sequential matrix's level-13 compression deficit. This is a measurement
confirmation, not another code improvement. Median throughput is shown below;
the C++ range is 324.45–380.90 MiB/s and reference is 290.14–347.70 MiB/s.
The ranges overlap, so the 13.32% median lead is not a guarantee for every run.
All frame and allocation checks passed. Raw results:
`out/aligned-strategy-control/btopt-matrix-confirm-compress-l13-*`.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Compression 13 | 350.44 | 309.26 | 113.32% | 21.19 | 22.00 | 96.29% | 126.69 | 628.16 | 20.17% |
| Decompression 3 | 7176.25 | 6943.54 | 103.35% | 5.83 | 5.98 | 97.45% | 98.41 | 202.89 | 48.50% |
| Decompression 4 | 7535.29 | 7283.36 | 103.46% | 5.84 | 5.98 | 97.65% | 98.41 | 202.89 | 48.50% |

The decompression row uses five alternating trials of 65,536 frames with the
same reference-generated input for both decoders. C++ spans 7051.22–7365.33
MiB/s; reference spans 5212.64–7324.32. These overlapping ranges and the small
median lead warrant further profiling, not a claim of a reliable all-level win.
Decoded bytes and allocation guards passed in every trial. Raw results:
`out/aligned-strategy-control/btopt-matrix-confirm-decompress-l3-*`.

Level 4 uses the same five-trial protocol. Its ranges are disjoint in this
batch: C++ 7440.25–7586.44 MiB/s versus reference 6208.01–7331.31. All decoding
and allocation checks passed. Raw results use the same prefix with `l4`.
These confirmations do not replace broader input coverage or establish that
every compression and decompression level is reliably faster.

### Experimental combined FSE state reads

An isolated decoder candidate reads the three next-state fields together,
replacing three checked bit extractions with one (at most 26 bits). Five
alternating CPU-2 trials of 65,536 level-3 mixed frames measured a **7.83%**
gain over the retained C++ decoder: 8047.92 versus 7463.72 MiB/s. Their ranges
are disjoint: candidate 7993.86–8170.63, retained 7331.44–7563.78, reference
7211.38–7352.78 MiB/s. Median cycles fell 6.95%; instructions rose 3.80%.
This candidate is **not retained yet**; broader controls, sanitizer checks,
and Windows validation are pending. The main tables still describe production.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate decompression 3 | 8047.92 | 7301.19 | 110.23% | 5.83 | 5.99 | 97.33% | 98.41 | 202.89 | 48.50% |

All timed decoding/allocation guards passed. The candidate also passed
8,384,400 scalar bit-reader comparisons, unit tests, and all-level reciprocal
acceptance. Raw measurements: `out/aligned-strategy-control/group-state-decompress-l3-*`.

Subsequent malformed-frame testing exposed a pre-existing direct-output buffer
overflow in the baseline. Both source variants now bound sequence output by
the caller's remaining capacity and avoid padded copies at its end. The fix
passed 16,198 differential malformed/reset cases under ASan/UBSan; permanent
guard-byte tests were added. The experimental timings above predate this
safety fix and must be remeasured against the corrected baseline before the
combined-read optimization is retained. The generated main tables likewise
predate this newly added safety fix.

**Final decision: reject the combined-state read.** With the bounds fix in
both variants, five new alternating 65,536-frame trials measured candidate
7407.36 versus corrected baseline 8050.40 MiB/s (**7.99% slower**). Ranges are
disjoint: candidate 7254.33–7515.06, baseline 7614.97–8124.40; reference
7163.51–7392.96. Candidate cycles increased 8.19% and instructions 6.20%.
The earlier pre-fix gain does not hold for the corrected implementation.
Only the output-bounds safety fix is retained; the grouped-read code remains
an isolated, rejected experiment. All new decode/allocation guards passed.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Corrected baseline decompression 3 | 8050.40 | 7300.87 | 110.27% | 5.84 | 5.99 | 97.52% | 98.41 | 202.89 | 48.50% |
| Rejected candidate decompression 3 | 7407.36 | 7300.87 | 101.46% | 5.84 | 5.99 | 97.52% | 98.41 | 202.89 | 48.50% |

Raw results: `out/aligned-strategy-control/group-state-safe-l3-*`.
This isolated baseline measurement is not an all-level benchmark refresh.

### Experimental repeat-Huffman size bound

An isolated candidate skips repeat-table encoding when a histogram-derived
lower bound proves its body cannot beat the newly encoded body. Five
alternating 16,384-frame varied-text level-3 trials measured candidate 2378.80
versus retained C++ 2346.86 MiB/s (+1.36%). Ranges overlap: candidate
2344.37–2454.16, retained 2322.42–2360.80; reference is 2484.63–2609.56.
Instructions are essentially unchanged (93.068B to 93.112B), so this is **not
an established speedup**. Mixed-input controls and skip-frequency diagnostics
are pending; production is unchanged. All frame/allocation checks passed.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate varied compression 3 | 2378.80 | 2570.27 | 92.55% | 6.00 | 6.61 | 90.73% | 122.96 | 628.16 | 19.57% |

Raw results: `out/aligned-strategy-control/repeat-bound-varied-l3-*`.

The mixed control measured 1846.97 versus retained 1813.62 MiB/s (+1.84%),
again with broad overlap (1748.12–1927.70 versus 1784.86–1920.25).

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate mixed compression 3 | 1846.97 | 1751.86 | 105.43% | 6.25 | 6.72 | 92.97% | 122.96 | 628.16 | 19.57% |

**Not retained:** diagnostic runs found zero skipped encodings—14 eligible
repeat candidates on varied text (warmup plus one frame), none on mixed input.
Mixed instructions were unchanged at 121.015B. The overlapping timing changes
do not demonstrate the proposed work reduction. A stronger follow-up is to
compute exact per-stream sizes before encoding either candidate, then emit only
the selected one. Raw mixed results use `repeat-bound-mixed-l3-*`; diagnostic
counts are in `repeat-bound-probe-{varied,mixed}.txt` under the same directory.

### Exact-size select-first experiment (rejected)

Computing both exact stream sizes before writing only the winner preserved
all tested bytes/history, but did not improve throughput. Five varied-text
trials measured candidate 2276.89 versus retained 2353.97 MiB/s (-3.27%);
five mixed trials measured 1763.60 versus 1813.51 (-2.75%). Both ranges overlap.
Varied instructions fell 1.24%, but cycles rose 2.10%; doing less encoding
was not enough to offset the changed execution cost. Production is unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Rejected varied compression 3 | 2276.89 | 2568.14 | 88.66% | 6.00 | 6.59 | 91.05% | 122.89 | 628.16 | 19.56% |
| Rejected mixed compression 3 | 1763.60 | 1670.31 | 105.59% | 6.24 | 6.75 | 92.42% | 122.89 | 628.16 | 19.56% |

All frame/allocation guards passed. The mixed batch was interrupted after
two complete trials; after confirming the process was gone and checking saved
frames, only the remaining three trials were run with unchanged binaries.
Raw results: `out/aligned-strategy-control/select-first-{varied,mixed}-l3-*`.

### Histogram-interval selection experiment

A follow-up uses existing symbol counts to bound stream sizes, rescanning
literals only for close decisions. Five alternating varied-text level-3 trials
(16,384 frames each) measured 2319.15 versus baseline 2285.87 MiB/s (+1.46%).
Instructions fell 4.52% and cycles 2.11%, but throughput ranges overlap:
candidate 2277.33–2381.87, baseline 2203.60–2330.48, reference 2219.13–2511.16.
The candidate remains experimental and below reference; mixed controls and
further validation are pending. All frame/allocation checks passed.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate varied compression 3 | 2319.15 | 2427.04 | 95.55% | 6.00 | 6.60 | 91.00% | 126.80 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/huffman-interval-varied-l3-*`.

The mixed control measured 1745.01 versus baseline 1790.32 MiB/s (-2.53%),
with overlapping ranges (1637.42–1815.17 versus 1744.28–1840.33).
Mixed instruction counts were essentially unchanged. A separate diagnostic
confirmed that varied text takes 14 interval decisions, all selecting repeat
without rescanning (warmup plus one frame); mixed input takes none. Thus the
varied work reduction is real, but the mixed timing difference is not caused
by executing the new size-counting path. Code-generation effects and timing
variation remain possibilities, not established causes. **Not retained yet.**

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate mixed compression 3 | 1745.01 | 1663.91 | 104.87% | 6.25 | 6.74 | 92.70% | 126.80 | 628.16 | 20.19% |

All guards passed. Mixed raw results use `huffman-interval-mixed-l3-*`;
diagnostics use `huffman-interval-probe-{varied,mixed}.txt` in the same directory.

An independent five-trial confirmation doubled each varied run to 32,768
frames and reversed the initial order. Candidate median 2341.37 versus
baseline 2308.23 MiB/s repeats the small gain (+1.44%). Ranges narrowly
overlap: candidate 2324.25–2379.97, baseline 2258.83–2332.07; reference
2437.04–2499.24. Cycles fell 2.50% and instructions 4.52%. The candidate
still trails reference, and the longer mixed-input confirmation is pending.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate varied compression 3, confirmation | 2341.37 | 2460.42 | 95.16% | 6.00 | 6.55 | 91.59% | 126.80 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/huffman-interval-confirm-varied-l3-*`.

The longer mixed confirmation did not reproduce the initial downside:
candidate 1891.48 versus baseline 1814.69 MiB/s (+4.23%). Ranges overlap
widely (1719.45–1929.72 versus 1701.95–1953.73; reference 1589.86–1817.77),
so this is not a demonstrated mixed-input improvement. Instructions remained
essentially unchanged. All frame/allocation checks passed. The candidate
remains isolated while broader-level and sanitizer/Windows gates run.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate mixed compression 3, confirmation | 1891.48 | 1677.50 | 112.76% | 6.25 | 6.73 | 92.86% | 126.80 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/huffman-interval-confirm-mixed-l3-*`.

The interval candidate subsequently passed ASan/UBSan history and unit tests,
Windows all-level acceptance, and a separate comparison of all 4,096 Windows
history records against the baseline. Broader optimized comparisons at levels
1, 5, 15, and 22 on both corpora are now running. It is still not retained.

The first broad result, level-1 varied text, has candidate median 3066.69
versus baseline 2936.19 MiB/s (+4.44%). Ranges overlap (2996.20–3129.69
versus 2896.65–3182.61), and reference remains faster (3245.94–3436.01).
All five 8,192-frame comparisons passed frame/allocation guards. Other cases
are still pending; this is not a retention decision.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate varied compression 1 | 3066.69 | 3348.42 | 91.59% | 5.12 | 5.88 | 87.23% | 126.85 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/interval-broad-varied-l1-*`.

Level-1 mixed input completed at candidate 2368.91 versus baseline 2330.08
MiB/s (+1.67%), with broad overlapping ranges (2171.29–2448.08 versus
2204.84–2388.28). Reference spans 2127.33–2184.47. Instructions are nearly
unchanged (58.422B to 58.443B), so the baseline gain remains inconclusive.
All five 8,192-frame comparisons passed frame/allocation checks.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate mixed compression 1 | 2368.91 | 2143.38 | 110.52% | 5.50 | 6.12 | 89.79% | 126.85 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/interval-broad-mixed-l1-*`.

Level-5 varied text measured candidate 291.42 versus baseline 275.84 MiB/s
(+5.65%) over five 8,192-frame trials. Ranges overlap widely: candidate
276.98–323.59, baseline 246.87–293.75, reference 241.56–287.09.
Cycles fell 5.06% and instructions 0.63%. The median exceeds reference, but
the overlap does not establish a reliable lead. All frame/allocation checks
passed; mixed level 5 and levels 15/22 remain pending.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate varied compression 5 | 291.42 | 278.18 | 104.76% | 7.50 | 8.32 | 90.05% | 122.70 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/interval-broad-varied-l5-*`.

The level-5 mixed control measured 2194.00 versus baseline 2214.17 MiB/s
(-0.91%), with overlapping ranges (2170.35–2285.00 versus 2113.36–2245.44).
Reference spans 1956.24–2132.84, below the candidate range in this batch.
All five 8,192-frame frame/allocation checks passed. The small baseline
difference is inconclusive; levels 15/22 are still running.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate mixed compression 5 | 2194.00 | 2013.95 | 108.94% | 7.84 | 8.49 | 92.31% | 122.70 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/interval-broad-mixed-l5-*`.

Level-15 varied text measured candidate 61.27 versus baseline 60.77 MiB/s
(+0.82%) over five 512-frame trials. Ranges overlap substantially (58.54–61.50
versus 58.06–61.66), while reference spans 51.97–52.37. Cycles are essentially
unchanged, so this is not an established improvement over baseline. All frame
and allocation checks passed; mixed level 15 and level 22 remain pending.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate varied compression 15 | 61.27 | 52.29 | 117.17% | 20.94 | 21.85 | 95.85% | 126.60 | 628.16 | 20.15% |

Raw results: `out/aligned-strategy-control/interval-broad-varied-l15-*`.

Level-15 mixed input completed at candidate 460.29 versus baseline 451.12
MiB/s (+2.03%) over five 4,096-frame trials. Ranges overlap widely:
candidate 426.28–533.49, baseline 393.97–484.50, reference 385.55–439.52.
Instructions are essentially unchanged (66.073B), so this is not an established
baseline improvement. All frame/allocation checks passed; level 22 is running.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate mixed compression 15 | 460.29 | 420.70 | 109.41% | 21.19 | 22.00 | 96.31% | 126.60 | 628.16 | 20.15% |

Raw results: `out/aligned-strategy-control/interval-broad-mixed-l15-*`.

Level-22 varied text exposes a downside: candidate 2.5787 versus baseline
2.7364 MiB/s (**5.76% slower**) over five 64-frame trials. Ranges are disjoint:
candidate 2.5102–2.6099, baseline 2.6376–2.7450; reference 1.7480–1.7821.
Cycles increased 5.56% with essentially unchanged instructions (363.105B).
The candidate still leads reference but loses baseline performance here;
this tradeoff must be assessed before retention. All frame/allocation checks
passed. The final mixed level-22 case remains running.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate varied compression 22 | 2.58 | 1.77 | 145.30% | 21.70 | 22.24 | 97.58% | 140.74 | 628.16 | 22.41% |

Raw results: `out/aligned-strategy-control/interval-broad-varied-l22-*`.

The broad interval experiment is now complete. Mixed level 22 gave 74.67 MiB/s
versus baseline 76.63 (-2.55%) and reference 62.13 across five 1,024-frame
trials. Baseline (70.34–78.52) and candidate (65.66–76.80) ranges overlap;
instructions were essentially unchanged at 108.664B. All byte-parity and
allocation guards passed. Together with the clear varied-text level-22
regression above, this motivates testing compile-time strategy scoping before
integrating the candidate. These remain experimental, not retained results.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Candidate mixed compression 22 | 74.67 | 62.13 | 120.19% | 22.33 | 22.34 | 99.97% | 140.74 | 628.16 | 22.41% |

Raw results: `out/aligned-strategy-control/interval-broad-mixed-l22-*`.

### Strategy-scoped Huffman selection experiment

The next isolated candidate enables pre-emission Huffman selection only for
fast, double-fast, and greedy strategies. Lazy and deeper strategies retain
the legacy selection path. Strict optimized GCC unit tests and all-level
acceptance passed; 4,096 direct comparisons of both policy variants passed
under ASan/UBSan. Generated machine code still differs at disabled levels,
so those levels require measured controls rather than an assumption of no impact.

Five alternating, CPU-pinned, 16,384-frame varied-text level-1 trials produced
a candidate median 4.25% above baseline (2,928.20 MiB/s), with 4.92% fewer
instructions (84.924B to 80.747B). Timing ranges overlap: baseline
2,881.48–2,987.32 and candidate 2,787.49–3,102.88 MiB/s. Reference remains
clearly faster at 3,368.45–3,447.30 MiB/s. All frame/allocation guards passed.
Levels 3, 15, and 22 remain pending; this is not yet a retained optimization.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Scoped candidate varied compression 1 | 3052.59 | 3414.73 | 89.39% | 5.09 | 5.89 | 86.35% | 126.86 | 628.16 | 20.20% |

Raw results: `out/aligned-strategy-control/scoped-varied-l1-*`.

Scoped level 3 also completed five 16,384-frame trials: candidate median
2,428.72 versus baseline 2,366.03 MiB/s (+2.65%). Instructions decreased
4.52% (93.068B to 88.863B), consistent with the earlier unscoped result.
Baseline 2,337.07–2,382.18 and candidate 2,349.79–2,457.82 ranges overlap;
reference 2,520.65–2,584.07 remains disjoint above the candidate. All guards
passed. The high-level controls are still running.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Scoped candidate varied compression 3 | 2428.72 | 2571.30 | 94.45% | 6.00 | 6.65 | 90.31% | 126.81 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/scoped-varied-l3-*`.

The scoped level-15 control is effectively neutral: five 512-frame trials
gave baseline 59.94 and candidate 60.07 MiB/s (+0.22%), with overlapping
ranges of 58.96–61.00 and 58.23–60.51. Instructions are unchanged at
130.532B; the reference range is 51.08–52.05 MiB/s. All guards passed.
Level 22 remains pending before the integration decision.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Scoped candidate varied compression 15 | 60.07 | 51.57 | 116.48% | 20.94 | 21.86 | 95.78% | 122.69 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/scoped-varied-l15-*`.

The scoped level-22 control still regresses: five 64-frame trials gave
candidate 2.5797 versus baseline 2.7151 MiB/s (-4.98%). Ranges are disjoint:
candidate 2.5081–2.6106, baseline 2.6454–2.7287; reference 1.7422–1.7663.
Instructions remain unchanged at 363.105B, while cycles rise from 107.666B
to 112.505B. All guards passed. A freshly rebuilt, unchanged baseline has
identical executable instruction bytes to the saved baseline, ruling out a
stale baseline as the explanation for this comparison. The candidate remains
isolated while the disabled-path generated-code difference is investigated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Scoped candidate varied compression 22 | 2.58 | 1.75 | 147.09% | 21.70 | 22.23 | 97.59% | 136.81 | 628.16 | 21.78% |

Raw results: `out/aligned-strategy-control/scoped-varied-l22-*`.

### Preserving legacy Huffman size expressions

A follow-up candidate removes a cached size variable from the disabled path,
preserving the original size expressions. Level-1/3 executable instruction
bytes remain identical to the scoped candidate. Level-22 tree and optimal
parser disassembly now matches baseline at the same addresses. GCC/MSVC
all-level acceptance and 4,096 policy-history comparisons passed, including
ASan/UBSan on GCC. These are correctness/code-generation findings, not proof
that the level-22 timing regression is resolved.

The level-15 control completed five 512-frame trials: candidate 60.55 versus
baseline 60.77 MiB/s (-0.37%). Ranges overlap (candidate 59.99–61.01,
baseline 59.96–61.87), and instructions remain unchanged at 130.532B.
All guards passed. Level-22 measurement is still running; the candidate is
not integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Expression candidate varied compression 15 | 60.55 | 51.24 | 118.15% | 20.96 | 21.88 | 95.81% | 122.69 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/expression-varied-l15-*`.

The expression-preserving level-22 run is complete: candidate median 2.7189
versus baseline 2.7449 MiB/s (-0.95%), with overlapping ranges
2.6643–2.7487 and 2.7024–2.7507. Reference ranged 1.7442–1.7735.
Instructions are unchanged at 363.105B; cycles were 107.320B versus baseline
106.879B. The earlier disjoint approximately 5% regression is no longer
observed. This supports the code-layout explanation without establishing that
layout was the sole cause. All frame/allocation guards passed.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Expression candidate varied compression 22 | 2.72 | 1.76 | 154.37% | 21.70 | 22.23 | 97.59% | 136.81 | 628.16 | 21.78% |

Raw results: `out/aligned-strategy-control/expression-varied-l22-*`.
This version is now integrated, with a permanent 4,096-case policy/history
equivalence test. Integrated validation and the all-level worker rebuild are
in progress; the main benchmark tables have not yet been refreshed. The
low-level gains remain insufficient to beat reference on varied text at
levels 1 and 3, so the overall performance goal remains open.

Integrated validation and the all-level rebuild have now finished successfully.
The main tables above were refreshed from
`out/full-metrics-20260926/after-huffman-preselection`: GCC 13.3 Release,
2,000 timed iterations, CPU 2, all 22 levels, tracing disabled. All frame,
decode, and allocation guards passed. In this run, decompression exceeds
reference at every level; compression falls below reference at levels 3
(99.11%), 5 (97.89%), 7 (98.04%), and 13 (93.15%). Longer alternating
confirmations are planned for those four gaps; this single sweep does not
establish a repeatable win or loss near parity. The separately measured
varied-text deficits at levels 1 and 3 also remain open.

The first longer confirmation is complete: mixed level 3, five alternating
8,192-frame C++/reference pairs. C++ ranged 1,800.95–1,932.27 MiB/s versus
reference 1,636.70–1,785.93, with medians shown below. Thus the initial
99.11% mixed-corpus result did not repeat; these longer ranges are disjoint
in C++'s favor. Instruction medians were 60.513B C++ versus 63.034B reference.
All frame/allocation guards passed. This does not resolve the separate
varied-text level-3 deficit.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Confirmed mixed compression 3 | 1843.33 | 1761.81 | 104.63% | 6.25 | 6.74 | 92.76% | 126.81 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/huffman-matrix-confirm-l3-*`.

Mixed level 5 also completed five alternating 8,192-frame pairs. C++ median
exceeded reference, so the initial 97.89% deficit did not repeat. Ranges
overlap: C++ 2,104.16–2,233.46 and reference 1,905.66–2,146.99 MiB/s;
this is weaker evidence than the disjoint level-3 result. C++ retired more
instructions (60.064B versus 56.122B) despite fewer measured cycles
(16.962B versus 17.961B), leaving further optimization headroom. All guards passed.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Confirmation mixed compression 5 | 2158.38 | 2026.99 | 106.48% | 7.84 | 8.47 | 92.48% | 122.70 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/huffman-matrix-confirm-l5-*`.

Mixed level 7's longer comparison likewise reverses the initial 98.04%
result. Five 8,192-frame pairs gave disjoint ranges: C++ 1,775.40–1,904.42
versus reference 1,355.97–1,627.27 MiB/s. Instruction medians were 60.162B
versus 61.690B. All guards passed; level 13 remains under measurement.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Confirmed mixed compression 7 | 1846.54 | 1561.02 | 118.29% | 10.25 | 11.05 | 92.79% | 122.70 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/huffman-matrix-confirm-l7-*`.

Mixed level 13 completes the longer confirmations. Across five 8,192-frame
pairs, C++ ranged 394.25–450.25 MiB/s and reference 372.84–389.72,
disjoint in C++'s favor. The initial 93.15% deficit did not repeat.
Instruction medians were 131.434B versus 149.886B, and all guards passed.
All four initial mixed-corpus deficits now have longer-run medians above
reference, but level 5's ranges overlap. Varied-text levels 1 and 3 remain
below reference and are the next profiling targets.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Confirmed mixed compression 13 | 427.32 | 381.12 | 112.12% | 21.19 | 21.98 | 96.39% | 126.69 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/huffman-matrix-confirm-l13-*`.

### Narrow sequence-histogram experiment

An isolated candidate reduces parallel histogram scratch clearing and reduction
to 64 bins for fast-strategy sequence codes, retaining a fully initialized
256-bin result. Generic histograms and deeper strategies keep the existing
path. Initial sanitizer, unit, and all-level interoperability checks passed.

Five alternating 16,384-frame varied-text level-1 trials gave candidate
3,098.11 versus baseline 3,084.31 MiB/s (+0.45%). Ranges overlap:
candidate 3,066.13–3,124.36, baseline 3,028.12–3,096.76; reference remains
ahead at 3,387.40–3,446.93. Instructions decreased only 0.27%
(80.747B to 80.532B), with essentially unchanged cycles. This is not a
convincing throughput improvement; other cases are still running. All guards
passed, and the candidate is not integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Histogram candidate varied compression 1 | 3098.11 | 3422.75 | 90.52% | 5.09 | 5.86 | 86.92% | 126.87 | 628.16 | 20.20% |

Raw results: `out/aligned-strategy-control/histogram64-varied-l1-*`.

Mixed level 1 is also inconclusive: five 16,384-frame trials gave candidate
2,345.38 versus baseline 2,371.70 MiB/s (-1.11%), with overlapping ranges
2,291.04–2,365.39 and 2,299.94–2,379.44. Instructions decreased only
0.12% (116.877B to 116.741B); cycles were effectively unchanged.
Reference ranged 1,901.63–2,175.27 MiB/s. All guards passed. The two level-3
cases remain running; neither level-1 case supports retaining this experiment.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Histogram candidate mixed compression 1 | 2345.38 | 2114.47 | 110.92% | 5.45 | 6.12 | 89.14% | 126.87 | 628.16 | 20.20% |

Raw results: `out/aligned-strategy-control/histogram64-mixed-l1-*`.

Varied-text level 3 is effectively unchanged: five 16,384-frame trials gave
candidate 2,429.94 versus baseline 2,429.38 MiB/s (+0.02%). Ranges overlap
broadly (candidate 2,374.15–2,469.27; baseline 2,419.55–2,470.53), and
reference ranged 2,457.13–2,572.20. Instructions decreased 0.25%
(88.863B to 88.644B), without a throughput gain. All guards passed.
The final mixed level-3 control remains running; this experiment has not
improved either targeted varied-text deficit.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Histogram candidate varied compression 3 | 2429.94 | 2556.59 | 95.05% | 5.97 | 6.64 | 89.88% | 126.81 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/histogram64-varied-l3-*`.

The histogram experiment is complete and **not retained**. Its final mixed
level-3 case gave candidate 1,900.17 versus baseline 1,857.12 MiB/s (+2.32%),
but ranges overlap broadly: candidate 1,752.17–1,926.13 and baseline
1,787.20–1,926.68. Reference ranged 1,652.20–1,786.18. Instructions fell
from 121.017B to 120.153B, and all guards passed. This noisy mixed-case
result does not offset the lack of meaningful improvement on either targeted
varied-text case. Production retains the previous Huffman optimization only.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Rejected histogram candidate mixed compression 3 | 1900.17 | 1764.18 | 107.71% | 6.25 | 6.77 | 92.21% | 126.81 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/histogram64-mixed-l3-*`.

### Once-per-table Huffman validation experiment

This isolated candidate checks code lengths for symbols present in the
existing histogram once before emitting the selected Huffman table's streams,
instead of checking every literal. The default stream API remains checked;
only prevalidated fast-strategy calls use the unchecked loop. Sanitizer-backed
packing/invalid-code/history tests, units, and full acceptance passed.

Five alternating 16,384-frame varied-text level-1 trials gave candidate
3,107.77 versus baseline 3,086.28 MiB/s (+0.70%), with overlapping ranges
3,049.37–3,154.50 and 3,050.56–3,093.07. Instructions fell 1.37%
(80.747B to 79.642B); this is real work reduction but not yet a convincing
throughput gain. Reference remains ahead at 3,339.74–3,419.34 MiB/s.
All guards passed; the remaining cases are running and the candidate is not integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Validation candidate varied compression 1 | 3107.77 | 3377.47 | 92.01% | 5.13 | 5.89 | 87.06% | 126.87 | 628.16 | 20.20% |

Raw results: `out/aligned-strategy-control/huffman-validation-varied-l1-*`.

Mixed level 1 is neutral: five 16,384-frame trials gave candidate 2,371.83
versus baseline 2,373.23 MiB/s (-0.06%), with overlapping ranges
2,299.74–2,404.88 and 2,322.37–2,402.82. Instructions were essentially
unchanged (116.877B baseline, 116.886B candidate). Reference ranged
2,090.36–2,167.61 MiB/s. All guards passed; both level-3 cases are still running.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Validation candidate mixed compression 1 | 2371.83 | 2136.03 | 111.04% | 5.46 | 6.14 | 88.99% | 126.87 | 628.16 | 20.20% |

Raw results: `out/aligned-strategy-control/huffman-validation-mixed-l1-*`.

Varied-text level 3 gives a promising but not decisive result: five
16,384-frame trials produced candidate 2,436.05 versus baseline 2,382.09
MiB/s (+2.27%), with 1.76% fewer instructions (88.863B to 87.299B).
Ranges overlap: candidate 2,386.62–2,449.05, baseline 2,345.29–2,455.19;
reference ranged 2,447.55–2,557.93. All guards passed. The final mixed
level-3 control is still running, and no integration decision has been made.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Validation candidate varied compression 3 | 2436.05 | 2498.73 | 97.49% | 6.00 | 6.61 | 90.89% | 126.91 | 628.16 | 20.20% |

Raw results: `out/aligned-strategy-control/huffman-validation-varied-l3-*`.

The final mixed level-3 case gave candidate 1,808.69 versus baseline
1,842.71 MiB/s (-1.85%). Ranges overlap: candidate 1,777.40–1,876.54,
baseline 1,808.77–1,910.48; reference ranged 1,724.16–1,795.28.
Instructions decreased 0.88% (121.017B to 119.951B), while cycles rose
from 39.066B to 40.373B. All guards passed. Because the varied-text gain
and mixed-input downside are both uncertain, an independent longer level-3
comparison is needed before retention; the candidate remains isolated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Validation candidate mixed compression 3 | 1808.69 | 1770.05 | 102.18% | 6.25 | 6.75 | 92.53% | 126.91 | 628.16 | 20.20% |

Raw results: `out/aligned-strategy-control/huffman-validation-mixed-l3-*`.

The independent, longer varied-text level-3 confirmation did **not** reproduce
the speedup. Five reversed-start-order 32,768-frame trials gave candidate
2,422.21 versus baseline 2,442.79 MiB/s (-0.84%). Candidate ranged
1,822.62–2,464.88, including a slow outlier; baseline ranged
2,341.02–2,455.43 and reference 2,409.20–2,557.90. No samples were excluded.
The instruction reduction persisted (177.718B to 174.591B), but cycles did
not improve (60.129B to 60.392B). All guards passed. The mixed confirmation
is still running; there is currently no repeatable target throughput gain
supporting integration.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Validation confirmation varied compression 3 | 2422.21 | 2539.70 | 95.37% | 5.97 | 6.64 | 89.88% | 126.91 | 628.16 | 20.20% |

Raw results: `out/aligned-strategy-control/validation-confirm-varied-l3-*`.

The independent mixed level-3 confirmation is now complete. Five 32,768-frame
trials gave candidate 1,847.50 versus baseline 1,854.17 MiB/s (-0.36%).
Ranges overlap: candidate 1,769.81–1,889.18, baseline 1,767.30–1,938.70;
reference ranged 1,646.87–1,785.56. Instructions fell from 242.026B to
239.892B, but cycles rose from 77.530B to 79.123B. All frame and allocation
guards passed.

**Decision: reject the validation-hoisting candidate.** The initial varied-text
gain did not repeat in the longer confirmation, and the mixed-input medians
did not improve. Fewer instructions alone are not a throughput improvement.
Production and the main benchmark tables remain unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Rejected validation confirmation mixed compression 3 | 1847.50 | 1776.65 | 103.99% | 6.21 | 6.75 | 92.07% | 126.91 | 628.16 | 20.20% |

Raw results: `out/aligned-strategy-control/validation-confirm-mixed-l3-*`.

### Compact sequence storage experiment (isolated, 2026-09-26)

This candidate replaces 12-byte parsed sequence records with 8-byte records,
using block metadata for an exceptional long length. It passed optimized unit
tests, all-level output parity and cross-decoding, the full ASan/UBSan unit
suite, and 1,048,590 additional container/view/repair checks. It is not integrated.

The first completed timing case is a regression: five alternating CPU-pinned
16,384-frame varied-text level-1 trials gave candidate 2,865.26 versus current
production 3,036.55 MiB/s (-5.64%). Candidate ranged 2,783.86–2,896.89 and
baseline 2,895.22–3,071.82, with a small overlap; reference ranged
3,313.58–3,476.66. Instructions increased 5.22% (80.747B to 84.962B), and
cycles rose from 23.801B to 25.339B. All output and allocation guards passed.
Smaller records do not yet translate into faster execution. The mixed-input
control and level-3 cases are pending; production and the main tables are unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Compact candidate varied compression 1 | 2865.26 | 3413.04 | 83.95% | 5.09 | 5.89 | 86.35% | 126.69 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/compact-sequences-varied-l1-*`.

The mixed-input level-1 control is substantially worse. Five trials gave
candidate 1,465.77 versus baseline 2,329.98 MiB/s (-37.09%). Ranges are
disjoint: candidate 1,428.98–1,470.96 versus baseline 2,294.39–2,342.61;
reference ranged 2,084.25–2,167.55. Instructions increased 17.86%
(116.877B to 137.755B), and cycles rose from 31.358B to 50.193B.
All frame and allocation guards passed. This implementation will not be
retained as-is; level-3 measurements are still running to characterize the
overhead before considering a different compact-record access design.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Compact candidate mixed compression 1 | 1465.77 | 2111.94 | 69.40% | 5.38 | 6.14 | 87.53% | 126.69 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/compact-sequences-mixed-l1-*`.

Varied-text level 3 also regresses: candidate 2,280.10 versus baseline
2,433.70 MiB/s (-6.31%), with disjoint ranges of 2,275.00–2,300.75 and
2,419.19–2,450.14 respectively. Reference ranged 2,429.81–2,562.48.
Instructions increased 7.55% (88.863B to 95.575B), and cycles rose from
30.218B to 31.954B. All five trials passed the output/allocation guards.
The final mixed level-3 case remains pending. These measurements reject the
current access design, not every possible implementation of compact storage.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Compact candidate varied compression 3 | 2280.10 | 2551.11 | 89.38% | 6.00 | 6.62 | 90.73% | 126.81 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/compact-sequences-varied-l3-*`.

The final mixed level-3 case confirms the regression: candidate 1,534.34
versus baseline 1,861.34 MiB/s (-17.57%). Candidate ranged 1,454.18–1,572.87,
disjoint from baseline 1,750.36–1,936.79; reference ranged 1,629.57–1,739.75.
Instructions rose 13.18% (121.017B to 136.964B), and cycles increased from
39.182B to 47.804B. All five trials passed output and allocation checks.
**The compact reconstruction-based candidate is rejected.** All four measured
cases were slower than production. The isolated artifacts are retained for
profiling the overhead; no production changes were made.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Rejected compact candidate mixed compression 3 | 1534.34 | 1656.61 | 92.62% | 6.25 | 6.68 | 93.57% | 126.81 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/compact-sequences-mixed-l3-*`.

### Compact storage with plain temporary fields (isolated, 2026-09-27)

This revision retains 8-byte stored records but replaces the temporary
match-length/repeat-code bitfields with ordinary integer members. It targets
stack read/modify/write chains observed in the rejected candidate's generated
parser and entropy code. It passed the container sanitizer oracle, optimized
unit tests, and all-level encoding/cross-decoding acceptance.

Five alternating four-way, CPU-pinned 8,192-frame mixed-input level-1 trials
gave candidate 2,252.24, production 2,360.53, rejected packed-temporary
candidate 1,475.66, and reference 2,159.18 MiB/s. Plain temporaries recover
52.63% over the rejected candidate, but remain 4.59% below production.
Ranges are candidate 2,121.50–2,287.55, production 2,279.90–2,394.82,
packed 1,424.30–1,479.41, and reference 2,117.42–2,175.37.
Instructions fall from packed 68.883B to 58.705B, near production's 58.443B;
cycles fall from 25.053B to 16.108B versus production's 15.521B.
All frame/allocation guards passed. This supports the temporary-bitfield
overhead hypothesis, but does not establish a production improvement or
exclude code-layout effects. Remaining cases are running; no integration.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Plain-temporary compact mixed compression 1 | 2252.24 | 2159.18 | 104.31% | 5.37 | 6.13 | 87.57% | 126.69 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/compact-plain-mixed-l1-*`.

Varied-text level 1 is promising: five 8,192-frame trials gave candidate
3,192.34 versus production 3,064.27 MiB/s (+4.18%). Ranges are disjoint:
candidate 3,176.13–3,217.79 versus production 3,048.88–3,094.18. The rejected
packed-temporary design gave 2,889.69 (2,876.15–2,914.58), and reference
gave 3,415.64 (3,336.60–3,458.48). Candidate instructions decreased 0.67%
versus production (40.377B to 40.108B), and cycles fell from 11.885B to
11.478B. All guards passed. This is an initial production-baseline improvement,
but reference remains faster; longer confirmation and the remaining level-3
cases are required, especially given the mixed-input level-1 downside.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Plain-temporary compact varied compression 1 | 3192.34 | 3415.64 | 93.46% | 5.09 | 5.87 | 86.69% | 126.69 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/compact-plain-varied-l1-*`.

Mixed-input level 3 recovers part of the rejected design's loss but does not
beat production. Five 8,192-frame medians are candidate 1,738.56, production
1,800.60, packed-temporary 1,466.96, and reference 1,759.53 MiB/s.
Candidate is 18.51% above the packed design and 3.45% below production.
Ranges overlap for candidate (1,725.69–1,825.56), production
(1,799.52–1,839.37), and reference (1,576.47–1,791.60); packed ranged
1,434.21–1,500.08. Instructions are candidate 63.398B versus production
60.513B (+4.77%) and packed 68.487B; cycles are 20.835B, 20.110B, and
24.900B respectively. All guards passed. The final varied level-3 case
is pending; the candidate remains isolated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Plain-temporary compact mixed compression 3 | 1738.56 | 1759.53 | 98.81% | 6.25 | 6.75 | 92.59% | 126.81 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/compact-plain-mixed-l3-*`.

The final varied-text level-3 case is inconclusive: candidate 2,450.71 versus
production 2,427.70 MiB/s (+0.95%), with overlapping ranges of
2,382.21–2,486.27 and 2,352.07–2,445.34. The packed version gave 2,291.51
(2,280.55–2,336.11), and reference 2,580.21 (2,466.40–2,598.85).
Instructions increased 2.72% versus production (44.435B to 45.642B), while
cycles were nearly unchanged (14.986B versus 14.973B). All guards passed.
The four-way screen is complete. The varied level-1 gain warrants further
investigation, but both mixed-input cases trail production. Before retention,
a separate revision will test reading stored encoded offsets directly instead
of splitting and recombining logical offset/repeat fields. Neither revision
is integrated into production.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Plain-temporary compact varied compression 3 | 2450.71 | 2580.21 | 94.98% | 5.95 | 6.60 | 90.23% | 126.81 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/compact-plain-varied-l3-*`.

### Direct encoded-field compact reads (isolated, 2026-09-27)

This revision keeps plain logical temporaries and 8-byte stored records, but
entropy gathering/emission and partition estimates read the stored encoded
offset directly instead of splitting and recombining offset/repeat fields.
The expanded compact/logical-view sanitizer oracle, optimized unit tests,
and all-level encoding/cross-decoding acceptance passed.

Five CPU-pinned, alternating 8,192-frame mixed-input level-1 trials gave
candidate 2,273.23, previous plain candidate 2,257.99, production 2,353.90,
and reference 2,156.02 MiB/s. That is +0.68% versus the previous candidate
but -3.43% versus production. Ranges overlap: candidate 2,240.42–2,321.97,
plain 2,230.38–2,275.88, production 2,203.95–2,402.15; reference ranged
2,141.75–2,188.54. Instructions decreased to 57.361B versus production
58.443B (-1.85%) and plain 58.705B, but cycles remain above production:
15.964B versus 15.537B (plain 16.141B). All output/allocation guards passed.
This reduces work without yet demonstrating a production throughput gain.
The remaining cases are running; production is unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Direct-field compact mixed compression 1 | 2273.23 | 2156.02 | 105.44% | 5.34 | 6.12 | 87.23% | 126.69 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/compact-direct-mixed-l1-*`.

Varied-text level 1 does not clearly improve on the previous compact revision.
Five 8,192-frame medians are direct candidate 3,106.63, plain candidate
3,160.22, production 3,068.96, and reference 3,435.34 MiB/s. Direct reads are
1.23% above production but 1.70% below plain temporaries. Ranges overlap:
direct 3,095.10–3,155.23, plain 3,114.54–3,195.63, production
3,035.36–3,112.38; reference ranged 3,390.10–3,460.04.
Instructions decrease to 40.045B versus production 40.377B (-0.82%) and
plain 40.108B, while cycles are 11.728B versus 11.819B and 11.572B.
All guards passed. Reduced instruction count has not established an additional
throughput gain; the level-3 cases remain pending.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Direct-field compact varied compression 1 | 3106.63 | 3435.34 | 90.43% | 5.10 | 5.88 | 86.77% | 126.69 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/compact-direct-varied-l1-*`.

Mixed-input level 3 also fails to show a throughput improvement. Five
8,192-frame medians are direct candidate 1,761.73, plain candidate 1,832.43,
production 1,822.36, and reference 1,673.89 MiB/s. Direct reads are 3.33%
below production and 3.86% below plain temporaries. Ranges broadly overlap:
direct 1,727.19–1,880.61, plain 1,573.80–1,877.01, production
1,790.99–1,860.07, reference 1,590.21–1,782.30. No samples were excluded.
Instructions are 61.353B versus production 60.513B (+1.39%) and plain
63.398B; cycles are 20.547B versus 19.697B and 19.769B. All output and
allocation guards passed. Final varied-text level-3 trials remain pending.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Direct-field compact mixed compression 3 | 1761.73 | 1673.89 | 105.25% | 6.25 | 6.73 | 92.97% | 126.81 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/compact-direct-mixed-l3-*`.

The direct-field screen is complete. Final varied-text level-3 medians are
candidate 2,462.37, production 2,474.32, plain candidate 2,457.49, and
reference 2,546.19 MiB/s. Candidate is 0.48% below production; their ranges
overlap (2,426.29–2,481.12 versus 2,432.46–2,482.41). Plain ranged
2,339.99–2,500.42 and reference 2,512.82–2,565.84. Candidate instructions
are 44.849B versus production 44.435B (+0.93%); cycles are 14.783B versus
14.917B. All guards passed. **Do not retain this direct-field revision:** it
does not demonstrate an overall throughput improvement. Production is unchanged.
The next control isolates bitfields using ordinary integer members in the
existing production sequence container, independently of compact storage.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Rejected direct-field compact varied compression 3 | 2462.37 | 2546.19 | 96.71% | 6.01 | 6.55 | 91.77% | 126.81 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/compact-direct-varied-l3-*`.

### Ordinary-field sequence records (isolated, 2026-09-27)

This control changes only production's sequence match-length/repeat-code
bitfields into ordinary integer members. Records grow from 12 to 16 bytes;
the existing vector, views, actual repeat offsets, and consumers are unchanged.
The unchanged optimized unit/acceptance suites and full ASan/UBSan unit suite
passed. This tests bitfield cost independently of compact storage.

Five alternating, CPU-pinned 8,192-frame mixed-input level-1 trials gave
candidate 2,407.99 versus production 2,337.78 MiB/s (+3.00%). Ranges overlap:
candidate 2,277.24–2,442.14 versus production 2,313.85–2,384.49. The compact
plain-temporary control gave 2,298.54 (2,259.57–2,317.53), and reference
2,132.01 (2,108.13–2,168.05). Instructions decreased 4.21% versus production
(58.443B to 55.981B), and cycles fell from 15.515B to 15.307B.
All output/allocation guards passed. This is promising work reduction, but
the throughput gain needs confirmation and remaining workloads are pending.
Production and the main tables remain unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Ordinary-field records mixed compression 1 | 2407.99 | 2132.01 | 112.94% | 5.49 | 6.14 | 89.32% | 122.86 | 628.16 | 19.56% |

Raw results: `out/aligned-strategy-control/plain-record-mixed-l1-*`.

Varied-text level 1 shows reduced work but an uncertain throughput benefit.
Five 8,192-frame medians are candidate 3,100.43, production 3,073.57,
compact control 3,182.52, and reference 3,406.05 MiB/s. Candidate is 0.87%
above production, with broad overlap: 2,991.53–3,128.23 versus
2,984.19–3,103.81. Compact ranged 3,089.32–3,188.57 and reference
3,378.53–3,424.20. Instructions decreased 2.84% (40.377B to 39.231B), and
cycles fell from 11.889B to 11.776B. All guards passed. Ordinary fields
alone have not established a meaningful win on this target workload; both
level-3 cases remain pending and the candidate is not integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Ordinary-field records varied compression 1 | 3100.43 | 3406.05 | 91.03% | 5.12 | 5.87 | 87.23% | 122.86 | 628.16 | 19.56% |

Raw results: `out/aligned-strategy-control/plain-record-varied-l1-*`.

Mixed-input level 3 goes in the wrong direction despite fewer instructions.
Five 8,192-frame medians are candidate 1,719.74, production 1,886.46,
compact control 1,837.70, and reference 1,766.39 MiB/s. Candidate is 8.84%
below production. Ranges overlap broadly: candidate 1,587.39–1,858.24,
production 1,757.53–1,927.57, compact 1,730.19–1,854.42, reference
1,682.10–1,781.34. No samples were excluded. Instructions decrease 3.81%
(60.513B to 58.208B), but cycles rise from 19.299B to 21.303B.
All frame/allocation checks passed. This weakens the case for ordinary fields
as a general optimization; the final varied-text level-3 case is still running.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Ordinary-field records mixed compression 3 | 1719.74 | 1766.39 | 97.36% | 6.25 | 6.73 | 92.87% | 126.81 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/plain-record-mixed-l3-*`.

The ordinary-field screen is complete. Final varied-text level-3 medians are
candidate 2,399.34, production 2,435.92, compact control 2,438.89, and
reference 2,550.78 MiB/s. Candidate is 1.50% below production, with overlapping
ranges (2,328.28–2,432.95 versus 2,340.38–2,445.04). Compact ranged
2,314.16–2,456.97 and reference 2,472.48–2,567.04. Instructions decrease
2.07% (44.435B to 43.514B), but cycles rise from 15.058B to 15.130B.
All guards passed. **The general ordinary-field change is rejected:** it
does not establish a throughput benefit on the varied-text target workloads.
The earlier compact/plain-temporary version's repeated level-1 varied-text
gain will instead receive longer independent confirmation, including its
mixed-input tradeoff. No candidate is integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Rejected ordinary-field records varied compression 3 | 2399.34 | 2550.78 | 94.06% | 6.00 | 6.59 | 91.00% | 126.81 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/plain-record-varied-l3-*`.

### Longer compact/plain level-1 confirmation (2026-09-27)

The independent varied-text confirmation reproduces the compact/plain-temporary
gain. Five reversed-start-order, CPU-pinned 32,768-frame trials gave candidate
3,136.31 versus production 3,022.77 MiB/s (+3.76%). Ranges are disjoint:
candidate 3,121.15–3,188.99 versus production 2,895.74–3,082.27. Reference
gave 3,389.68 (3,328.39–3,420.95), still faster than both. Instructions
decreased 0.67% (161.487B to 160.411B), and cycles fell from 48.009B to
46.103B. All output/allocation guards passed. This confirms a corpus-specific
gain, not the overall goal. The longer mixed-input control is still running;
strategy scoping and additional validation remain necessary before integration.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Confirmed compact/plain varied compression 1 | 3136.31 | 3389.68 | 92.53% | 5.09 | 5.88 | 86.64% | 126.69 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/compact-plain-confirm-varied-l1-*`.

The longer mixed-input confirmation is complete. Five 32,768-frame medians
are candidate 2,248.73, production 2,326.96, and reference 2,154.61 MiB/s.
Candidate is 3.36% below production but 4.37% above reference. Candidate
2,240.48–2,284.15 overlaps production 2,235.46–2,386.20, while reference
2,060.02–2,160.77 is disjoint below. Instructions increase 0.45%
(233.745B to 234.792B), and cycles rise from 62.669B to 64.389B.
All frame/allocation guards passed. The measured tradeoff is a repeated
varied-text improvement with a mixed-input downside, while retaining the
mixed-input reference lead. Carry this design forward only as a fast-strategy
candidate, with scoping, correctness, compiler, and performance revalidation
before integration; do not apply it globally to strategies without a measured gain.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Confirmed compact/plain mixed compression 1 | 2248.73 | 2154.61 | 104.37% | 5.38 | 6.13 | 87.70% | 126.69 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/compact-plain-confirm-mixed-l1-*`.

### Fast-strategy-only compact storage (isolated, 2026-09-27)

The scoped candidate uses compact records/plain logical temporaries only for
the fast strategy. Other strategies retain production's 12-byte records;
levels 3, 15, and 22 have byte-identical machine-code sections to production.
Unchanged GCC unit/all-level acceptance, full ASan/UBSan units, a 152-block
parser/entropy equivalence oracle, and MSVC correctness checks passed.

The scoped implementation preserves the initial varied-text level-1 gain:
five CPU-pinned 16,384-frame trials gave candidate 3,165.51 versus production
3,042.54 MiB/s (+4.04%). Candidate ranged 3,109.65–3,200.82 and production
2,645.51–3,116.88, with a small overlap and a slow production outlier;
no samples were excluded. Reference gave 3,426.77 (3,356.39–3,473.79).
Instructions decreased 0.38% (80.747B to 80.439B), and cycles fell from
23.952B to 23.003B. All frame/allocation guards passed. Mixed level 1 and
both level-2 cases remain pending; the scoped candidate is not integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast-scoped compact varied compression 1 | 3165.51 | 3426.77 | 92.38% | 5.12 | 5.91 | 86.71% | 126.70 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/compact-fast-varied-l1-*`.

The scoped mixed-input level-1 control confirms a downside: candidate
2,240.45 versus production 2,356.29 MiB/s (-4.92%), with disjoint ranges
of 2,150.08–2,281.70 and 2,291.77–2,390.87. Reference gave 2,133.73
(2,087.49–2,176.90), so the candidate median remains 5.00% ahead of reference,
although their ranges overlap. Instructions increased 1.36%
(116.877B to 118.466B), and cycles rose from 30.982B to 32.701B.
All five trials passed output/allocation guards. The varied-text gain therefore
has a real workload tradeoff; level-2 controls remain pending before deciding
whether to retain this scoped design or narrow it further.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast-scoped compact mixed compression 1 | 2240.45 | 2133.73 | 105.00% | 5.38 | 6.14 | 87.48% | 126.70 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/compact-fast-mixed-l1-*`.

Varied-text level 2 does not benefit from the fast-wide scope. Five 16,384-frame
trials gave candidate 3,079.21 versus production 3,168.57 MiB/s (-2.82%).
Ranges narrowly overlap: candidate 2,943.80–3,101.96 versus production
3,099.09–3,190.88. Reference gave 3,431.34 (3,406.17–3,484.15).
Instructions increased 3.29% (76.306B to 78.816B), and cycles rose from
23.007B to 23.817B. All guards passed. This argues against applying compact
storage to every fast configuration: the minMatch-6 level-2 path differs from
level 1's minMatch-7 parser-time literal collection. The final mixed level-2
control remains pending before revising the compile-time policy.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast-scoped compact varied compression 2 | 3079.21 | 3431.34 | 89.74% | 5.24 | 6.12 | 85.58% | 122.16 | 628.16 | 19.45% |

Raw results: `out/aligned-strategy-control/compact-fast-varied-l2-*`.

The mixed level-2 control is complete: candidate 2,682.40 versus production
2,731.70 MiB/s (-1.80%), with overlapping ranges of 2,646.13–2,730.35 and
2,496.79–2,812.48. Reference gave 2,535.32 (2,458.99–2,560.86).
Instructions increased 5.52% (97.091B to 102.446B), and cycles rose from
26.890B to 27.248B. All guards passed. The fast-wide policy will not be
retained. A separate revision restricts compact storage to fast configurations
with minimum match length at least 7; validation must show that it preserves
the measured level-1 implementation and restores production code for level 2.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast-wide compact mixed compression 2 (excluded by revised policy) | 2682.40 | 2535.32 | 105.80% | 5.59 | 6.27 | 89.10% | 122.16 | 628.16 | 19.45% |

Raw results: `out/aligned-strategy-control/compact-fast-mixed-l2-*`.

Integration status: the narrowed fast/minimum-match-at-least-7 policy is now
integrated, with permanent exhaustive length/storage and parser/entropy tests.
Optimized GCC tests, GCC ASan/UBSan, and MSVC correctness checks passed.
The integrated level-1 binary differs from the measured candidate only in ten
calls selecting between equivalent vector-insertion routines; levels 2, 3, 15,
and 22 retain identical machine code. Earlier candidate timings are not treated
as measurements of the integrated binary.

The main evaluation tables were refreshed on 2026-09-27 from the integrated
build: GCC 13 Release `-O3 -DNDEBUG`, CPU2, 2,000 timed iterations per worker.
All 88 measurements and all nine metrics in the 44 table rows were independently
checked, and all 22 compressed outputs match the reference byte for byte.
Raw data: `out/full-metrics-20260927/after-fast7-compact/metrics.csv`.
This single mixed-corpus screening run puts compression ahead at 21 of 22 levels;
level 14 is a near-tie at 99.43% (393.79 versus 396.04 MiB/s), requiring repeated
paired measurement. Decompression is ahead at all 22 levels in this screen.
Level 1 compression is 2,180.44 versus 2,122.34 MiB/s (102.74%). These are not
controlled before/after speedup claims. Earlier varied-text deficits remain
unresolved, so this does not establish the all-level performance goal.

Level-14 follow-up: five alternating CPU2 pairs of 8,192 mixed-corpus frames
put integrated C++ at 464.15 versus reference 417.65 MiB/s (111.13%).
The ranges are disjoint: C++ 456.78–501.87, reference 268.62–434.45.
The slow reference outlier is retained in the range; the median does not depend
on it. All frame comparisons and worker guards passed. This does not reproduce
the single-screen level-14 deficit; no level-14 code change was made.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Integrated mixed compression 14, five-pair median | 464.15 | 417.65 | 111.13% | 21.14 | 21.96 | 96.27% | 122.72 | 628.16 | 19.54% |

Raw results: `out/aligned-strategy-control/integrated-confirm-mixed14-*`.

The integrated build's varied-text deficits remain in five alternating pairs
of 16,384 frames on CPU2. Level-1 ranges are 3,118.45–3,210.92 C++ versus
3,412.47–3,475.49 reference; level-2 ranges overlap at 3,022.17–3,217.56 versus
3,131.01–3,508.12; level-3 ranges are 2,406.15–2,451.89 versus 2,523.71–2,586.01.
All exact-frame and worker guards passed. These actual integrated measurements,
not inherited candidate timings, define the next profiling targets.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Integrated varied compression 1 | 3175.75 | 3451.34 | 92.02% | 5.12 | 5.86 | 87.53% | 126.70 | 628.16 | 20.17% |
| Integrated varied compression 2 | 3200.37 | 3481.40 | 91.93% | 5.21 | 6.11 | 85.24% | 122.49 | 628.16 | 19.50% |
| Integrated varied compression 3 | 2445.16 | 2534.22 | 96.49% | 6.00 | 6.65 | 90.19% | 126.84 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/integrated-confirm-varied*`.

### Fast-only canonical Huffman experiment (isolated, 2026-09-27)

The candidate replaces repeated alphabet scans with per-length starting codes
only in the GCC13 fast-strategy path. Disabled level-3/5/15 machine code matches
production exactly. Sanitized Huffman history comparisons, the scalar code
oracle, and all-level acceptance/cross-decoding passed; it is not integrated.

Five alternating 16,384-frame varied-text level-1 triples measured candidate
3,193.78 versus current C++ 3,185.41 MiB/s (+0.26%), with overlapping ranges
3,146.03–3,208.91 and 3,159.09–3,217.85. Reference measured 3,416.16
(3,414.20–3,429.97). Instructions fell 2.28% (80.439B to 78.602B), but this
does not establish a throughput improvement. Mixed-input and level-2 controls
are still running; no retention decision has been made.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast-only canonical varied compression 1 | 3193.78 | 3416.16 | 93.49% | 5.12 | 5.89 | 87.06% | 126.71 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/fast-canonical-varied-l1-*`.

The remaining five-triple screens are complete. Mixed level 1 is +0.34%
versus current C++ (2,263.38 versus 2,255.66), with overlapping ranges
2,162.27–2,272.85 versus 2,235.06–2,280.06. Varied level 2 is +2.97%
(3,234.91 versus 3,141.57), with narrowly overlapping ranges 3,194.13–3,266.37
versus 3,094.22–3,199.96; instructions fall 2.41% (76.306B to 74.467B).
Mixed level 2 is -1.78% (2,724.85 versus 2,774.27), with broadly overlapping
ranges 2,533.42–2,816.58 versus 2,561.32–2,809.38. All guards passed.
Level 2 warrants longer confirmation of both the varied-text benefit and the
possible mixed-input cost; level 1 has no demonstrated throughput benefit.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast-only canonical mixed compression 1 | 2263.38 | 2167.30 | 104.43% | 5.38 | 6.13 | 87.70% | 126.71 | 628.16 | 20.17% |
| Fast-only canonical varied compression 2 | 3234.91 | 3495.72 | 92.54% | 5.25 | 6.12 | 85.65% | 122.69 | 628.16 | 19.53% |
| Fast-only canonical mixed compression 2 | 2724.85 | 2491.69 | 109.36% | 5.62 | 6.26 | 89.83% | 122.69 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/fast-canonical-{mixed-l1,varied-l2,mixed-l2}-*`.

Longer level-2 varied-text confirmation (five 32,768-frame triples, reversed
starting order) gives candidate 3,219.89 versus current C++ 3,168.38 MiB/s
(+1.63%). Ranges overlap: 3,131.52–3,237.06 versus 3,093.40–3,193.83.
Four paired comparisons improve (+4.64%, +1.63%, +0.88%, +0.61%) and one
declines (-0.69%). Instructions again fall 2.41%, from 152.605B to 148.927B;
cycles fall from 46.167B to 45.389B. This supports a modest benefit but not
an across-the-board win. Longer mixed-input confirmation is still running.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast-only canonical varied compression 2, longer confirmation | 3219.89 | 3460.85 | 93.04% | 5.21 | 6.13 | 84.97% | 122.69 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/fast-canonical-confirm-varied-l2-*`.

The longer mixed-input confirmation is complete: candidate 2,759.56 versus
current C++ 2,658.79 MiB/s (+3.79%), with broadly overlapping ranges
2,535.72–2,772.05 versus 2,471.89–2,796.24. Paired changes were -1.08%, +4.26%,
+8.74%, +0.58%, and +0.15%. Instructions barely changed (-0.07%); the initial
mixed-input decline did not repeat, but these variable timings do not prove a
mixed-input speedup. All frame and worker guards passed. A narrowed candidate
is being validated to retain the measured level-2 path while restoring level 1;
it is not yet integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast-only canonical mixed compression 2, longer confirmation | 2759.56 | 2526.07 | 109.24% | 5.59 | 6.26 | 89.26% | 122.69 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/fast-canonical-confirm-mixed-l2-*`.

The narrowed policy passed optimized unit and all-level acceptance tests.
Its level-2 machine code exactly matches the measured candidate; levels 1, 3,
5, and 15 exactly match the preceding production build. It is now integrated
for the GCC13 fast path without parser-time literal gathering. Permanent tests
exercise both canonical assignment paths at depths 1–11 and compare 4,096
Huffman history transitions. Integrated GCC tests, full unit ASan/UBSan,
MSVC correctness checks, and the all-level rebuild passed. Integrated machine
code at levels 1, 2, 3, 5, and 15 matches the validated narrowed candidate.

The main evaluation tables now reflect the 2026-09-27 post-canonical build:
GCC13 Release `-O3 -DNDEBUG`, CPU2, 2,000 frames per worker. All 88 measurements
and all nine metrics in the 44 table rows were independently verified against
the raw data and README. All 22 compressed frames match reference exactly.
This single screen has compression ahead at 18/22 levels; apparent gaps are
level 5 (88.58%), 7 (96.53%), 13 (93.96%), and 16 (92.90%), requiring paired
confirmation. Decompression is ahead at all 22 levels in this screen. Level-2
compression is 2,802.08 versus 2,551.29 MiB/s (109.83%). These are not controlled
before/after comparisons or proof of regressions in unchanged strategies.
The repeated varied-text deficits remain unresolved.

Raw full-report data: `out/full-metrics-20260927/after-fast-canonical/metrics.csv`.

Paired follow-up of the apparent mixed-corpus gaps uses five alternating
8,192-frame pairs on CPU2. Level 5 remains below reference by median, but with
wide overlapping ranges: C++ 1,671.64–2,260.15 versus reference
1,762.44–2,061.54 MiB/s. Level 7 reverses its screening deficit, with ranges
1,678.45–1,877.82 versus 1,560.42–1,692.85. All frame and worker guards passed.
These variable timings do not prove a regression caused by the canonical change.
Levels 13 and 16 are still being confirmed.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Mixed compression 5, paired confirmation | 1886.11 | 1952.78 | 96.59% | 7.87 | 8.50 | 92.56% | 122.73 | 628.16 | 19.54% |
| Mixed compression 7, paired confirmation | 1818.46 | 1651.48 | 110.11% | 10.20 | 11.02 | 92.62% | 122.73 | 628.16 | 19.54% |

Raw results: `out/aligned-strategy-control/canonical-matrix-gap-l{5,7}-*`.

The remaining confirmations are complete. Level 13's five 8,192-frame pairs
give C++ 413.62–459.67 versus reference 350.80–382.12 MiB/s (disjoint ranges).
Level 16's five 1,024-frame pairs give 82.71–101.12 versus 79.06–88.00
(overlapping ranges). Both reverse the single-screen deficits, without code
changes. All output and worker guards passed. Level 5 remains an uncertain
mixed-input gap; the repeated varied-text gaps are still open.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Mixed compression 13, paired confirmation | 437.61 | 375.66 | 116.49% | 21.21 | 21.98 | 96.52% | 126.72 | 628.16 | 20.17% |
| Mixed compression 16, paired confirmation | 98.38 | 81.75 | 120.35% | 21.45 | 21.86 | 98.12% | 140.56 | 628.16 | 22.38% |

Raw results: `out/aligned-strategy-control/canonical-matrix-gap-l{13,16}-*`.

### Deferred Huffman code assignment (isolated, 2026-09-27)

A diagnostic found 14 of 16 new-code constructions unused when the previous
table was selected on the varied-text warm-up-plus-one-frame runs at levels
1/2/3/5. Mixed input discarded none. The candidate computes weights and validates
the tree before selection, then assigns new codes only if needed. It passed
8,192 sanitized history comparisons, a full unit sanitizer run including explicit
deferred scalar-oracle coverage, and all-level acceptance/cross-decoding. It is
not integrated; disabled level-15 code also changed and needs a timing control.

Five alternating 8,192-frame level-1 triples give varied-text candidate
3,234.16 versus current C++ 3,156.77 MiB/s (+2.45%); ranges overlap at
3,069.67–3,256.06 versus 3,114.61–3,193.39. Instructions fall 2.09%
(40.223B to 39.383B). Mixed input instead gives 2,203.17 versus 2,264.53
(-2.71%), with overlapping ranges 2,176.25–2,236.01 versus 2,129.50–2,285.98;
instructions rise 0.05%. All guards passed. Remaining levels and controls are
still running; the tradeoff requires further evidence before retention.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Deferred canonical varied compression 1 | 3234.16 | 3356.62 | 96.35% | 5.09 | 5.89 | 86.35% | 126.71 | 628.16 | 20.17% |
| Deferred canonical mixed compression 1 | 2203.17 | 2147.64 | 102.59% | 5.37 | 6.12 | 87.69% | 126.71 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/deferred-canonical-{varied,mixed}-l1-*`.

Level 2 adds no clear benefit over its existing rank-based construction:
varied candidate 3,212.29 versus current C++ 3,235.70 MiB/s (-0.72%), ranges
3,174.25–3,273.64 versus 3,159.98–3,263.58; mixed 2,724.54 versus 2,677.47
(+1.76%), ranges 2,615.05–2,786.18 versus 2,446.11–2,783.65. Instructions
change only -0.13% and +0.02%, respectively.

Level 3 is more promising: varied candidate 2,566.62 versus current C++ 2,421.47
(+5.99%), with disjoint ranges 2,532.54–2,605.57 versus 2,400.20–2,477.63.
Instructions fall 2.44% (44.435B to 43.350B). Reference's 2,478.15–2,563.83
range overlaps the candidate, so its 100.84% median ratio is not a confirmed
reference win. Mixed candidate is +2.08% (1,862.23 versus 1,824.30), with
overlapping ranges 1,805.60–1,936.68 versus 1,687.71–1,896.63; instructions
fall 0.87%. All guards passed. Level-5 and level-15 controls remain in progress.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Deferred canonical varied compression 2 | 3212.29 | 3477.00 | 92.39% | 5.24 | 6.11 | 85.81% | 122.50 | 628.16 | 19.50% |
| Deferred canonical mixed compression 2 | 2724.54 | 2524.13 | 107.94% | 5.61 | 6.24 | 89.92% | 122.50 | 628.16 | 19.50% |
| Deferred canonical varied compression 3 | 2566.62 | 2545.21 | 100.84% | 5.99 | 6.65 | 90.08% | 123.08 | 628.16 | 19.59% |
| Deferred canonical mixed compression 3 | 1862.23 | 1761.96 | 105.69% | 6.25 | 6.73 | 92.92% | 123.08 | 628.16 | 19.59% |

Raw results: `out/aligned-strategy-control/deferred-canonical-{varied,mixed}-l{2,3}-*`.

Level-5 controls do not support broad adoption. Five 2,048-frame varied-text
triples give candidate 276.39 versus current C++ 274.62 MiB/s (+0.64%), with
broadly overlapping ranges 226.93–299.02 versus 223.81–284.36; instructions
fall only 0.22%. Five 8,192-frame mixed triples give 2,177.14 versus 2,232.33
(-2.47%), with overlapping ranges 2,139.05–2,228.97 versus 2,177.20–2,252.34;
instructions fall 0.06%. All guards passed. Level 3 remains the strongest
candidate for narrower scoping; the disabled level-15 control is still running.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Deferred canonical varied compression 5 | 276.39 | 232.81 | 118.72% | 7.46 | 8.41 | 88.71% | 122.73 | 628.16 | 19.54% |
| Deferred canonical mixed compression 5 | 2177.14 | 2107.65 | 103.30% | 7.87 | 8.50 | 92.60% | 122.73 | 628.16 | 19.54% |

Raw results: `out/aligned-strategy-control/deferred-canonical-{varied,mixed}-l5-*`.

The disabled level-15 control is complete. Five 512-frame varied triples give
candidate 60.20 versus current C++ 60.84 MiB/s (-1.05%), with overlapping ranges
58.04–60.27 versus 59.34–61.09. Five 4,096-frame mixed triples give 448.10
versus 431.28 (+3.90%), with overlapping ranges 433.98–476.79 versus
410.10–465.14. Instruction counts are effectively unchanged in both cases;
these timings do not establish an algorithmic gain in the disabled path.
All guards passed. The broad candidate is not being integrated. Level 3 will
receive longer, reversed-order confirmation before a narrower policy is tested.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Deferred canonical varied compression 15 control | 60.20 | 50.77 | 118.57% | 20.94 | 21.86 | 95.80% | 122.64 | 628.16 | 19.52% |
| Deferred canonical mixed compression 15 control | 448.10 | 402.74 | 111.26% | 21.19 | 21.97 | 96.43% | 122.64 | 628.16 | 19.52% |

Raw results: `out/aligned-strategy-control/deferred-canonical-{varied,mixed}-l15-*`.

Longer level-3 varied-text confirmation (five 32,768-frame triples with reversed
starting order) reproduces the gain: candidate 2,577.06 versus current C++
2,433.68 MiB/s (+5.89%). Every pair improves (+8.18%, +5.70%, +5.23%, +5.25%,
+6.15%), with disjoint ranges 2,561.07–2,581.90 versus 2,386.63–2,441.21.
Instructions fall 2.44% (177.718B to 173.378B); cycles fall from 60.297B to
56.981B. Reference gives 2,563.34 (2,509.41–2,628.88), overlapping candidate:
100.54% of reference is a near-tie, not a confirmed reference win. All guards
passed. Longer mixed-input confirmation is still running.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Deferred canonical varied compression 3, longer confirmation | 2577.06 | 2563.34 | 100.54% | 6.00 | 6.59 | 90.99% | 123.08 | 628.16 | 19.59% |

Raw results: `out/aligned-strategy-control/deferred-canonical-confirm-varied-l3-*`.

The longer mixed-input confirmation is complete: candidate 1,902.65 versus
current C++ 1,875.57 MiB/s (+1.44%), with overlapping ranges 1,803.51–1,921.59
versus 1,860.97–1,917.76. Instructions fall 0.87% (242.026B to 239.910B).
Reference gives 1,709.51 (1,656.90–1,788.35), below the candidate's range.
All guards passed. A separate double-fast-only candidate is now being validated;
it must preserve the measured implementation or be remeasured before integration.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Deferred canonical mixed compression 3, longer confirmation | 1902.65 | 1709.51 | 111.30% | 6.24 | 6.72 | 92.91% | 123.08 | 628.16 | 19.59% |

Raw results: `out/aligned-strategy-control/deferred-canonical-confirm-mixed-l3-*`.

### Double-fast-only deferred assignment (isolated, 2026-09-27)

The strategy-scoped candidate passes unit and all-level compatibility tests,
but its generated code differs from the earlier broad candidate, so it is
measured separately. Five 8,192-frame varied-text level-3 triples give candidate
2,561.63 versus current C++ 2,452.26 MiB/s (+4.46%), with overlapping ranges
2,404.89–2,606.92 versus 2,389.26–2,470.08. Instructions fall 2.44%.
Reference gives 2,556.71 (2,531.51–2,590.48), making this a near-tie.

Mixed-input level 3 gives candidate 1,821.71 versus current C++ 1,914.45
(-4.84%), with broadly overlapping ranges 1,791.76–1,956.29 versus
1,785.33–1,935.22. Instructions still fall 0.87%, but sampled cycle totals
rise from 19.286B to 20.135B. All guards passed. This mixed-input result needs
confirmation; the earlier broad candidate's timings cannot substitute for it.
Level 4 and unaffected-strategy controls remain in progress. Not integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Double-fast-only deferred varied compression 3 | 2561.63 | 2556.71 | 100.19% | 5.99 | 6.56 | 91.36% | 123.09 | 628.16 | 19.60% |
| Double-fast-only deferred mixed compression 3 | 1821.71 | 1660.76 | 109.69% | 6.25 | 6.73 | 92.81% | 123.09 | 628.16 | 19.60% |

Raw results: `out/aligned-strategy-control/deferred-doublefast-{varied,mixed}-l3-*`.

Level 4, the other default double-fast level, is complete: varied candidate
2,524.33 versus current C++ 2,474.97 MiB/s (+1.99%), with broad overlap
2,295.45–2,613.37 versus 2,465.16–2,531.73. Instructions fall 2.45%
(44.314B to 43.229B), but this screen alone does not establish a speedup.
Mixed candidate is essentially unchanged at 1,793.98 versus 1,794.56 (-0.03%),
with ranges 1,783.79–1,894.08 versus 1,600.83–1,848.20; instructions fall 0.88%.
All output and worker guards passed. Unaffected-strategy controls are still
running; this candidate remains isolated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Double-fast-only deferred varied compression 4 | 2524.33 | 2489.82 | 101.39% | 7.50 | 7.75 | 96.73% | 123.09 | 628.16 | 19.60% |
| Double-fast-only deferred mixed compression 4 | 1793.98 | 1569.64 | 114.29% | 7.71 | 7.99 | 96.53% | 123.09 | 628.16 | 19.60% |

Raw results: `out/aligned-strategy-control/deferred-doublefast-{varied,mixed}-l4-*`.

Disabled fast-strategy controls are complete (five 8,192-frame triples each).
Level-1 varied candidate is +2.57% versus current C++ (3,104.67 versus 3,026.99),
with overlapping ranges 3,079.15–3,111.35 versus 2,946.83–3,098.95. Mixed is
-2.50% (2,154.67 versus 2,209.94), with broad overlap 1,923.78–2,254.86 versus
2,152.37–2,232.26. Level 2 is effectively unchanged: varied +0.24% (3,235.48
versus 3,227.81), ranges 3,133.48–3,246.41 versus 3,110.66–3,264.78; mixed
+0.13% (2,682.90 versus 2,679.53), ranges 2,382.94–2,809.39 versus
2,461.96–2,737.84. Instruction counts are effectively unchanged in all four
controls, so these fluctuations are not claimed as algorithmic gains. All guards
passed. Level-5 and level-15 controls remain in progress.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Double-fast-only varied compression 1 control | 3104.67 | 3305.00 | 93.94% | 5.12 | 5.89 | 86.93% | 126.71 | 628.16 | 20.17% |
| Double-fast-only mixed compression 1 control | 2154.67 | 2142.57 | 100.56% | 5.34 | 6.13 | 87.07% | 126.71 | 628.16 | 20.17% |
| Double-fast-only varied compression 2 control | 3235.48 | 3422.62 | 94.53% | 5.25 | 6.09 | 86.09% | 122.51 | 628.16 | 19.50% |
| Double-fast-only mixed compression 2 control | 2682.90 | 2515.73 | 106.64% | 5.62 | 6.23 | 90.16% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/deferred-doublefast-{varied,mixed}-l{1,2}-*`.

The disabled level-5 control is mixed. Five 2,048-frame varied-text triples give
candidate 252.25 versus current C++ 269.37 MiB/s (-6.36%), with broad overlap
227.57–264.63 versus 213.99–276.52. Instruction counts are unchanged, while
cycles rise from 33.664B to 35.947B; no specific cause is established. Five
8,192-frame mixed triples are neutral at +0.37% (2,202.93 versus 2,194.73),
with ranges 2,102.22–2,226.48 versus 2,133.74–2,246.74. All guards passed.
The varied-text downside remains a retention concern despite the overlapping
ranges. The level-15 control is still running; production remains unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Double-fast-only varied compression 5 control | 252.25 | 239.53 | 105.31% | 7.46 | 8.41 | 88.71% | 122.73 | 628.16 | 19.54% |
| Double-fast-only mixed compression 5 control | 2202.93 | 2067.25 | 106.56% | 7.87 | 8.50 | 92.56% | 122.73 | 628.16 | 19.54% |

Raw results: `out/aligned-strategy-control/deferred-doublefast-{varied,mixed}-l5-*`.

The level-15 controls are now complete and the scoped screen exited successfully.
Five 512-frame varied-text triples give candidate 59.30 versus current C++
59.74 MiB/s (-0.73%), with overlapping ranges 58.54–60.54 versus 57.28–60.43.
Five 4,096-frame mixed triples give 448.82 versus 433.40 (+3.56%), also with
overlap: 425.92–478.01 versus 410.73–472.80. Instruction counts are effectively
unchanged in both controls. These are not claimed as algorithmic gains.
All exact-frame comparisons and worker guards passed. The candidate remains
isolated; longer confirmation of mixed level 3, varied level 5, and mixed
level 1 is required to resolve the screen's apparent costs before retention.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Double-fast-only varied compression 15 control | 59.30 | 51.20 | 115.83% | 20.93 | 21.86 | 95.76% | 122.65 | 628.16 | 19.53% |
| Double-fast-only mixed compression 15 control | 448.82 | 384.85 | 116.62% | 21.18 | 21.97 | 96.41% | 122.65 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/deferred-doublefast-{varied,mixed}-l15-*`.

The longer scoped mixed-level-3 confirmation does not reproduce the screen's
4.84% cost. Five alternating 32,768-frame triples give candidate 1,900.53,
current C++ 1,903.91, and reference 1,745.33 MiB/s: -0.18% versus current
C++, or 108.89% of reference. Candidate and baseline ranges overlap broadly
(1,818.37–1,934.99 versus 1,794.29–1,933.23); the reference range is
1,664.10–1,793.51. Instructions decrease 0.87% (242.026B to 239.910B),
while median cycles are nearly unchanged (76.750B to 76.551B). All exact-frame
comparisons and worker guards passed. This supports neutral mixed-input
behavior, not a mixed-input speedup. Longer level-5 varied and level-1 mixed
controls remain running; the candidate is not integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Double-fast-only deferred mixed compression 3 confirmation | 1900.53 | 1745.33 | 108.89% | 6.25 | 6.73 | 92.81% | 123.09 | 628.16 | 19.60% |

Raw results: `out/aligned-strategy-control/deferred-doublefast-confirm-mixed-l3-*`.

The longer disabled level-5 varied-text control also does not reproduce its
earlier 6.36% cost. Five alternating 8,192-frame triples give candidate 288.85,
current C++ 279.89, and reference 256.06 MiB/s (+3.20% versus current C++,
112.80% of reference). However, ranges remain broad and overlapping:
candidate 254.75–317.24, baseline 267.50–318.44, reference 242.92–264.35.
Instruction counts are unchanged at 333.071B; median cycles are 127.273B
versus baseline 130.959B. This is evidence against a repeatable regression,
not evidence of an algorithmic improvement in this disabled strategy. All
exact-frame comparisons and worker guards passed. The remaining mixed-level-1
control is running; the candidate is still isolated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Double-fast-only varied compression 5 control confirmation | 288.85 | 256.06 | 112.80% | 7.50 | 8.36 | 89.71% | 122.73 | 628.16 | 19.54% |

Raw results: `out/aligned-strategy-control/deferred-doublefast-confirm-varied-l5-*`.

The final mixed-level-1 control is neutral: five 32,768-frame triples give
candidate 2,259.10 versus current C++ 2,262.91 MiB/s (-0.17%), with overlapping
ranges 2,236.74–2,279.22 versus 2,209.76–2,296.45. Reference is 2,160.22
(range 2,107.16–2,169.12). Instructions are effectively unchanged; median
cycles are 64.927B versus baseline 64.596B. All guards passed and the full
confirmation run exited successfully. None of the three screened downsides
repeated in these longer controls. The candidate is still isolated: removing
duplicated assignment logic requires fresh validation and machine-code checks
before these measurements can be applied to the cleaned-up implementation.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Double-fast-only mixed compression 1 control confirmation | 2259.10 | 2160.22 | 104.58% | 5.37 | 6.12 | 87.74% | 126.71 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/deferred-doublefast-confirm-mixed-l1-*`.

### Cleaned deferred-code candidate controls

The duplicate early/late canonical assignment bodies have been consolidated in
the isolated candidate. Optimized GCC units/all-level acceptance, full
ASan/UBSan units, 8,192 sanitized history transitions, and MSVC units/all-level
acceptance pass. Levels 3 and 4 retain identical `.text` to the measured scoped
candidate; levels 1, 2, 5, and 15 are being remeasured because their code changed.

The first completed control, varied level 1 (five 16,384-frame triples), gives
candidate 3,160.83 versus current C++ 3,135.15 MiB/s (+0.82%). Ranges overlap:
3,047.46–3,191.31 versus 3,103.88–3,179.59. Instructions are effectively unchanged;
this is not a demonstrated speedup. Reference remains faster at 3,411.88 MiB/s
(range 3,286.21–3,442.03). All exact-frame and worker guards passed. Remaining
controls are running; production is unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Clean deferred varied compression 1 control | 3160.83 | 3411.88 | 92.64% | 5.12 | 5.89 | 86.93% | 126.71 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/deferred-clean-varied-l1-*`.

The cleaned mixed-level-1 control also shows no clear regression. Five
16,384-frame triples give candidate 2,275.81 versus current C++ 2,244.71 MiB/s
(+1.39%), with broad overlap: 2,161.83–2,290.19 versus 2,222.40–2,288.25.
Reference is 2,147.47 (range 2,125.69–2,174.84). Instructions are effectively
unchanged; median cycles are 32.337B versus baseline 32.459B. No algorithmic
gain is claimed on this disabled path. All exact-frame and worker guards passed.
Levels 2, 5, and 15 remain in progress; the candidate is not integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Clean deferred mixed compression 1 control | 2275.81 | 2147.47 | 105.98% | 5.37 | 6.12 | 87.63% | 126.71 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/deferred-clean-mixed-l1-*`.

The cleaned varied-level-2 control gives candidate 3,223.01 versus current C++
3,260.28 MiB/s (-1.14%) across five 16,384-frame triples. Ranges overlap
broadly: candidate 3,084.51–3,273.70 versus baseline 3,113.21–3,273.08.
Instructions are effectively unchanged (74.465B versus 74.467B), and median
cycles are 22.578B versus 22.656B. This does not establish a regression or gain.
Reference remains faster at 3,465.28 (range 3,382.70–3,473.36). All exact-frame
and worker guards passed; mixed level 2 and the level-5/15 controls continue.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Clean deferred varied compression 2 control | 3223.01 | 3465.28 | 93.01% | 5.21 | 6.12 | 85.08% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/deferred-clean-varied-l2-*`.

Mixed level 2 raises a new retention concern for the cleanup: five 16,384-frame
triples give candidate 2,512.01 versus current C++ 2,714.20 MiB/s (-7.45%),
or 98.58% of reference. Ranges overlap (candidate 2,446.88–2,801.48 versus
baseline 2,578.51–2,813.38), but median cycles increase from 27.033B to 28.940B
despite effectively unchanged instructions (97.019B). No cause is established.
All exact-frame and worker guards passed. Do not integrate this cleaned variant
on the strength of the earlier variant's neutral controls; level-5/15 controls
are still running and this cost must be resolved first.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Clean deferred mixed compression 2 control | 2512.01 | 2548.28 | 98.58% | 5.61 | 6.24 | 89.92% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/deferred-clean-mixed-l2-*`.

The cleaned varied-level-5 control also shows an apparent cost. Five 4,096-frame
triples give candidate 256.44 versus current C++ 271.94 MiB/s (-5.70%), with
overlapping ranges 255.61–286.18 versus 260.78–285.78. Instructions are unchanged
at 166.557B, while median cycles rise from 67.432B to 71.322B. Reference is
249.27 (range 217.55–259.21); all exact-frame and worker guards passed.
Given the new level-2 and level-5 downside signals, this cleanup is not selected
for integration. They are not proven causal regressions, but the cleanup has
not established a benefit over the previously measured scoped version. After
remaining controls finish, return to that version and validate its exact code;
do not discard the double-fast optimization itself based on these control cases.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Clean deferred varied compression 5 control | 256.44 | 249.27 | 102.88% | 7.50 | 8.38 | 89.46% | 122.66 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/deferred-clean-varied-l5-*`.

The cleaned mixed-level-5 control is neutral: five 16,384-frame triples give
candidate 2,191.27 versus current C++ 2,200.28 MiB/s (-0.41%), with overlapping
ranges 2,114.13–2,233.92 versus 1,905.33–2,235.61. Reference is 2,082.82
(range 2,002.30–2,120.64). Instructions are unchanged at 120.119B; median
cycles are 33.466B versus baseline 33.155B. All exact-frame and worker guards
passed. This does not change the decision to prefer the previously measured
scoped version; level-15 cleanup controls are still running.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Clean deferred mixed compression 5 control | 2191.27 | 2082.82 | 105.21% | 7.87 | 8.50 | 92.51% | 122.66 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/deferred-clean-mixed-l5-*`.

The cleaned varied-level-15 control is neutral: five 512-frame triples give
candidate 60.21 versus current C++ 60.37 MiB/s (-0.26%), with overlapping
ranges 59.54–60.63 versus 58.37–61.23. Reference is 51.58 (range 51.19–52.24).
Instructions are effectively unchanged at 130.532B; median cycles are 38.097B
versus baseline 37.973B. All exact-frame and worker guards passed. Only the
mixed-level-15 control remains; the cleanup is still not selected for integration.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Clean deferred varied compression 15 control | 60.21 | 51.58 | 116.72% | 20.94 | 21.88 | 95.70% | 122.65 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/deferred-clean-varied-l15-*`.

The last cleanup control, mixed level 15, gives candidate 426.44 versus current
C++ 437.29 MiB/s (-2.48%) across five 4,096-frame triples. Ranges overlap:
374.27–456.97 versus 418.34–441.05; reference is 369.98 (358.96–416.69).
Instructions are effectively unchanged at 66.073B; cycles are 43.461B versus
baseline 42.072B. All guards passed and the complete control run exited
successfully. The cleanup is not retained. The prior scoped source has been
restored byte-for-byte in the isolated candidate and is undergoing exact-version
validation; production is unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Clean deferred mixed compression 15 control | 426.44 | 369.98 | 115.26% | 21.19 | 21.99 | 96.36% | 122.65 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/deferred-clean-mixed-l15-*`.

### Integrated deferred Huffman code construction

Retained the measured double-fast-only version, preserving the original eager
assignment expression for other strategies. New canonical codes are constructed
only if the new tree wins selection; the repeat-tree case avoids discarded work.
Optimized GCC tests, full ASan/UBSan units, MSVC correctness tests, custom
double-fast strategy overrides, and scalar canonical-code checks pass. Rebuilt
levels 1, 2, 3, 4, 5, and 15 match the measured candidate's machine code exactly.

The refreshed GCC13 Release mixed-corpus evaluation (2,000 iterations, CPU 2)
shows compression and decompression ahead of reference at all 22 levels. The
generated tables above contain all nine metrics. All 88 raw measurements and
44 table rows were independently checked, and all 22 compressed frames match
reference byte-for-byte. Compression ranges from 102.95% to 164.43% of reference;
decompression ranges from 108.97% to 172.11% in this run.

This is a single-corpus screen, not proof of universal superiority or a controlled
before/after gain at every level. Varied-text levels 1 and 2 still trail reference
in the controlled measurements above, and the double-fast varied-text result is
near a tie with reference. These remain optimization targets; the overall goal
is not complete.

Raw report: `out/full-metrics-20260927/after-deferred-doublefast/metrics.csv`.

### Validated branch-free FSE transition experiment

An isolated candidate uses ordinary zero-bit FSE transitions for RLE tables,
matching the reference representation, instead of checking the RLE marker for
every sequence symbol. The generic checked API remains unchanged. Independent
ASan/UBSan transition/RLE-reuse tests, full units, and all-level acceptance pass.

The first completed performance case is a loss: six 16,384-frame triples using
all six baseline/candidate/reference run orders give varied-level-1 candidate
3,071.57 versus current C++ 3,214.08 MiB/s (-4.43%). Candidate range
3,039.22–3,134.46 is below baseline 3,153.68–3,241.83. Instructions increase
0.81% (80.442B to 81.091B), and median cycles increase from 22.797B to 23.760B.
Removing branches did not reduce total work in this case. All exact-frame and
worker guards passed. Mixed level 1 and both level-2 cases remain running;
the candidate is not integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Branchless FSE varied compression 1 | 3071.57 | 3423.04 | 89.73% | 5.12 | 5.85 | 87.51% | 126.71 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/branchless-fse-varied-l1-*`.

Mixed level 1 also regresses: six balanced 16,384-frame triples give candidate
2,066.90 versus current C++ 2,224.68 MiB/s (-7.09%). Candidate range
2,035.41–2,086.29 is entirely below baseline 2,209.99–2,294.49. Instructions
increase 8.40% (118.467B to 128.417B), and cycles rise from 32.755B to 35.288B.
All exact-frame and worker guards pass, but this candidate is rejected for
level 1. The level-2 cases are still running; production remains unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Branchless FSE mixed compression 1 | 2066.90 | 2138.17 | 96.67% | 5.37 | 6.13 | 87.58% | 126.71 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/branchless-fse-mixed-l1-*`.

Varied level 2 is effectively neutral: six balanced 16,384-frame triples give
candidate 3,162.64 versus current C++ 3,153.11 MiB/s (+0.30%), with overlapping
ranges 3,107.46–3,200.88 versus 3,031.50–3,240.17. Instructions increase 1.26%
(74.468B to 75.402B), and cycles increase slightly from 22.844B to 22.948B.
Reference remains faster at 3,350.56 (range 3,175.68–3,388.04). All exact-frame
and worker guards pass. No benefit is established; only mixed level 2 remains.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Branchless FSE varied compression 2 | 3162.64 | 3350.56 | 94.39% | 5.24 | 6.12 | 85.72% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/branchless-fse-varied-l2-*`.

The final mixed-level-2 case loses 11.32%: candidate 2,342.96 versus current C++
2,641.99 MiB/s across six balanced 16,384-frame triples. Ranges overlap
(2,289.00–2,548.64 versus 2,348.34–2,700.48), but instructions increase 5.44%
(97.018B to 102.293B), and cycles rise from 26.915B to 30.321B. All guards pass
and the experiment completed successfully. The candidate is rejected: no
demonstrated varied-text gain offsets its mixed-input costs, and level 1 loses
on both corpora. Production retains the existing RLE shortcuts.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Branchless FSE mixed compression 2 | 2342.96 | 2398.88 | 97.67% | 5.58 | 6.23 | 89.68% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/branchless-fse-mixed-l2-*`.

### Block-level FSE mode specialization experiment

The next isolated variant selects specialized loops once per block for the
observed RLE combinations, skipping RLE transitions entirely and retaining a
checked fallback. All-eight-mode scalar comparisons under ASan/UBSan, units,
and all-level acceptance pass. This avoids the previous experiment's extra
RLE table lookups, but does not automatically improve generated code.

Six balanced 16,384-frame varied-level-1 triples give candidate 3,010.83 versus
current C++ 3,139.01 MiB/s (-4.08%). Candidate range 2,976.69–3,032.71 is below
baseline 3,125.94–3,186.88. Instructions increase 1.27% (80.442B to 81.464B),
and cycles rise from 22.874B to 23.800B. All exact-frame and worker guards pass.
Mixed level 1 and both level-2 cases are still running. Not integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block-dispatch FSE varied compression 1 | 3010.83 | 3336.50 | 90.24% | 5.12 | 5.90 | 86.82% | 124.70 | 628.16 | 19.85% |

Raw results: `out/aligned-strategy-control/fse-dispatch-varied-l1-*`.

Mixed level 1 shows a provisional benefit: six balanced 16,384-frame triples
give candidate 2,218.34 versus current C++ 2,156.07 MiB/s (+2.89%), with overlapping
ranges 2,146.31–2,257.18 versus 2,047.31–2,198.04. Instructions decrease 4.99%
(118.467B to 112.554B), and cycles fall from 32.947B to 32.329B. All exact-frame
and worker guards pass. This does not offset the established varied-text loss;
the variant remains unintegrated while level-2 controls run.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block-dispatch FSE mixed compression 1 | 2218.34 | 2084.45 | 106.42% | 5.35 | 6.10 | 87.74% | 124.70 | 628.16 | 19.85% |

Raw results: `out/aligned-strategy-control/fse-dispatch-mixed-l1-*`.

Varied level 2 reduces instructions but has no established speedup: six balanced
16,384-frame triples give candidate 3,130.07 versus current C++ 3,095.56 MiB/s
(+1.11%), with broad overlapping ranges 3,065.02–3,310.88 versus
2,935.04–3,284.70. Instructions decrease 2.26% (74.468B to 72.788B), and cycles
fall from 23.131B to 22.894B. Reference remains faster at 3,340.72. All exact-frame
and worker guards pass. Mixed level 2 remains running; the level-1 varied-text
regression still prevents retaining this variant unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block-dispatch FSE varied compression 2 | 3130.07 | 3340.72 | 93.69% | 5.22 | 6.12 | 85.33% | 123.48 | 628.16 | 19.66% |

Raw results: `out/aligned-strategy-control/fse-dispatch-varied-l2-*`.

Mixed level 2 reduces instructions but not elapsed time: six balanced
16,384-frame triples give candidate 2,637.32 versus current C++ 2,759.97 MiB/s
(-4.44%), with overlapping ranges 2,552.23–2,737.06 versus 2,429.45–2,815.50.
Instructions fall 2.64% (97.018B to 94.460B), while cycles increase from 26.504B
to 27.495B. All guards pass and the run completed successfully. This variant
is not retained: the mixed-level-1 benefit does not justify the established
varied-level-1 loss and unresolved level-2 cost. Production remains unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block-dispatch FSE mixed compression 2 | 2637.32 | 2534.09 | 104.07% | 5.62 | 6.25 | 89.90% | 123.48 | 628.16 | 19.66% |

Raw results: `out/aligned-strategy-control/fse-dispatch-mixed-l2-*`.

#### FSE loop inlining diagnostic (not retained)

Forcing the experimental block-dispatch loop inline removes captured-view
pointer indirection and the alternate-storage check from the generated compact
loop, but does not improve varied-text level 1. Six balanced 16,384-frame
baseline/candidate/reference triples on CPU 2 give 3,182.18/3,023.85/3,406.60
MiB/s. The candidate is 4.98% slower than current C++; ranges do not overlap
(baseline 3,109.48–3,233.31, candidate 2,999.16–3,093.91).
Instructions increase 2.27% (80.442B to 82.267B), and cycles increase from
22.945B to 24.050B. All frame and allocation guards pass. Sanitizer mode-oracle,
unit, and all-level cross-decoding checks also passed. Production is unchanged;
the remaining mixed/level-2 measurements are pending.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Inlined-dispatch FSE varied compression 1 | 3023.85 | 3406.60 | 88.76% | 5.12 | 5.87 | 87.25% | 126.88 | 628.16 | 20.20% |

Raw results: `out/aligned-strategy-control/fse-inline-varied-l1-*`.

Mixed level 1 also rejects forced loop inlining: six balanced 16,384-frame
triples give current C++ 2,236.77 versus candidate 2,105.84 MiB/s (-5.85%).
Ranges are disjoint: baseline 2,185.06–2,248.17, candidate 2,099.05–2,135.52.
Instructions decrease 2.88% (118.467B to 115.059B), but cycles increase from
32.717B to 34.565B. All frame/allocation guards pass. The prior outlined
dispatch's mixed-level-1 benefit does not survive this inlining change;
production remains unchanged. Level-2 measurements are still pending.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Inlined-dispatch FSE mixed compression 1 | 2105.84 | 2135.65 | 98.60% | 5.34 | 6.12 | 87.20% | 126.88 | 628.16 | 20.20% |

Raw results: `out/aligned-strategy-control/fse-inline-mixed-l1-*`.

Varied-text level 2 likewise shows no gain: six balanced 16,384-frame triples
give baseline 3,215.86 versus candidate 3,113.19 MiB/s (-3.19%). Ranges overlap
(baseline 3,076.24–3,249.02, candidate 3,084.12–3,143.11); instructions rise
0.98% (74.468B to 75.197B) and cycles rise from 22.766B to 23.523B. All frame
and allocation guards pass. Only mixed level 2 remains pending.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Inlined-dispatch FSE varied compression 2 | 3113.19 | 3448.91 | 90.27% | 5.24 | 6.15 | 85.15% | 122.59 | 628.16 | 19.52% |

Raw results: `out/aligned-strategy-control/fse-inline-varied-l2-*`.

The final mixed-level-2 case completes the rejection: baseline 2,638.92 versus
candidate 2,532.42 MiB/s (-4.04%), with overlapping ranges 2,504.89–2,776.20
and 2,400.36–2,705.03. Instructions fall 2.21% (97.018B to 94.876B), but cycles
increase from 27.479B to 28.817B. All guards pass and the benchmark completed
successfully. No case establishes a benefit; this inlining variant is not retained.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Inlined-dispatch FSE mixed compression 2 | 2532.42 | 2498.90 | 101.34% | 5.61 | 6.25 | 89.81% | 122.59 | 628.16 | 19.52% |

Raw results: `out/aligned-strategy-control/fse-inline-mixed-l2-*`.

#### Constant-symbol histogram bypass experiment

An isolated candidate tests equality of adjacent code spans before constructing
a histogram for streams of at least 128 symbols. Constant streams select the
same RLE table directly; other streams retain the existing histogram path.
Sanitizer differential tests, units, and all-level acceptance checks pass.
Six balanced 16,384-frame varied-level-1 triples show no established benefit:
baseline 3,184.94 versus candidate 3,169.49 MiB/s (-0.49%), with overlapping
ranges 3,128.38–3,196.64 and 3,070.03–3,222.15. Instructions fall 0.50%
(80.442B to 80.036B), while cycles rise from 22.899B to 23.070B. All frame and
allocation guards pass. Other cases are pending; production is unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| RLE histogram bypass varied compression 1 | 3169.49 | 3445.82 | 91.98% | 5.12 | 5.85 | 87.55% | 126.72 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/rle-probe-varied-l1-*`.

Mixed level 1 is promising: six balanced 16,384-frame triples improve from
2,261.61 to 2,325.32 MiB/s (+2.82%). Ranges are disjoint: baseline
2,181.60–2,280.93, candidate 2,312.98–2,352.70. Instructions fall 1.66%
(118.467B to 116.497B), and cycles fall from 32.392B to 31.383B. All frame and
allocation guards pass. This is an initial controlled gain, not yet an
integrated optimization: independent confirmation and level-2 controls remain.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| RLE histogram bypass mixed compression 1 | 2325.32 | 2120.28 | 109.67% | 5.38 | 6.14 | 87.48% | 126.72 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/rle-probe-mixed-l1-*`.

Varied level 2 shows a modest, unconfirmed median improvement: baseline
3,227.72 versus candidate 3,261.38 MiB/s (+1.04%). Six balanced 16,384-frame
triples have overlapping ranges (3,123.35–3,267.55 versus 3,126.70–3,298.11).
Instructions fall 0.54% (74.468B to 74.062B) and cycles fall from 22.849B to
22.428B. All frame/allocation guards pass; mixed level 2 remains pending.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| RLE histogram bypass varied compression 2 | 3261.38 | 3465.39 | 94.11% | 5.20 | 6.11 | 85.08% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/rle-probe-varied-l2-*`.

Mixed level 2 is a warning against integrating this candidate yet: baseline
2,745.25 versus candidate 2,561.31 MiB/s (-6.70%). Six balanced 16,384-frame
triples show broadly overlapping ranges (2,522.29–2,799.76 versus
2,441.66–2,818.93). Instructions fall 1.11% (97.018B to 95.937B), but cycles
increase from 26.551B to 28.638B. All guards pass and the initial benchmark
completed successfully. A longer independent confirmation will test whether
the mixed-level-1 gain and mixed-level-2 loss persist; production is unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| RLE histogram bypass mixed compression 2 | 2561.31 | 2513.52 | 101.90% | 5.62 | 6.26 | 89.77% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/rle-probe-mixed-l2-*`.

The independent mixed-level-2 confirmation does **not** reproduce the initial
slowdown. Six balanced 32,768-frame triples, with reversed case/trial order
and unchanged binaries, give baseline 2,618.85 versus candidate 2,700.09 MiB/s
(+3.10%). Ranges overlap broadly (2,509.97–2,831.13 versus 2,524.14–2,898.02),
so the throughput effect remains uncertain, not a confirmed gain. Instruction
savings repeat at 1.11% (194.029B to 191.866B); cycles fall from 55.101B to
53.681B. All output and allocation guards pass. Other confirmation cases are
still running; no production integration has occurred.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| RLE bypass confirmation mixed compression 2 | 2700.09 | 2442.20 | 110.56% | 5.62 | 6.26 | 89.77% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/rle-probe-confirm-mixed-l2-*`.

Varied-level-2 confirmation repeats the small median improvement, not a
statistically established speedup: baseline 3,060.95 versus candidate
3,088.13 MiB/s (+0.89%) across six balanced 32,768-frame triples. Ranges
overlap (3,023.52–3,096.53 versus 2,937.70–3,149.14). Instructions again fall
0.54% (148.929B to 148.118B), and cycles fall from 46.561B to 45.685B. All
frame/allocation guards pass. Level-1 confirmation remains in progress.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| RLE bypass confirmation varied compression 2 | 3088.13 | 3291.67 | 93.82% | 5.24 | 6.12 | 85.52% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/rle-probe-confirm-varied-l2-*`.

Mixed-level-1 confirmation repeats the favorable median: baseline 2,168.19
versus candidate 2,234.51 MiB/s (+3.06%, initially +2.82%) across six balanced
32,768-frame triples. This run has overlapping ranges (2,106.19–2,199.03
versus 1,939.77–2,275.81), unlike the disjoint initial screen. Instructions
again fall 1.66% (236.926B to 232.985B), and cycles fall from 65.686B to
63.633B. All frame/allocation guards pass. The repeatable median direction
supports further validation, but does not eliminate timing variability;
varied-level-1 confirmation is still pending and production is unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| RLE bypass confirmation mixed compression 1 | 2234.51 | 2064.94 | 108.21% | 5.37 | 6.13 | 87.57% | 126.72 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/rle-probe-confirm-mixed-l1-*`.

The final varied-level-1 confirmation gives baseline 3,155.17 versus candidate
3,098.41 MiB/s (-1.80%, initially -0.49%). Six balanced 32,768-frame triples
have overlapping ranges (3,035.49–3,183.77 versus 2,921.04–3,143.93).
Instructions fall 0.50% (160.876B to 160.065B), but cycles rise from 45.716B
to 46.312B. All guards pass and confirmation is complete. The candidate is
not retained: its mixed-level-1 benefit does not improve the remaining
varied-level-1 deficit, and both varied-level-1 median comparisons are slower.
This is a conservative retention decision, not a claim of a statistically
established regression. Production remains unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| RLE bypass confirmation varied compression 1 | 3098.41 | 3321.94 | 93.27% | 5.12 | 5.87 | 87.32% | 126.72 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/rle-probe-confirm-varied-l1-*`.

#### Interleaved four-stream Huffman encoder experiment

An isolated prototype interleaves symbol packing across four independent
bit containers, retaining the existing bounds, per-symbol validation, and
end markers. Its sanitizer oracle (12,432 byte/reuse cases plus 16 invalid-code
cases), units, and all-level cross-decoding acceptance checks pass.
The first performance case rejects it for varied level 1: six balanced
16,384-frame triples give baseline 3,187.70 versus candidate 3,054.31 MiB/s
(-4.18%). Ranges are disjoint (3,169.06–3,229.22 versus 2,902.84–3,098.98).
Instructions increase 2.05% (80.442B to 82.094B), and cycles increase from
22.818B to 23.817B. All output/allocation guards pass. Remaining cases are
pending; production is unchanged and no speedup is claimed.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Interleaved Huffman varied compression 1 | 3054.31 | 3390.40 | 90.09% | 5.12 | 5.89 | 86.96% | 130.88 | 628.16 | 20.84% |

Raw results: `out/aligned-strategy-control/huffman-interleave-varied-l1-*`.

Mixed level 1 provides no offsetting benefit: six balanced 16,384-frame
triples give baseline 2,225.01 versus candidate 2,204.25 MiB/s (-0.93%).
Ranges overlap (2,192.85–2,261.67 versus 2,132.73–2,265.11). Instructions are
essentially unchanged (+0.04%, 118.467B to 118.511B); cycles rise from
32.730B to 32.890B. All frame/allocation guards pass. The current prototype
is not a retention candidate for level 1; level-2/3 measurements continue.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Interleaved Huffman mixed compression 1 | 2204.25 | 2105.02 | 104.71% | 5.37 | 6.14 | 87.47% | 130.88 | 628.16 | 20.84% |

Raw results: `out/aligned-strategy-control/huffman-interleave-mixed-l1-*`.

Varied level 2 is inconclusive: six balanced 16,384-frame triples give baseline
3,196.80 versus candidate 3,220.81 MiB/s (+0.75%), with overlapping ranges
(3,109.16–3,242.54 versus 3,153.30–3,239.87). Instructions increase 2.21%
(74.468B to 76.116B); cycles decrease slightly from 22.937B to 22.726B.
All frame/allocation guards pass. This is not an established throughput gain.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Interleaved Huffman varied compression 2 | 3220.81 | 3443.47 | 93.53% | 5.23 | 6.14 | 85.12% | 126.68 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/huffman-interleave-varied-l2-*`.

Mixed level 2 has a favorable but unconfirmed median: baseline 2,583.88 versus
candidate 2,795.82 MiB/s (+8.20%) across six balanced 16,384-frame triples.
Ranges overlap broadly (2,455.15–2,777.81 versus 2,525.94–2,835.10).
Instructions are essentially unchanged (+0.05%, 97.018B to 97.069B), while
cycles fall from 28.476B to 26.190B. All frame/allocation guards pass.
This does not offset the established varied-level-1 loss or establish an
across-level optimization; level-3 controls remain in progress.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Interleaved Huffman mixed compression 2 | 2795.82 | 2539.40 | 110.10% | 5.59 | 6.24 | 89.52% | 126.68 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/huffman-interleave-mixed-l2-*`.

Varied level 3 is unfavorable: six balanced 16,384-frame triples give baseline
2,494.17 versus candidate 2,400.52 MiB/s (-3.75%). Ranges overlap
(2,326.61–2,539.26 versus 2,296.22–2,435.57), while instructions increase
1.88% (86.693B to 88.321B) and cycles increase from 29.231B to 30.422B.
All output/allocation guards pass. Mixed level 3 is the last pending case;
the prototype has not demonstrated a benefit on the varied-data deficits.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Interleaved Huffman varied compression 3 | 2400.52 | 2532.17 | 94.80% | 6.00 | 6.61 | 90.79% | 127.02 | 628.16 | 20.22% |

Raw results: `out/aligned-strategy-control/huffman-interleave-varied-l3-*`.

Mixed level 3 closes the screen without an established benefit: baseline
1,814.01 versus candidate 1,820.56 MiB/s (+0.36%) across six balanced
16,384-frame triples. Ranges overlap broadly (1,688.03–1,908.82 versus
1,730.27–1,865.30); instructions are unchanged to practical precision
(119.960B to 119.973B, +0.01%), and cycles are 39.922B versus 39.471B.
All frame/allocation guards pass and the run completed successfully.
The current interleaved prototype is not retained: it loses on varied level 1
and provides no confirmed benefit on the other remaining varied-data gaps.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Interleaved Huffman mixed compression 3 | 1820.56 | 1575.90 | 115.52% | 6.25 | 6.73 | 92.95% | 127.02 | 628.16 | 20.22% |

Raw results: `out/aligned-strategy-control/huffman-interleave-mixed-l3-*`.

#### Shared-index four-stream Huffman revision

A follow-up aligns the unequal stream suffixes before the bulk loop, giving
all four streams one source index. Sanitizer byte/reuse checks, units, and
all-level acceptance pass; assembly confirms the shared index. Eight balanced
8,192-frame four-way trials compare current production, the previous interleaved
prototype, this revision, and reference. On varied level 1, medians are
3,147.39 / 2,989.04 / 3,008.58 / 3,393.85 MiB/s respectively. The revision
is 4.41% below production and only 0.65% above the prior prototype; ranges
overlap (production 3,003.20–3,213.24, revision 2,750.40–3,139.75).
It removes 1.05% of the prior prototype's instructions (41.050B to 40.618B),
but remains 0.98% above production's 40.224B. Cycles are 11.437B production,
11.955B previous, and 11.842B revised. All frame/allocation guards pass.
This does not establish a useful speedup; other cases are pending and
production is unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Shared-index Huffman varied compression 1 | 3008.58 | 3393.85 | 88.65% | 5.09 | 5.87 | 86.66% | 130.88 | 628.16 | 20.84% |

Raw results: `out/aligned-strategy-control/huffman-shared-index-varied-l1-*`.

Mixed level 1 also shows no benefit from the revision: eight balanced
8,192-frame four-way trials give production / previous / revised / reference
medians of 2,221.87 / 2,196.40 / 2,178.87 / 2,098.13 MiB/s. The revision is
1.94% below production, with overlapping ranges (production
1,924.59–2,271.81, revised 2,066.98–2,236.08). Instructions are effectively
unchanged: 59.238B production, 59.260B previous, 59.259B revised. Cycles are
16.180B / 16.421B / 16.550B respectively. All frame/allocation guards pass;
level-2/3 controls are still running and production remains unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Shared-index Huffman mixed compression 1 | 2178.87 | 2098.13 | 103.85% | 5.38 | 6.13 | 87.65% | 130.88 | 628.16 | 20.84% |

Raw results: `out/aligned-strategy-control/huffman-shared-index-mixed-l1-*`.

Varied level 2 is flat versus production: eight balanced 8,192-frame four-way
trials give production / previous / revised / reference medians of
3,117.68 / 3,090.19 / 3,119.10 / 3,362.87 MiB/s (+0.05% revised versus
production). Ranges overlap (production 3,099.51–3,157.01, revised
2,892.06–3,142.77). Instructions are 37.237B / 38.061B / 37.631B respectively;
the revision removes work versus the previous prototype but remains 1.06%
above production. Cycles are 11.483B / 11.476B / 11.529B. All guards pass.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Shared-index Huffman varied compression 2 | 3119.10 | 3362.87 | 92.75% | 5.23 | 6.12 | 85.42% | 126.68 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/huffman-shared-index-varied-l2-*`.

Mixed level 2 has a favorable, provisional median: production / previous /
revised / reference throughput is 2,570.98 / 2,629.87 / 2,697.24 / 2,452.40
MiB/s across eight balanced 8,192-frame trials (+4.91% revised versus
production). All C++ ranges overlap: 2,313.69–2,743.60 / 2,395.56–2,737.44 /
2,433.30–2,753.11. Instructions are 48.513B / 48.538B / 48.538B, and cycles
are 13.925B / 13.435B / 13.245B respectively. All guards pass. The broad
overlap and lack of varied-data gains still prevent a retention claim;
level-3 controls remain in progress.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Shared-index Huffman mixed compression 2 | 2697.24 | 2452.40 | 109.98% | 5.61 | 6.24 | 89.83% | 126.68 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/huffman-shared-index-mixed-l2-*`.

Varied level 3 remains unfavorable. Eight balanced 8,192-frame four-way
trials give production / previous / revised / reference medians of
2,497.05 / 2,402.08 / 2,423.48 / 2,515.43 MiB/s (-2.95% revised versus
production). Ranges overlap (production 2,248.89–2,528.16, revised
2,354.96–2,464.63). Instructions are 43.350B / 44.164B / 43.733B: the shared
index reduces the previous prototype's work but remains 0.88% above production.
Cycles are 14.582B / 15.070B / 15.008B. All frame/allocation guards pass.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Shared-index Huffman varied compression 3 | 2423.48 | 2515.43 | 96.34% | 6.00 | 6.59 | 90.94% | 127.02 | 628.16 | 20.22% |

Raw results: `out/aligned-strategy-control/huffman-shared-index-varied-l3-*`.

The final mixed-level-3 case closes this revision without a retention case.
Eight balanced 8,192-frame four-way trials give production / previous /
revised / reference medians of 1,824.04 / 1,702.82 / 1,764.23 / 1,715.26
MiB/s (-3.28% revised versus production). Ranges overlap (production
1,720.14–1,900.49, revised 1,556.11–1,839.73). Instructions are effectively
unchanged (59.984B / 59.991B / 59.991B), with cycles of
19.476B / 20.894B / 20.343B. All output/allocation guards pass and the run
completed successfully. The shared-index revision is not retained: reducing
the previous prototype's bookkeeping did not yield a useful production gain
on the remaining varied-data deficits. Production is unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Shared-index Huffman mixed compression 3 | 1764.23 | 1715.26 | 102.85% | 6.23 | 6.73 | 92.60% | 127.02 | 628.16 | 20.22% |

Raw results: `out/aligned-strategy-control/huffman-shared-index-mixed-l3-*`.

#### Fast-parser post-match tag preloads

An isolated change loads the two post-match tag bytes before writing their
hash-table entries. Assembly confirms the intended ordering, and the disabled
level-2 code is byte-identical to production. Units, all-level cross-decoding,
and sanitizer parser tests pass. Six balanced 16,384-frame varied-level-1
triples are neutral: baseline 3,126.06 versus candidate 3,133.99 MiB/s (+0.25%),
with broadly overlapping ranges (2,844.17–3,235.45 versus 2,851.76–3,208.39).
Instructions are identical at 80.442B; cycles are 22.953B versus 22.890B.
All frame/allocation guards pass. Mixed-data control remains pending; no
speedup or production integration is claimed.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast tag preload varied compression 1 | 3133.99 | 3372.58 | 92.93% | 5.12 | 5.89 | 86.93% | 126.71 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/fast-tag-preload-varied-l1-*`.

The mixed-level-1 control is also inconclusive: six balanced 16,384-frame
triples give baseline 2,163.09 versus candidate 2,188.15 MiB/s (+1.16%), with
overlapping ranges (2,025.37–2,238.53 versus 2,075.28–2,207.63). Instructions
remain identical at 118.467B; cycles are 33.053B versus 32.796B. All guards
pass and the run completed successfully. This ordering-only change is not
retained: neither corpus establishes a useful improvement. The profile's
hot load samples alone did not establish a costly store/load dependency.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast tag preload mixed compression 1 | 2188.15 | 2097.59 | 104.32% | 5.38 | 6.14 | 87.53% | 126.71 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/fast-tag-preload-mixed-l1-*`.

### Rejected experiment: conditional post-match tag writes

An isolated level-1 candidate skips the catchup/trailing tag store when the
existing tag byte is identical. Diagnostic counts found 54.87% redundant
stores on varied text, but avoiding them adds a load and branch. Six balanced
16,384-frame GCC Release trials on CPU 2 show no benefit: varied throughput
falls from production's 3,130.75 to 2,933.12 MiB/s (-6.31%), and mixed falls
from 2,226.12 to 2,191.24 MiB/s (-1.57%). Trial ranges overlap narrowly on
varied and also overlap on mixed; no statistical significance is claimed.
Varied instructions increase 0.84%; mixed instructions increase 0.02%.
Strict unit tests, all-level cross-decoding acceptance, 152 sanitizer-checked
parser/entropy blocks, exact benchmark frames, and allocation guards pass.
The candidate is **not retained**; the main production report is unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Conditional tag write varied compression 1 | 2933.12 | 3347.90 | 87.61% | 5.12 | 5.89 | 86.90% | 126.71 | 628.16 | 20.17% |
| Conditional tag write mixed compression 1 | 2191.24 | 2086.79 | 105.01% | 5.34 | 6.11 | 87.38% | 126.71 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/fast-tag-writeguard-{varied,mixed}-l1-*`.

### Experimental fast-parser local frame base (not retained)

Caching the frame-index base once per parse removes member reloads but grows
the generated stack frame. Six balanced 16,384-frame GCC Release CPU-2 trials
on varied level 1 give production 3,153.71 versus candidate 3,059.29 MiB/s
(-2.99%), with overlapping ranges (3,039.36–3,187.33 versus
2,963.43–3,184.13). Instructions are effectively unchanged at 80.442B;
cycles increase from 22.992B to 23.625B. All frame/allocation checks pass.
Mixed-level-1 and level-2 controls are pending; no improvement or production
integration is claimed.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast local base varied compression 1 | 3059.29 | 3326.50 | 91.97% | 5.12 | 5.89 | 86.91% | 126.71 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/fast-local-base-varied-l1-*`.

The remaining local-base controls are complete, and this candidate is rejected.
Mixed level 1 falls from production 2,167.69 to 2,147.57 MiB/s (-0.93%);
varied level 2 is effectively unchanged, 3,119.65 to 3,113.70 (-0.19%);
mixed level 2 falls from 2,691.85 to 2,564.85 (-4.72%). All trial ranges
overlap. Instructions decrease by 0.04%, 0.10%, and 0.52%, respectively,
without a useful throughput gain. Every exact-frame/allocation guard passed.
Production code and its main benchmark report remain unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast local base mixed compression 1 | 2147.57 | 2007.72 | 106.97% | 5.38 | 6.13 | 87.67% | 126.71 | 628.16 | 20.17% |
| Fast local base varied compression 2 | 3113.70 | 3359.16 | 92.69% | 5.22 | 6.13 | 85.25% | 122.65 | 628.16 | 19.53% |
| Fast local base mixed compression 2 | 2564.85 | 2476.71 | 103.56% | 5.62 | 6.25 | 89.99% | 122.65 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/fast-local-base-{varied,mixed}-l{1,2}-*`.

### Experimental fast-parser input prefetch (not retained)

This isolated candidate adds the reference fast parser's two input-prefetch
hints at search-step growth, with an input-bound check before forming the
addresses. Six balanced 16,384-frame GCC Release CPU-2 trials on varied level 1
give production 3,169.73 versus candidate 3,084.58 MiB/s (-2.69%). Ranges
overlap (3,078.30–3,212.11 versus 2,964.32–3,138.78); instructions increase
2.01%, from 80.442B to 82.059B, and cycles rise 22.898B to 23.417B.
Strict unit tests, all-level reciprocal acceptance, 152 sanitizer-checked
parser/entropy blocks, and measured frame/allocation guards pass. Mixed
level 1 and level 2 controls remain pending. No improvement or production
integration is claimed.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast input prefetch varied compression 1 | 3084.58 | 3352.34 | 92.01% | 5.12 | 5.89 | 86.97% | 126.71 | 628.16 | 20.17% |

Raw results: `out/aligned-strategy-control/fast-prefetch-varied-l1-*`.

The remaining prefetch screen is complete. Mixed level 1 is effectively
unchanged: production 2,182.35 versus candidate 2,175.31 MiB/s (-0.32%).
Varied level 2 is also unchanged: 3,087.02 versus 3,089.37 (+0.08%).
Mixed level 2 is promising at 2,434.47 versus 2,650.02 (+8.85%), but its
ranges overlap (2,345.67–2,655.79 versus 2,584.91–2,758.69), so it needs
longer confirmation before retention. Instructions change by +0.67%,
approximately zero, and +0.08%, respectively. All exact-frame/allocation
guards pass. No production change or confirmed speedup is claimed.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast input prefetch mixed compression 1 | 2175.31 | 2082.35 | 104.46% | 5.38 | 6.14 | 87.56% | 126.71 | 628.16 | 20.17% |
| Fast input prefetch varied compression 2 | 3089.37 | 3309.53 | 93.35% | 5.23 | 6.14 | 85.12% | 122.51 | 628.16 | 19.50% |
| Fast input prefetch mixed compression 2 | 2650.02 | 2433.27 | 108.91% | 5.62 | 6.23 | 90.28% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/fast-prefetch-{varied,mixed}-l{1,2}-*`.

Longer level-2 confirmation uses the same binaries, six balanced trials of
32,768 frames, and reversed trial/corpus order. Mixed throughput remains
favorable: production 2,539.90 versus candidate 2,637.19 MiB/s (+3.83%).
However, ranges still overlap broadly (2,374.94–2,742.43 versus
2,464.09–2,725.56), so the first screen's +8.85% is not a stable gain estimate.
Instructions rise 0.08%; cycles decrease from 56.431B to 53.905B.
Every frame/allocation guard passes. Varied-text confirmation remains pending;
the optimization is still not integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast prefetch confirmation mixed compression 2 | 2637.19 | 2485.60 | 106.10% | 5.62 | 6.23 | 90.16% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/fast-prefetch-confirm-mixed-l2-*`.

Within-trial comparisons favor the candidate in only three of six trials
(gains range from -7.15% to +13.94%), reinforcing that this is not yet a
reliable speedup.

Varied-text confirmation is complete: production 3,164.67 versus candidate
3,184.51 MiB/s (+0.63%), with overlapping ranges (3,027.74–3,229.43 versus
3,042.88–3,222.23). Only three of six paired trials favor the candidate
(-2.75% to +2.81%). Instructions are essentially unchanged at 148.929B;
cycles are 45.683B versus 45.214B. All frame/allocation guards passed.
The prefetch candidate is **not retained**: neither confirmation establishes
a sufficiently consistent gain, and the remaining varied-text reference gap
persists. Production remains unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fast prefetch confirmation varied compression 2 | 3184.51 | 3407.76 | 93.45% | 5.21 | 6.13 | 84.99% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/fast-prefetch-confirm-varied-l2-*`.

### Experimental pre-sized compact-sequence storage (not retained)

An isolated candidate keeps backing records alive with a separate logical
count and reserves the sequence bound once per block, eliminating per-append
vector capacity checks in the compact parser. Six balanced 16,384-frame GCC
Release CPU-2 varied-level-1 trials give production 3,203.92 versus candidate
3,165.12 MiB/s (-1.21%), with overlapping ranges (3,172.68–3,237.07 versus
3,117.33–3,190.86). Instructions increase 0.86%, from 80.442B to 81.136B;
cycles rise from 22.831B to 23.175B. Candidate RSS also rises because pre-sizing
initializes the backing records. Unit/all-level reciprocal tests, 152
sanitizer-checked parser/entropy cases, dedicated storage-lifecycle checks,
and all measured frame/allocation guards pass. Other controls are pending;
no speedup or production integration is claimed.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Pre-sized compact store varied compression 1 | 3165.12 | 3430.11 | 92.27% | 5.46 | 5.88 | 92.86% | 127.02 | 628.16 | 20.22% |

Raw results: `out/aligned-strategy-control/compact-reserved-varied-l1-*`.

The pre-sized-storage screen is complete and this version is not retained.
Mixed level 1 changes from production 2,256.63 to candidate 2,224.96 MiB/s
(-1.40%). Varied level 2 changes from 3,186.68 to 3,223.29 (+1.15%), while
mixed level 2 changes from 2,764.35 to 2,694.07 (-2.54%). All ranges overlap;
every frame/allocation guard passed. Level 2 retains the original sequence
representation: its results reflect changed code generation from the shared
append wrapper, not use of compact backing storage. The level-1 implementation
still loads/updates a logical count and computes a destination address per
sequence. Removing the vector capacity check alone did not establish a gain.
Production remains unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Pre-sized compact store mixed compression 1 | 2224.96 | 2141.07 | 103.92% | 5.61 | 6.13 | 91.37% | 127.02 | 628.16 | 20.22% |
| Pre-sized compact store varied compression 2 | 3223.29 | 3475.52 | 92.74% | 5.23 | 6.13 | 85.25% | 122.65 | 628.16 | 19.53% |
| Pre-sized compact store mixed compression 2 | 2694.07 | 2557.15 | 105.35% | 5.60 | 6.25 | 89.62% | 122.65 | 628.16 | 19.53% |

Raw results: `out/aligned-strategy-control/compact-reserved-{varied,mixed}-l{1,2}-*`.

### Experimental block-local compact-sequence cursor

The revision keeps the append cursor and exception metadata local to parsing,
then commits once per block. Eight position-balanced 8,192-frame GCC Release
CPU-2 four-way trials compare production, the previous pre-sized-store
prototype, this revision, and reference. Varied level 1 medians are
3,170.84 / 3,141.44 / 3,266.62 / 3,426.53 MiB/s: the revision gains 3.02%
over production and wins six of eight paired trials. Mixed medians are
2,258.21 / 2,219.01 / 2,212.71 / 2,132.84: a 2.01% loss, with seven of
eight paired trials slower. Both baseline/candidate ranges overlap.
Instructions change by +0.90% varied and -0.95% mixed; fewer instructions
alone do not explain the performance outcome. The mixed regression and higher
RSS prevent retaining this revision as-is; production remains unchanged.

All-level reciprocal acceptance, unit tests, sanitizer parser/storage-lifecycle
checks, exact benchmark frames, and allocation guards pass. Level 2's `.text`
is byte-identical to production, unlike the previous prototype.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block-local compact cursor varied compression 1 | 3266.62 | 3426.53 | 95.33% | 5.45 | 5.89 | 92.57% | 126.80 | 628.16 | 20.19% |
| Block-local compact cursor mixed compression 1 | 2212.71 | 2132.84 | 103.74% | 5.62 | 6.14 | 91.51% | 126.80 | 628.16 | 20.19% |

Raw results: `out/aligned-strategy-control/compact-cursor-{varied,mixed}-l1-*`.

### Rejected experiment: separate sequence-code and literal-copy loops

The isolated encoder generated LL/ML/OF codes before traversing sequences
again to gather literals. Six balanced 16,384-frame GCC Release CPU-2 trials
show varied level 2 falling from production 3,227.62 to 3,184.74 MiB/s
(-1.33%), and mixed falling from 2,619.93 to 2,432.32 (-7.16%). Both ranges
overlap. Instructions increase 1.55% and 1.76%, respectively. The extra
traversal did not yield a useful simplification, so this candidate is not
retained. Level 1's machine code is unchanged. Strict units, all-level
reciprocal acceptance, 152 sanitizer-checked parser/entropy cases, and every
benchmark frame/allocation guard pass; production remains unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Split code and literal loops varied compression 2 | 3184.74 | 3468.12 | 91.83% | 5.21 | 6.13 | 84.97% | 122.51 | 628.16 | 19.50% |
| Split code and literal loops mixed compression 2 | 2432.32 | 2508.73 | 96.95% | 5.60 | 6.23 | 89.91% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/split-gather-{varied,mixed}-l2-*`.

### Experimental block-level literal-copy bounds

This parser-only candidate proves input/output slack once per block before
using 16-byte short copies; nonqualifying blocks and generic sequence views
retain checked copying. It adds no padding or allocations. Six balanced
16,384-frame GCC Release CPU-2 trials show varied level 2 at production
3,206.48 versus candidate 3,238.65 MiB/s (+1.00%), with all six paired
trials favorable but overlapping overall ranges. Mixed is effectively
unchanged at 2,683.98 versus 2,673.67 (-0.38%), with two of six paired wins.
Instructions fall 1.19% varied and 2.19% mixed. Longer confirmation is
pending; the candidate is not integrated.

The arithmetic boundary oracle, 1,728 checked/trusted encoder comparisons
under ASan/UBSan, unit/all-level reciprocal acceptance, and benchmark
frame/allocation guards pass. Level 1's machine code is unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block-level gather bounds varied compression 2 | 3238.65 | 3453.66 | 93.77% | 5.24 | 6.13 | 85.56% | 122.51 | 628.16 | 19.50% |
| Block-level gather bounds mixed compression 2 | 2673.67 | 2575.25 | 103.82% | 5.60 | 6.24 | 89.79% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-{varied,mixed}-l2-*`.

Longer confirmation uses the same binaries, six balanced 32,768-frame trials,
and reversed trial/corpus order. Mixed level 2 is effectively unchanged:
production 2,751.37 versus candidate 2,749.96 MiB/s (-0.05%), with overlapping
ranges (2,536.76–2,810.41 versus 2,589.68–2,809.97). The 2.19% instruction
reduction repeats, while cycles fall from 53.253B to 52.822B. Every frame and
allocation guard passes. Varied-text confirmation is still pending; production
has not changed.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block bounds confirmation mixed compression 2 | 2749.96 | 2506.61 | 109.71% | 5.57 | 6.25 | 89.21% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-confirm-mixed-l2-*`.

Varied-text confirmation is complete: production 3,180.15 versus candidate
3,251.77 MiB/s (+2.25%), with five of six paired trials faster. Ranges still
overlap (3,153.09–3,218.49 versus 3,122.42–3,260.22). The 1.19% instruction
reduction repeats; cycles fall from 45.538B to 44.956B. All frame/allocation
guards pass. This repeats the initial varied gain while mixed remains neutral;
broader sanitizer, custom-parameter, and MSVC correctness checks are now in
progress. The candidate remains isolated, not production code.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block bounds confirmation varied compression 2 | 3251.77 | 3463.00 | 93.90% | 5.24 | 6.13 | 85.45% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-confirm-varied-l2-*`.

The full sanitizer, custom fast/minMatch-6, and MSVC correctness gates passed.
The affected 64-KiB and 256-KiB level-1 presets were then measured using fresh
GCC 13 Release workers. Six balanced production/candidate/reference trials
processed 4 GiB each, with exact-frame comparisons and allocation guards passing.
These remain experimental results, not a production update.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block bounds 64-KiB varied compression 1 | 3522.91 | 3523.10 | 99.99% | 4.12 | 4.01 | 102.63% | 122.50 | 628.16 | 19.50% |
| Block bounds 64-KiB mixed compression 1 | 38159.64 | 9811.13 | 388.94% | 4.12 | 4.00 | 102.83% | 122.50 | 628.16 | 19.50% |
| Block bounds 256-KiB varied compression 1 | 3426.59 | 4030.65 | 85.01% | 4.37 | 4.38 | 99.91% | 122.51 | 628.16 | 19.50% |
| Block bounds 256-KiB mixed compression 1 | 40324.46 | 9107.96 | 442.74% | 4.36 | 4.38 | 99.73% | 122.51 | 628.16 | 19.50% |

Production medians in the same row order were 3471.13, 38132.16, 3428.81,
and 40925.53 MiB/s: candidate changes of +1.49%, +0.07%, -0.06%, and -1.47%.
All production/candidate ranges overlap. Varied-text instructions fell 1.09%
and 1.15%; mixed instructions were effectively unchanged. The very compressible
mixed cases ran for only about 0.1 seconds per C++ trial, so longer confirmation
is required before treating their throughput differences as a regression or gain.
The reference also leads on 256-KiB varied text; the overall goal remains unmet.

Raw results: `out/aligned-strategy-control/block-bounds-small-{65536,262144}-*`.

The longer 256-KiB mixed confirmation (six balanced 64-GiB trials) measured
production at 42275.02 versus candidate at 41217.69 MiB/s (-2.50%). Ranges
overlap (41638.74–43127.73 versus 38098.26–42140.82), but this repeats the
screen's unfavorable direction. Instructions remain essentially unchanged
(+0.02%); cycles rise from 6.979B to 7.156B. All frame/allocation guards pass.
This argues against adopting the current candidate unchanged; the remaining
small-input confirmation cases are still running.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block bounds confirmation 256-KiB mixed compression 1 | 41217.69 | 9339.42 | 441.33% | 4.34 | 4.38 | 99.11% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-small-confirm-262144-mixed-l1-*`.

The 256-KiB varied-text confirmation (six balanced 16-GiB trials) measured
3382.49 MiB/s production versus 3413.60 candidate (+0.92%). Ranges overlap
(3326.47–3416.09 versus 3387.33–3437.72). Instructions fall 1.15%, with cycles
21.603B versus 21.442B. All guards pass, but this does not offset the need to
resolve the mixed-case regression before adopting the candidate.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block bounds confirmation 256-KiB varied compression 1 | 3413.60 | 4030.12 | 84.70% | 4.37 | 4.38 | 99.69% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-small-confirm-262144-varied-l1-*`.

The remaining 64-KiB confirmations are complete. Mixed-prefix production versus
candidate was 37193.18 versus 38246.89 MiB/s (+2.83%, overlapping ranges);
varied text was 3423.26 versus 3556.55 MiB/s (+3.89%, non-overlapping observed
ranges of 3350.85–3454.49 and 3474.61–3598.43). Varied instructions fell 1.09%;
mixed-prefix instructions changed only +0.01%. All frame/allocation guards pass.

**Corpus clarification:** the small files named `mixed` are prefixes of the
1-MiB mixed corpus, whose first quarter is all zeros. Both small sizes therefore
test uniform zero input, not all four components of the mixed corpus. The rows
above retain their raw artifact labels; they must not be read as scaled mixed
coverage. Genuinely mixed small-input controls are also needed before retention.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Block bounds confirmation 64-KiB mixed-prefix compression 1 | 38246.89 | 9597.43 | 398.51% | 4.10 | 3.99 | 102.64% | 122.50 | 628.16 | 19.50% |
| Block bounds confirmation 64-KiB varied compression 1 | 3556.55 | 3502.36 | 101.55% | 4.12 | 3.98 | 103.43% | 122.50 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-small-confirm-65536-*`.
The current candidate remains isolated: the 256-KiB zero-input slowdown remains
unresolved despite gains elsewhere.

#### Output-first bounds qualification (experimental)

The next revision checks output eligibility before examining the last sequence
for the source-bounds proof. Sanitizer boundary tests, unit tests, and all-level
reciprocal acceptance pass. Assembly confirms that ineligible sparse blocks skip
that setup; no safety condition was removed.

Eight position-balanced four-way trials on 256-KiB zero input (64 GiB per trial)
measured production / previous candidate / revision / reference medians of
39221.23 / 38448.60 / 40174.15 / 8849.10 MiB/s. The revision is +2.43% against
production in this run, but ranges overlap substantially: production
31299.39–41901.62, previous 33561.19–39660.23, revision 34958.81–42360.30.
This is not yet evidence of a stable speedup. Revision instruction count is
29.145111B versus previous 29.148519B and production 29.144063B—about 13 fewer
instructions per frame than the previous candidate, with four remaining above
production. Cycles are 7.191B versus previous 7.374B and production 7.177B.
All exact-frame and allocation guards pass. Remaining corpus controls are
running; neither candidate is integrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Output-first 256-KiB zero compression 1 | 40174.15 | 8849.10 | 453.99% | 4.35 | 4.38 | 99.38% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-output-first-262144-zero-l1-*`.

The 256-KiB varied-text four-way result is unfavorable: production / previous /
revision / reference medians are 3159.25 / 3141.51 / 3084.36 / 3668.43 MiB/s
(-2.37% revision versus production). All three C++ ranges overlap widely
(2783.81–3347.48 / 2826.32–3291.39 / 2952.27–3399.80), so this run is not a
precise regression estimate. The revision executes 1.15% fewer instructions
than production, but cycles increase from 11.044B to 11.223B (previous 11.165B).
All exact-frame and allocation guards pass. Reduced work has not established a
speed benefit; remaining controls are pending, with production unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Output-first 256-KiB varied compression 1 | 3084.36 | 3668.43 | 84.08% | 4.37 | 4.38 | 99.91% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-output-first-262144-varied-l1-*`.

The genuinely mixed 256-KiB control includes all four corpus components.
Production / previous / revision / reference medians were
1927.53 / 1893.65 / 1902.86 / 2267.11 MiB/s: revision -1.28% versus production
and only 83.93% of reference. C++ ranges overlap (1761.87–2062.72 /
1665.37–2098.97 / 1831.00–2147.04). Revision instructions fall 1.81% against
production (59.763321B to 58.679388B), but cycles rise slightly from 17.856B
to 17.944B; the previous candidate used 17.936B. All guards pass.
This is a separate small-input performance gap, not covered by the successful
1-MiB mixed report. Neither bounds candidate has demonstrated a speed benefit
on this case; it remains part of the optimization target.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Output-first 256-KiB scaled-mixed compression 1 | 1902.86 | 2267.11 | 83.93% | 4.50 | 4.50 | 99.83% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-output-first-262144-scaled-mixed-l1-*`.

The 64-KiB zero-input four-way medians were production / previous / revision /
reference 32959.30 / 33589.34 / 32405.80 / 8249.24 MiB/s. The revision is
1.68% below production, with overlapping ranges (29570.89–33797.35 /
32100.01–35375.36 / 29802.84–36043.34). Unlike the larger zero-input case,
instruction count did not fall: production / previous / revision used
31.703556B / 31.707750B / 31.708799B instructions, respectively. Cycles were
8.280B / 8.134B / 8.384B. All guards pass, but these noisy timings and lack of
work reduction provide no retention evidence for this case.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Output-first 64-KiB zero compression 1 | 32405.80 | 8249.24 | 392.83% | 4.08 | 4.01 | 101.75% | 122.50 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-output-first-65536-zero-l1-*`.

On 64-KiB varied text, production / previous / revision / reference medians
were 2975.93 / 2950.17 / 3085.14 / 3078.34 MiB/s: revision +3.67% against
production and 100.22% of reference. The wide C++ ranges still overlap
(2710.33–3192.22 / 2608.59–3279.98 / 2850.08–3211.18), so the tiny margin
over reference is not a robust win. Instructions fall 1.10% against production
(39.223444B to 38.793393B; previous 38.794442B), and cycles fall from 11.383B
to 11.022B (previous 11.326B). All guards pass. This favorable case does not
resolve the unfavorable controls, and the revision remains experimental.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Output-first 64-KiB varied compression 1 | 3085.14 | 3078.34 | 100.22% | 4.10 | 3.99 | 102.64% | 122.50 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-output-first-65536-varied-l1-*`.

The genuinely mixed 64-KiB control measured production / previous / revision /
reference medians of 1524.92 / 1457.46 / 1539.73 / 1728.45 MiB/s. Revision
improved 0.97% against production but reached only 89.08% of reference. C++
ranges overlap widely (1208.38–1605.28 / 1297.73–1625.20 / 1358.21–1600.06).
Instructions fell 1.65% against production (73.752902B to 72.537199B; previous
72.537724B), with cycles 23.194B / 24.124B / 22.879B respectively. All guards
pass. This confirms another small-input gap versus reference, not overall
success; the final 1-MiB controls remain pending.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Output-first 64-KiB scaled-mixed compression 1 | 1539.73 | 1728.45 | 89.08% | 4.11 | 3.97 | 103.49% | 122.50 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-output-first-65536-scaled-mixed-l1-*`.

The 1-MiB varied-text level-2 control measured production / previous / revision /
reference medians of 3017.24 / 2881.29 / 2964.53 / 3325.93 MiB/s. Revision
was 1.75% below production and reached 89.13% of reference. C++ ranges overlap
(2788.74–3158.87 / 2441.60–3220.30 / 2520.39–3144.35). Instructions decreased
1.20% against production (37.237074B to 36.791769B; previous 36.792171B),
but cycles were 11.611B / 11.911B / 11.672B respectively. All guards pass.
The revised bounds ordering has not demonstrated a reliable overall throughput
benefit, including on its original dense-text target. It remains unintegrated.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Output-first 1024-KiB varied compression 2 | 2964.53 | 3325.93 | 89.13% | 5.25 | 6.13 | 85.62% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-output-first-1048576-varied-l2-*`.

The final 1-MiB mixed level-2 control measured production / previous / revision /
reference medians of 2498.45 / 2510.95 / 2666.56 / 2346.75 MiB/s. Revision
was 6.73% above production and 113.63% of reference, with overlapping C++
ranges (2278.24–2798.01 / 1981.54–2757.31 / 2383.52–2777.30). Instructions
fell 2.19% against production (48.513101B to 47.448518B; previous 47.448986B),
and cycles were 13.984B / 13.981B / 13.399B respectively. All guards pass.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Output-first 1024-KiB mixed compression 2 | 2666.56 | 2346.75 | 113.63% | 5.62 | 6.25 | 89.88% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/block-bounds-output-first-1048576-mixed-l2-*`.
The four-way experiment is complete. Despite favorable individual cases, the
revision is not retained: throughput effects were inconsistent across inputs,
including losses on the original 1-MiB varied-text target. The isolated candidates
and raw results remain available. Production/reference profiling of genuine
256-KiB mixed input is the next investigation; production is unchanged.

#### Reference-compatible literal sampling (experimental)

Profiling found that C++ counted all 67,585 literals in one mixed-input block,
while reference zstd could reject Huffman coding after sampling 4,096 bytes at
each end. The isolated candidate ports the reference's exact eligibility and
sample-maxima thresholds, reusing existing histogram scratch storage. Unit tests,
all-level reciprocal acceptance, and 1,800 sanitizer boundary cases pass.

Six balanced 16-GiB trials on genuinely mixed 256-KiB input measured production
2043.70 versus candidate 2337.58 MiB/s: **+14.38%**, reaching **96.55% of
reference**. Production/candidate ranges were 1950.78–2167.04 and
2165.27–2462.50 MiB/s. Instructions fell 6.65% (119.522886B to 111.575278B),
and cycles fell from 34.763B to 30.598B. All exact-frame and allocation guards
pass. Remaining corpus controls and broader validation are pending; the change
is not yet integrated and does not yet beat reference on this case.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Literal sampling 256-KiB scaled-mixed compression 1 | 2337.58 | 2420.99 | 96.55% | 4.50 | 4.47 | 100.48% | 122.50 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/literal-sample-262144-scaled-mixed-l1-*`.

The 256-KiB varied-text control was essentially neutral: production 3240.08
versus candidate 3230.49 MiB/s (-0.30%), with overlapping ranges
3116.24–3312.33 and 3184.99–3292.08. Instructions changed -0.15%
(75.646237B to 75.530630B); cycles were 22.006B versus 22.041B. All exact-frame
and allocation guards pass. This does not close the varied-text gap versus
reference, but does not show a clear regression from the sampling shortcut.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Literal sampling 256-KiB varied compression 1 | 3230.49 | 3777.54 | 85.52% | 4.37 | 4.39 | 99.47% | 122.50 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/literal-sample-262144-varied-l1-*`.

The 64-KiB genuinely mixed control measured production 1582.98 versus candidate
1565.82 MiB/s (-1.08%), with strongly overlapping ranges 1527.52–1626.80 and
1523.60–1621.40. Instructions changed -0.23% (147.503322B to 147.166990B),
while cycles rose from 45.771B to 46.262B. All exact-frame and allocation
guards pass. This smaller-input result needs confirmation before adoption;
the candidate remains isolated despite the larger mixed-input gain.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Literal sampling 64-KiB scaled-mixed compression 1 | 1565.82 | 1896.58 | 82.56% | 4.12 | 4.00 | 102.83% | 122.50 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/literal-sample-65536-scaled-mixed-l1-*`.

The 64-KiB varied-text control measured production 3426.40 versus candidate
3480.53 MiB/s (+1.58%), reaching 101.29% of reference. Ranges overlap
(3353.78–3479.03 versus 3162.08–3561.15), so this narrow reference lead is not
yet a robust win. Instructions changed -0.15% (78.443863B to 78.327860B),
and cycles fell from 21.249B to 21.004B. All exact-frame and allocation guards
pass. The final 1-MiB controls remain pending; production is unchanged.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Literal sampling 64-KiB varied compression 1 | 3480.53 | 3436.13 | 101.29% | 4.08 | 3.99 | 102.15% | 122.50 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/literal-sample-65536-varied-l1-*`.

The 1-MiB mixed level-2 control measured production 2736.55 versus candidate
2664.28 MiB/s (-2.64%), still 105.19% of reference. Ranges overlap
(2457.10–2791.07 versus 2532.41–2774.63). Instructions changed -0.28%
(97.018346B to 96.745700B), but cycles rose from 26.317B to 27.506B.
All guards pass. This possible tradeoff needs confirmation alongside the
substantial 256-KiB mixed improvement before deciding on retention.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Literal sampling 1024-KiB mixed compression 2 | 2664.28 | 2532.93 | 105.19% | 5.60 | 6.24 | 89.67% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/literal-sample-1048576-mixed-l2-*`.

The final 1-MiB varied-text control measured production 3194.52 versus candidate
3211.50 MiB/s (+0.53%), reaching 93.05% of reference. Ranges overlap
(3118.07–3253.76 versus 3194.92–3241.73). Instructions changed -0.16%
(74.467686B to 74.352041B), and cycles fell from 22.735B to 22.665B.
All exact-frame and allocation guards pass. The initial set is complete;
longer, reversed-order mixed-input trials are underway to confirm the large
256-KiB gain and assess the possible 64-KiB/1-MiB tradeoffs. No production
change has been made.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Literal sampling 1024-KiB varied compression 2 | 3211.50 | 3451.27 | 93.05% | 5.24 | 6.12 | 85.61% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/literal-sample-1048576-varied-l2-*`.

Longer confirmation on 256-KiB mixed input used six balanced 32-GiB trials
with the order reversed. Production measured 2136.58 versus candidate
2413.00 MiB/s: **+12.94%**, reaching **98.31% of reference**. The observed
ranges do not overlap (2033.50–2142.70 versus 2233.25–2445.59): every
candidate trial exceeded every production trial. Instructions again fell
6.65% (239.042014B to 223.146919B), and cycles fell from 68.218B to 60.370B.
All exact-frame and allocation guards pass. This confirms the target gain,
but the other mixed-size confirmations and broader correctness gates remain
pending before integration.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Literal sampling confirmation 256-KiB scaled-mixed compression 1 | 2413.00 | 2454.48 | 98.31% | 4.50 | 4.52 | 99.40% | 122.50 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/literal-sample-confirm-262144-scaled-mixed-l1-*`.

The longer 64-KiB mixed confirmation was essentially neutral: production
1599.45 versus candidate 1595.26 MiB/s (-0.26%), with overlapping ranges
1505.88–1619.01 and 1564.78–1607.30. Instructions again changed -0.23%
(295.004165B to 294.331501B); cycles were 91.428B versus 91.754B. All guards
pass. The initial -1.08% result did not repeat at the same magnitude, though
this case still trails reference substantially. The final 1-MiB confirmation
and broader validation remain pending.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Literal sampling confirmation 64-KiB scaled-mixed compression 1 | 1595.26 | 1863.85 | 85.59% | 4.12 | 3.99 | 103.28% | 122.50 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/literal-sample-confirm-65536-scaled-mixed-l1-*`.

The longer 1-MiB mixed confirmation was neutral: production 2756.73 versus
candidate 2757.77 MiB/s (+0.04%), reaching 108.71% of reference. Ranges
overlap (2442.07–2795.63 versus 2460.34–2765.04). Instructions changed -0.28%
(194.028839B to 193.483561B), and cycles were 52.829B versus 52.917B.
All guards pass. The initial -2.64% did not repeat. Confirmation therefore
supports the substantial 256-KiB gain with near-neutral mixed controls at
64 KiB and 1 MiB; broader sanitizer/history/custom-parameter validation is
now running before integration.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Literal sampling confirmation 1024-KiB mixed compression 2 | 2757.77 | 2536.76 | 108.71% | 5.61 | 6.26 | 89.73% | 122.51 | 628.16 | 19.50% |

Raw results: `out/aligned-strategy-control/literal-sample-confirm-1048576-mixed-l2-*`.

**Integration update:** the sampling shortcut is now in the production header.
The full sanitizer suite passed, together with 1,800 boundary cases, 24 direct
reference-history cases, eight Huffman/raw/repeat frames decoded by both
implementations, and 1,080 custom fast-strategy reset/streaming cases. These
checks are preserved in the new `literal_sampling_tests` CTest target. All four
GCC Release test targets pass. Rebuilt 64-KiB/256-KiB level-1 and 1-MiB level-2
workers have byte-identical executable code sections to the measured candidate;
the disabled 1-MiB level-1 path remains identical to its production baseline.
Thus the confirmed +12.94% target improvement applies to the integrated code.
MSVC unit, all-level acceptance, and sampling tests also pass (correctness-only
build, not a throughput measurement). The earlier post-sampling all-level report
was independently verified against all 88 raw measurements: all nine metrics
match, all 44 README rows match the generated report, and all 22 compressed
frame pairs have identical SHA-256 hashes. Results are saved under
`out/full-metrics-20260927/after-literal-sampling`.

On that earlier 1-MiB mixed-corpus run, compression led reference at 21 of 22 levels;
level 16 measured 96.65% of reference (subsequent balanced confirmation is below).
Decompression leads at all 22 levels (109.16–166.51% of reference). These are
workload-specific results, not proof of universal superiority: smaller mixed
inputs and varied-text cases still have known gaps. The sampling shortcut is
not active on level 16, so the new screen alone does not establish a regression
caused by that change.

Level 16 was checked again using six alternating C++/reference pairs of 2,000
frames. C++ median throughput was 90.42 versus 83.62 MiB/s (108.13% of reference),
winning five of six pairs; paired percentages were 105.80, 99.40, 106.89,
108.96, 112.72, and 111.58%. Ranges overlapped widely (74.14–94.33 versus
70.08–88.43). Instructions were 148.404B versus 177.496B, and cycles 99.713B
versus 107.734B. All exact-frame and allocation checks pass. The full-report
deficit did not reproduce consistently; its original row is preserved rather
than replaced by this confirmation. Known varied-text and small-input gaps
still prevent an overall completion claim.

| Operation / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Integrated confirmation compression 16 | 90.42 | 83.62 | 108.13% | 21.45 | 21.87 | 98.11% | 140.57 | 628.16 | 22.38% |

Raw results: `out/aligned-strategy-control/integrated-l16-confirm-*`.
