# Benchmark results

These measurements compare optimized, isolated sph-zstd++ (`C++`) and bundled reference-zstd (`Reference`) workers at the same level and on the same input. Throughput is MiB/s (higher is better); memory is peak resident set size and code is the complete worker executable (lower is better). Percentages are `C++ / Reference × 100`. Compression outputs are byte-compared and both decoders check both encodings in paired runs.

The [current 22-level mixed-input evaluation](#current-evaluation) was run on 2026-11-01 from this checkout with GCC 13 Release. The [full current matrix](BENCHMARKS_INPUTS.md) covers mixed, zero-filled, repeated, structured, and seeded-random data from the same optimized build. The earlier focused tables further below are dated snapshots from prior changes; their compilers and trial counts differ. The [experiment history](PERFORMANCE_HISTORY.md) preserves prior reports and rejected trials.

## Confirmed mixed-input level 13 compression

Short level-13 screens have varied widely. A pinned-core follow-up used six alternating trials of 1,000 frames each on the current optimized workers and 1-MiB mixed input. Every trial passed exact compressed-byte comparison and both decoding directions. The README uses this longer result for its level-13 mixed-input compression cell; the current short screen remains below for provenance. Raw paired trials are under `out/optimal-price-delta-split-20261101/confirm-l13-mixed/`.

| C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 473.35 | 428.95 | 110.35% | 21.19 | 21.98 | 96.43% | 126.88 | 628.16 | 20.20% |

## Confirmed mixed-input level 16 compression

The short all-level screen is noisy at level 16. A pinned-core follow-up used six alternating trials of 2,000 frames each on the current optimized workers and the same 1-MiB mixed input. Every trial passed exact compressed-byte comparison and both decoding directions. The README uses this longer result for its level-16 mixed-input compression cell; the short-screen row remains below for provenance. Raw paired trials are under `out/optimal-price-delta-split-20261101/confirm-l16-mixed/`.

| C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 93.35 | 85.44 | 109.26% | 21.46 | 21.88 | 98.06% | 144.95 | 628.16 | 23.07% |

## Confirmed structured-input compression

Six alternating, core-pinned trials per level used the current optimized workers and the same deterministic 1-MiB structured input. Levels 1–4 used 3,000 frames per trial; level 16 used 1,000. Every trial matched the reference's compressed bytes, and both decoders recovered both encodings. These longer results supersede their short-screen rows in the README and full matrix; raw trials are under `out/structured-gap-20261101/confirm-l*/`.

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 649.68 | 669.31 | 97.07% | 5.24 | 6.12 | 85.60% | 131.25 | 628.16 | 20.89% |
| 2 | 808.59 | 831.79 | 97.21% | 5.36 | 6.24 | 85.80% | 127.40 | 628.16 | 20.28% |
| 3 | 683.45 | 716.29 | 95.41% | 6.12 | 6.72 | 91.14% | 127.38 | 628.16 | 20.28% |
| 4 | 647.65 | 692.28 | 93.55% | 7.74 | 7.96 | 97.18% | 127.38 | 628.16 | 20.28% |
| 16 | 493.05 | 487.10 | 101.22% | 21.22 | 21.73 | 97.63% | 144.95 | 628.16 | 23.07% |

## Confirmed double-fast guard compression

After changing the level-3/4 repeat-offset checks to match the reference's eager Boolean evaluation, six alternating core-pinned trials per input compared the current optimized GCC 13 workers with the optimized reference. Structured and mixed used 3,000 frames per trial, random used 5,000, and zero/repeated used 10,000. Every compressed frame matched byte for byte, and each decoder recovered both encodings. These rows supersede the earlier level-3/4 compression rows in the README and input matrix. Raw reports are under `out/structured-gap-20261101/candidate-confirm-l*/`.

| Level | Input | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3 | Zero | 19831.26 | 7743.44 | 256.10% | 6.00 | 6.62 | 90.62% | 127.38 | 628.16 | 20.28% |
| 3 | Repeated | 14913.67 | 8860.90 | 168.31% | 6.00 | 6.53 | 91.87% | 127.38 | 628.16 | 20.28% |
| 3 | Structured | 713.90 | 718.78 | 99.32% | 6.11 | 6.73 | 90.69% | 127.38 | 628.16 | 20.28% |
| 3 | Random | 3565.87 | 2846.17 | 125.29% | 6.98 | 6.68 | 104.47% | 127.38 | 628.16 | 20.28% |
| 3 | Mixed | 1996.26 | 1736.54 | 114.96% | 6.25 | 6.72 | 93.05% | 127.38 | 628.16 | 20.28% |
| 4 | Zero | 20272.44 | 7796.91 | 260.01% | 7.50 | 7.87 | 95.31% | 127.38 | 628.16 | 20.28% |
| 4 | Repeated | 14826.18 | 8689.39 | 170.62% | 7.47 | 7.86 | 95.05% | 127.38 | 628.16 | 20.28% |
| 4 | Structured | 664.99 | 663.14 | 100.28% | 7.75 | 7.99 | 97.09% | 127.38 | 628.16 | 20.28% |
| 4 | Random | 3499.15 | 2760.78 | 126.75% | 8.47 | 7.99 | 106.07% | 127.38 | 628.16 | 20.28% |
| 4 | Mixed | 2016.32 | 1693.72 | 119.05% | 7.72 | 8.00 | 96.48% | 127.38 | 628.16 | 20.28% |

## Confirmed random-input decompression

The short random-input decoder screen suggested a level-3/4 gap. Six alternating, CPU-pinned trials of 100,000 frames per level used the current reference-produced 1-MiB random frame and optimized workers. Both workers verified the decoded output, and the C++ worker checked for allocations after warmup. The longer medians are approximately even; individual trial ratios varied in both directions, so no repeatable random-decoder deficit or speedup is claimed. Raw CSVs are under `out/structured-gap-20261101/random-decode-confirm/`.

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3 | 42255.91 | 41975.51 | 100.67% | 13.18 | 13.18 | 100.00% | 123.28 | 202.89 | 60.76% |
| 4 | 40776.99 | 40675.21 | 100.25% | 13.18 | 13.18 | 100.00% | 123.28 | 202.89 | 60.76% |

## Earlier focused measurements

The following paired reports were collected with optimized builds on 2026-10-25. Each table is a measured snapshot from the named change, not a claim that all rows came from the same build. All nine throughput, memory, and code-size comparison columns are retained.

### MSVC compression: level 1, 1 MiB

Six alternating trials of 3,000 frames per row, after the merged candidate-guard change. Exact compressed bytes and reciprocal decoding passed.

| Input / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1-MiB varied text / 1 | 3219.31 | 3401.78 | 94.64% | 5.76 | 6.38 | 90.18% | 119.00 | 382.00 | 31.15% |
| 1-MiB structured text / 1 | 827.08 | 1091.71 | 75.76% | 5.91 | 6.57 | 89.90% | 119.00 | 382.00 | 31.15% |
| 1-MiB mixed input / 1 | 1937.37 | 2263.21 | 85.60% | 6.06 | 6.58 | 92.02% | 119.00 | 382.00 | 31.15% |

### MSVC compression: levels 8 and 9, 1 MiB

Six alternating paired trials per row after the row-search call-site inlining change. Exact compressed bytes and reciprocal decoding passed.

| Input / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1-MiB mixed input / 8 | 1445.87 | 1362.37 | 106.13% | 11.02 | 11.45 | 96.31% | 115.00 | 382.00 | 30.10% |
| 1-MiB mixed input / 9 | 1272.34 | 1199.60 | 106.06% | 16.00 | 16.48 | 97.05% | 115.00 | 382.00 | 30.10% |
| 1-MiB varied text / 8 | 157.36 | 167.12 | 94.15% | 10.69 | 11.31 | 94.49% | 115.00 | 382.00 | 30.10% |
| 1-MiB varied text / 9 | 150.39 | 159.27 | 94.42% | 15.69 | 16.31 | 96.18% | 115.00 | 382.00 | 30.10% |

### MSVC compression: level 15, 1 MiB

Six alternating trials of 1,000 frames per row after the long-match counting change. Exact compressed bytes and reciprocal decoding passed.

| Input / level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1-MiB repeated text / 15 | 1223.52 | 1227.72 | 99.66% | 21.66 | 22.28 | 97.23% | 116.00 | 382.00 | 30.37% |
| 1-MiB structured text / 15 | 833.92 | 730.85 | 114.10% | 21.67 | 22.34 | 97.03% | 116.00 | 382.00 | 30.37% |
| 1-MiB mixed input / 15 | 672.41 | 535.68 | 125.52% | 21.98 | 22.46 | 97.88% | 116.00 | 382.00 | 30.37% |

### GCC 13 compression: levels 2, 3, and 4

Six alternating Release `-O3` trials per row after the double-fast splitter-layout change; level 2 uses 256 KiB inputs and levels 3/4 use 1 MiB. Exact compressed bytes and reciprocal decoding passed.

| Level / corpus | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2 / varied, 256 KiB | 3123.22 | 3520.78 | 88.71 | 4.32 | 4.51 | 95.63 | 132.03 | 628.16 | 21.02 |
| 2 / balanced, 256 KiB | 2060.08 | 2112.22 | 97.53 | 4.58 | 4.45 | 102.99 | 132.03 | 628.16 | 21.02 |
| 2 / mixed, 256 KiB | 26805.51 | 9840.88 | 272.39 | 4.33 | 4.52 | 95.76 | 132.03 | 628.16 | 21.02 |
| 3 / varied, 1 MiB | 2630.16 | 2567.10 | 102.46 | 5.99 | 6.62 | 90.56 | 127.34 | 628.16 | 20.27 |
| 3 / mixed, 1 MiB | 1950.79 | 1745.24 | 111.78 | 6.26 | 6.71 | 93.33 | 127.34 | 628.16 | 20.27 |
| 4 / varied, 1 MiB | 2713.39 | 2548.81 | 106.46 | 7.51 | 7.85 | 95.69 | 127.34 | 628.16 | 20.27 |
| 4 / mixed, 1 MiB | 1926.41 | 1730.69 | 111.31 | 7.76 | 7.97 | 97.38 | 127.34 | 628.16 | 20.27 |

### MSVC decompression: levels 3, 12, and 16, 1 MiB

Six alternating Release `/O2 /Ob3 /Z7` trials of 10,000 reference-produced frames per row on 2026-11-01 after direct FSE-to-sequence-table construction. The two workers decoded identical bytes. These are production-decoder results, unlike the older all-level table below. Six old/new trials on the same inputs found median MSVC gains of 3.29% at L16 varied, 2.01% at L3 varied, and 0.95% at L12 varied; mixed-input medians were 5.49%, 2.88%, and 1.20% respectively. GCC's direct-table trial regressed on L16 varied and L3 mixed, so GCC retains its original builder.

| Input / operation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Varied text / decode L3 | 9487.90 | 8136.11 | 116.61% | 6.28 | 6.28 | 100.03% | 234.00 | 311.00 | 75.24% |
| Varied text / decode L12 | 9366.33 | 8226.54 | 113.86% | 6.28 | 6.28 | 100.03% | 234.00 | 311.00 | 75.24% |
| Varied text / decode L16 | 3522.01 | 4661.63 | 75.55% | 6.28 | 6.29 | 99.88% | 234.00 | 311.00 | 75.24% |
| Mixed / decode L3 | 7522.65 | 5530.51 | 136.02% | 6.54 | 6.51 | 100.57% | 234.00 | 311.00 | 75.24% |
| Mixed / decode L12 | 23314.97 | 14813.74 | 157.39% | 6.53 | 6.49 | 100.51% | 234.00 | 311.00 | 75.24% |
| Mixed / decode L16 | 17376.16 | 14216.77 | 122.22% | 6.53 | 6.49 | 100.57% | 234.00 | 311.00 | 75.24% |

### GCC 13 decompression: levels 3 and 16, 1 MiB

Six alternating native-targeted Release trials per row after the single-load sequence-table change. The two workers decoded identical bytes.

| Native GCC 13 / decode, 1 MiB | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Level 16 / varied text | 4590.59 | 5671.00 | 80.95 | 5.69 | 5.72 | 99.59 | 191.34 | 170.12 | 112.47 |
| Level 3 / varied text | 10493.44 | 9855.97 | 106.47 | 5.64 | 5.72 | 98.50 | 191.34 | 170.12 | 112.47 |
| Level 3 / mixed | 9211.69 | 6972.97 | 132.11 | 5.89 | 5.95 | 98.98 | 191.34 | 170.12 | 112.47 |

## Earlier 22-level mixed-input screen

The following compression screen was collected on 2026-10-24 with GCC 13 Release `-O3`, one-MiB deterministic mixed input, six alternating trials per level, 300 frames per trial at levels 1–15 and 100 at levels 16–22. Every trial passed exact compressed-byte parity and reciprocal decoding. It predates later retained optimizations; use the [current evaluation](#current-evaluation) for this checkout.

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

<!-- SPH_ZSTDPP_EVAL_BEGIN -->

## Current evaluation

Generated by `zstdpp_eval` using **GNU 13.3.0**, Release, 3 alternating trials of 200 timed iterations each, and a deterministic 1048576-byte mixed corpus. **Key: `C++` is sph-zstd++; `Reference` is reference zstd.** Code size is the complete worker executable; memory is peak resident set size, so both intentionally include runtime overhead. Each percentage is C++ / Reference * 100, computed before display rounding. Higher is better for throughput; lower is better for memory and code size.

Throughput is derived from median elapsed time; memory is the median of process peak RSS. For even trial counts, central values are averaged with integer rounding down. Each trial alternates which implementation runs first, for both operations. All measurements are preserved in `trials.csv` and per-trial worker CSVs; `metrics.csv` contains aggregates.

Compression workers receive identical input. Decompression workers both consume the same reference-zstd-generated frame at each level, so their throughput and memory results use identical compressed bytes.

### Compression

| Level | C++ size (KiB) | Reference size (KiB) | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 258.38 | 258.38 | 2330.02 | 2127.90 | 109.50% | 5.51 | 6.11 | 90.15% | 131.25 | 628.16 | 20.89% |
| 2 | 273.71 | 273.71 | 2814.61 | 2535.17 | 111.02% | 5.59 | 6.27 | 89.04% | 127.40 | 628.16 | 20.28% |
| 3 | 273.66 | 273.66 | 1870.72 | 1734.86 | 107.83% | 6.21 | 6.76 | 91.91% | 127.38 | 628.16 | 20.28% |
| 4 | 273.66 | 273.66 | 1669.29 | 1568.76 | 106.41% | 7.75 | 8.01 | 96.68% | 127.38 | 628.16 | 20.28% |
| 5 | 273.62 | 273.62 | 2294.43 | 2043.64 | 112.27% | 7.84 | 8.51 | 92.10% | 127.35 | 628.16 | 20.27% |
| 6 | 259.00 | 259.00 | 2288.91 | 1612.47 | 141.95% | 7.70 | 8.46 | 90.95% | 122.84 | 628.16 | 19.56% |
| 7 | 259.00 | 259.00 | 2248.12 | 1714.28 | 131.14% | 10.24 | 10.98 | 93.28% | 122.84 | 628.16 | 19.56% |
| 8 | 259.06 | 259.06 | 1580.03 | 1045.54 | 151.12% | 10.25 | 11.05 | 92.72% | 122.84 | 628.16 | 19.56% |
| 9 | 259.06 | 259.06 | 1114.46 | 997.70 | 111.70% | 15.21 | 15.99 | 95.11% | 122.84 | 628.16 | 19.56% |
| 10 | 259.06 | 259.06 | 1177.88 | 942.03 | 125.04% | 15.21 | 16.03 | 94.93% | 122.85 | 628.16 | 19.56% |
| 11 | 259.06 | 259.06 | 1136.92 | 883.36 | 128.70% | 15.20 | 16.00 | 94.99% | 122.85 | 628.16 | 19.56% |
| 12 | 259.06 | 259.06 | 1032.75 | 847.08 | 121.92% | 15.24 | 16.05 | 94.94% | 122.85 | 628.16 | 19.56% |
| 13 | 259.06 | 259.06 | 480.23 | 426.46 | 112.61% | 21.18 | 22.01 | 96.25% | 126.88 | 628.16 | 20.20% |
| 14 | 259.06 | 259.06 | 478.18 | 386.69 | 123.66% | 21.19 | 21.97 | 96.44% | 122.88 | 628.16 | 19.56% |
| 15 | 259.06 | 259.06 | 505.18 | 424.04 | 119.14% | 21.18 | 22.02 | 96.19% | 122.88 | 628.16 | 19.56% |
| 16 | 257.48 | 257.48 | 99.75 | 76.51 | 130.37% | 21.46 | 21.88 | 98.09% | 144.95 | 628.16 | 23.07% |
| 17 | 257.48 | 257.48 | 97.14 | 75.62 | 128.45% | 21.80 | 21.87 | 99.68% | 144.95 | 628.16 | 23.07% |
| 18 | 260.60 | 260.60 | 70.80 | 52.89 | 133.87% | 22.29 | 22.43 | 99.39% | 145.16 | 628.16 | 23.11% |
| 19 | 260.60 | 260.60 | 71.24 | 50.85 | 140.10% | 22.29 | 22.39 | 99.58% | 145.16 | 628.16 | 23.11% |
| 20 | 260.60 | 260.60 | 70.72 | 50.42 | 140.26% | 22.29 | 22.35 | 99.76% | 145.16 | 628.16 | 23.11% |
| 21 | 260.60 | 260.60 | 70.84 | 53.75 | 131.81% | 22.33 | 22.36 | 99.86% | 145.16 | 628.16 | 23.11% |
| 22 | 260.60 | 260.60 | 70.05 | 56.49 | 124.02% | 22.33 | 22.37 | 99.83% | 145.16 | 628.16 | 23.11% |

### Decompression

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4607.87 | 4092.78 | 112.59% | 5.86 | 5.86 | 100.00% | 123.28 | 202.89 | 60.76% |
| 2 | 9430.15 | 6999.39 | 134.73% | 5.86 | 5.95 | 98.36% | 123.28 | 202.89 | 60.76% |
| 3 | 9433.54 | 7189.03 | 131.22% | 5.86 | 5.94 | 98.55% | 123.28 | 202.89 | 60.76% |
| 4 | 9506.15 | 7243.37 | 131.24% | 5.85 | 5.99 | 97.65% | 123.28 | 202.89 | 60.76% |
| 5 | 8749.84 | 7029.42 | 124.47% | 5.85 | 5.98 | 97.84% | 123.28 | 202.89 | 60.76% |
| 6 | 25922.36 | 15493.03 | 167.32% | 5.89 | 5.98 | 98.50% | 123.28 | 202.89 | 60.76% |
| 7 | 26007.56 | 15212.59 | 170.96% | 5.86 | 5.85 | 100.07% | 123.28 | 202.89 | 60.76% |
| 8 | 26309.95 | 15730.64 | 167.25% | 5.85 | 5.86 | 99.73% | 123.28 | 202.89 | 60.76% |
| 9 | 26675.41 | 15471.79 | 172.41% | 5.86 | 5.98 | 97.85% | 123.28 | 202.89 | 60.76% |
| 10 | 26783.24 | 15771.04 | 169.83% | 5.82 | 5.86 | 99.33% | 123.28 | 202.89 | 60.76% |
| 11 | 26510.03 | 16069.37 | 164.97% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 12 | 27175.37 | 16051.84 | 169.30% | 5.85 | 5.98 | 97.78% | 123.28 | 202.89 | 60.76% |
| 13 | 26880.50 | 16277.55 | 165.14% | 5.82 | 5.83 | 99.87% | 123.28 | 202.89 | 60.76% |
| 14 | 27428.63 | 16324.19 | 168.02% | 5.86 | 5.86 | 99.87% | 123.28 | 202.89 | 60.76% |
| 15 | 26961.20 | 16338.20 | 165.02% | 5.89 | 5.98 | 98.50% | 123.28 | 202.89 | 60.76% |
| 16 | 19128.78 | 16208.60 | 118.02% | 5.85 | 5.98 | 97.72% | 123.28 | 202.89 | 60.76% |
| 17 | 19573.91 | 16072.32 | 121.79% | 5.82 | 5.95 | 97.77% | 123.28 | 202.89 | 60.76% |
| 18 | 15970.67 | 12814.94 | 124.63% | 5.86 | 5.98 | 97.91% | 123.28 | 202.89 | 60.76% |
| 19 | 16181.23 | 12908.23 | 125.36% | 5.85 | 5.98 | 97.72% | 123.28 | 202.89 | 60.76% |
| 20 | 16050.59 | 12736.24 | 126.02% | 5.86 | 5.98 | 97.85% | 123.28 | 202.89 | 60.76% |
| 21 | 16271.28 | 12887.63 | 126.26% | 5.86 | 5.95 | 98.36% | 123.28 | 202.89 | 60.76% |
| 22 | 16132.28 | 12692.36 | 127.10% | 5.82 | 5.95 | 97.70% | 123.28 | 202.89 | 60.76% |

### Trial variability

| Level | Operation | C++ min–max (MiB/s) | Reference min–max (MiB/s) | C++ paired wins / trials |
|---:|---|---:|---:|---:|
| 1 | Compression | 2324.85–2361.58 | 2052.19–2183.25 | 3 / 3 |
| 1 | Decompression | 4546.78–4655.13 | 4046.12–4144.24 | 3 / 3 |
| 2 | Compression | 2800.09–2821.75 | 2521.37–2566.96 | 3 / 3 |
| 2 | Decompression | 9315.06–9537.71 | 6883.63–7038.09 | 3 / 3 |
| 3 | Compression | 1816.61–2008.09 | 1646.37–1758.35 | 3 / 3 |
| 3 | Decompression | 9335.70–9494.12 | 7140.41–7370.59 | 3 / 3 |
| 4 | Compression | 1645.84–1956.52 | 1545.75–1619.11 | 3 / 3 |
| 4 | Decompression | 9164.92–9515.09 | 7167.42–7315.43 | 3 / 3 |
| 5 | Compression | 2241.96–2352.86 | 1901.81–2121.55 | 3 / 3 |
| 5 | Decompression | 8634.34–8752.35 | 6959.30–7186.13 | 3 / 3 |
| 6 | Compression | 2199.44–2469.04 | 1443.08–1906.39 | 3 / 3 |
| 6 | Decompression | 25878.47–26425.16 | 14720.04–15583.50 | 3 / 3 |
| 7 | Compression | 1724.31–2366.88 | 1397.86–1759.13 | 2 / 3 |
| 7 | Decompression | 24567.79–26520.21 | 14999.36–15411.68 | 3 / 3 |
| 8 | Compression | 1461.17–1590.82 | 978.32–1118.42 | 3 / 3 |
| 8 | Decompression | 26270.34–26489.45 | 15727.94–15744.52 | 3 / 3 |
| 9 | Compression | 1063.29–1120.52 | 911.51–1011.92 | 3 / 3 |
| 9 | Decompression | 25691.48–27478.20 | 15025.91–15567.58 | 3 / 3 |
| 10 | Compression | 1117.83–1202.61 | 837.26–976.01 | 3 / 3 |
| 10 | Decompression | 26723.39–27256.38 | 15465.81–15833.87 | 3 / 3 |
| 11 | Compression | 1096.22–1227.48 | 726.69–915.86 | 3 / 3 |
| 11 | Decompression | 26179.42–26879.57 | 15683.16–16192.99 | 3 / 3 |
| 12 | Compression | 984.90–1094.13 | 789.04–867.09 | 3 / 3 |
| 12 | Decompression | 27165.68–27309.69 | 15861.70–16292.99 | 3 / 3 |
| 13 | Compression | 447.98–480.66 | 376.02–429.43 | 3 / 3 |
| 13 | Decompression | 26616.49–27641.76 | 16173.28–16349.79 | 3 / 3 |
| 14 | Compression | 455.17–508.88 | 341.25–403.27 | 3 / 3 |
| 14 | Decompression | 26812.37–27552.29 | 16322.56–16324.99 | 3 / 3 |
| 15 | Compression | 487.53–548.89 | 384.93–468.15 | 3 / 3 |
| 15 | Decompression | 26759.02–27616.81 | 15390.38–16359.07 | 3 / 3 |
| 16 | Compression | 99.48–109.53 | 64.97–87.21 | 3 / 3 |
| 16 | Decompression | 18859.97–19802.11 | 15821.32–16518.52 | 3 / 3 |
| 17 | Compression | 83.88–109.31 | 67.43–85.26 | 3 / 3 |
| 17 | Decompression | 19062.83–19714.45 | 16050.86–16152.09 | 3 / 3 |
| 18 | Compression | 69.16–72.39 | 51.40–53.42 | 3 / 3 |
| 18 | Decompression | 15935.12–16380.41 | 12385.35–12959.77 | 3 / 3 |
| 19 | Compression | 70.06–73.07 | 43.30–59.14 | 3 / 3 |
| 19 | Decompression | 16021.44–16345.34 | 12041.06–12910.39 | 3 / 3 |
| 20 | Compression | 63.08–72.17 | 49.95–54.45 | 3 / 3 |
| 20 | Decompression | 15177.72–16123.83 | 12315.31–12886.48 | 3 / 3 |
| 21 | Compression | 67.63–73.40 | 50.48–53.85 | 3 / 3 |
| 21 | Decompression | 15811.02–16621.26 | 12491.18–12945.14 | 3 / 3 |
| 22 | Compression | 46.56–70.35 | 48.45–58.50 | 2 / 3 |
| 22 | Decompression | 15860.04–16485.32 | 12625.41–12712.28 | 3 / 3 |

<!-- SPH_ZSTDPP_EVAL_END -->


## How to reproduce

The full evaluator builds separate optimized workers for each implementation, operation, and configured level. On Windows:

```text
cmake --preset msvc-release-eval
cmake --build --preset msvc-release-eval
cmake --build --preset msvc-run-eval
```

The evaluator updates the marked generated section above and writes `metrics.csv`, `trials.csv`, worker CSVs, and `METRICS.md` to its results directory. It defaults to the configured levels 1, 3, 5, 7, 8, 9, and 15; set `SPH_ZSTDPP_EVAL_LEVELS` to the full semicolon-separated list `1;2;...;22` for a complete matrix. Its `--iterations`, `--trials`, `--output`, and `--report` options allow a fresh run without overwriting prior output. Benchmark both implementations with the same Release optimization and CPU target. Use alternating, unprofiled runs for throughput; profiler samples are for locating hotspots, not measuring speedups.

Pass `--input FILE` to run the same all-level evaluator on a different corpus. The file must have exactly `SPH_ZSTDPP_EVAL_CORPUS_SIZE` bytes; by default the evaluator generates its deterministic mixed input. For the [additional input matrix](BENCHMARKS_INPUTS.md), generate four files with `python3 eval/make_matrix_inputs.py out/matrix-inputs`, then run each through the all-level evaluator with a distinct empty output directory. `eval/render_current_matrix.py` renders their aggregate CSVs together with the mixed-input CSV into the README overview and detailed input report.

For a single level and input, build `msvc-pair`, `gcc-pair`, or `gcc-native-pair` and run `zstdpp_pair --level N --input FILE --iterations 1000 --trials 6`. The input must match the configured `SPH_ZSTDPP_EVAL_CORPUS_SIZE` (1 MiB by default). The pair tool saves exact frames and checks byte-level compression equality and both decoder directions on every trial.
