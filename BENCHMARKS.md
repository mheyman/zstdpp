# Benchmark results

These measurements compare optimized, isolated sph-zstd++ (`C++`) and bundled reference-zstd (`Reference`) workers at the same level and on the same input. Throughput is MiB/s (higher is better); memory is peak resident set size and code is the complete worker executable (lower is better). Percentages are `C++ / Reference × 100`. Compression outputs are byte-compared and both decoders check both encodings in paired runs.

The [current 22-level mixed-input evaluation](#current-evaluation) was run on 2026-11-01 from this checkout with GCC 13 Release. The [additional input matrix](BENCHMARKS_INPUTS.md) covers zero-filled, repeated, structured, and seeded-random data from the same optimized build. The earlier focused tables further below are dated snapshots from prior changes; their compilers and trial counts differ. The [experiment history](PERFORMANCE_HISTORY.md) preserves prior reports and rejected trials.

## Confirmed mixed-input level 13 compression

Short level-13 screens have varied widely. A pinned-core follow-up used six alternating trials of 1,000 frames each on optimized workers with the same unchanged level-13 compressor and 1-MiB mixed input. Every trial passed exact compressed-byte comparison and both decoding directions. The README uses this longer result for its level-13 mixed-input compression cell; the current short screen remains below for provenance.

| C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 522.50 | 466.52 | 112.00% | 21.19 | 21.93 | 96.62% | 126.88 | 628.16 | 20.20% |

## Confirmed mixed-input level 16 compression

The fresh short all-level screen was noisy at level 16. A pinned-core follow-up used six alternating trials of 2,000 frames each on the current optimized workers and the same 1-MiB mixed input. Every trial passed exact compressed-byte comparison and both decoding directions. The README uses this longer result for its level-16 mixed-input compression cell; the short-screen row remains below for provenance. Raw paired trials were generated locally under `out/native-fingerprint-20261101/confirm-l16-mixed/`.

| C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 97.01 | 87.57 | 110.78% | 21.45 | 21.86 | 98.14% | 140.69 | 628.16 | 22.40% |

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
| 1 | 258.38 | 258.38 | 2303.86 | 2003.19 | 115.01% | 5.47 | 6.14 | 89.06% | 131.25 | 628.16 | 20.89% |
| 2 | 273.71 | 273.71 | 2739.62 | 2461.31 | 111.31% | 5.58 | 6.27 | 88.97% | 127.40 | 628.16 | 20.28% |
| 3 | 273.66 | 273.66 | 1858.48 | 1699.60 | 109.35% | 6.25 | 6.77 | 92.21% | 127.38 | 628.16 | 20.28% |
| 4 | 273.66 | 273.66 | 1934.62 | 1580.62 | 122.40% | 7.75 | 8.00 | 96.83% | 127.38 | 628.16 | 20.28% |
| 5 | 273.62 | 273.62 | 2316.33 | 1974.14 | 117.33% | 7.87 | 8.53 | 92.22% | 127.35 | 628.16 | 20.27% |
| 6 | 259.00 | 259.00 | 2575.15 | 1910.92 | 134.76% | 7.74 | 8.51 | 90.96% | 122.84 | 628.16 | 19.56% |
| 7 | 259.00 | 259.00 | 1908.28 | 1718.31 | 111.06% | 10.24 | 11.02 | 92.98% | 122.84 | 628.16 | 19.56% |
| 8 | 259.06 | 259.06 | 1592.62 | 1150.22 | 138.46% | 10.20 | 11.00 | 92.69% | 122.84 | 628.16 | 19.56% |
| 9 | 259.06 | 259.06 | 1176.76 | 989.29 | 118.95% | 15.25 | 15.98 | 95.45% | 122.84 | 628.16 | 19.56% |
| 10 | 259.06 | 259.06 | 1277.04 | 973.13 | 131.23% | 15.21 | 15.98 | 95.19% | 122.85 | 628.16 | 19.56% |
| 11 | 259.06 | 259.06 | 1034.90 | 887.05 | 116.67% | 15.24 | 15.99 | 95.31% | 122.85 | 628.16 | 19.56% |
| 12 | 259.06 | 259.06 | 1216.56 | 887.25 | 137.12% | 15.25 | 16.03 | 95.13% | 122.85 | 628.16 | 19.56% |
| 13 | 259.06 | 259.06 | 495.73 | 529.11 | 93.69% | 21.16 | 21.93 | 96.46% | 126.88 | 628.16 | 20.20% |
| 14 | 259.06 | 259.06 | 475.63 | 419.19 | 113.46% | 21.07 | 21.98 | 95.84% | 122.88 | 628.16 | 19.56% |
| 15 | 259.06 | 259.06 | 461.13 | 401.62 | 114.82% | 21.19 | 21.99 | 96.34% | 122.88 | 628.16 | 19.56% |
| 16 | 257.48 | 257.48 | 110.64 | 94.14 | 117.53% | 21.42 | 21.90 | 97.81% | 140.69 | 628.16 | 22.40% |
| 17 | 257.48 | 257.48 | 105.32 | 89.66 | 117.46% | 21.82 | 21.87 | 99.77% | 140.69 | 628.16 | 22.40% |
| 18 | 260.60 | 260.60 | 73.00 | 57.78 | 126.35% | 22.32 | 22.43 | 99.53% | 140.98 | 628.16 | 22.44% |
| 19 | 260.60 | 260.60 | 70.59 | 55.77 | 126.56% | 22.34 | 22.39 | 99.79% | 141.05 | 628.16 | 22.45% |
| 20 | 260.60 | 260.60 | 73.93 | 55.91 | 132.23% | 22.33 | 22.32 | 100.04% | 141.05 | 628.16 | 22.45% |
| 21 | 260.60 | 260.60 | 74.65 | 61.27 | 121.84% | 22.33 | 22.39 | 99.70% | 141.05 | 628.16 | 22.45% |
| 22 | 260.60 | 260.60 | 68.94 | 58.16 | 118.53% | 22.32 | 22.36 | 99.83% | 141.05 | 628.16 | 22.45% |

### Decompression

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4564.61 | 4079.31 | 111.90% | 5.86 | 5.98 | 97.91% | 123.28 | 202.89 | 60.76% |
| 2 | 9013.23 | 7097.63 | 126.99% | 5.86 | 5.96 | 98.30% | 123.28 | 202.89 | 60.76% |
| 3 | 9324.09 | 7238.40 | 128.81% | 5.85 | 5.98 | 97.78% | 123.28 | 202.89 | 60.76% |
| 4 | 9186.45 | 7083.16 | 129.69% | 5.84 | 5.94 | 98.36% | 123.28 | 202.89 | 60.76% |
| 5 | 8950.06 | 7146.41 | 125.24% | 5.85 | 5.99 | 97.72% | 123.28 | 202.89 | 60.76% |
| 6 | 25330.06 | 15385.43 | 164.64% | 5.86 | 5.86 | 99.93% | 123.28 | 202.89 | 60.76% |
| 7 | 25048.74 | 16094.52 | 155.64% | 5.86 | 5.95 | 98.36% | 123.28 | 202.89 | 60.76% |
| 8 | 26236.52 | 15595.25 | 168.23% | 5.82 | 5.86 | 99.27% | 123.28 | 202.89 | 60.76% |
| 9 | 25985.90 | 15881.42 | 163.62% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 10 | 25610.03 | 15742.89 | 162.68% | 5.82 | 5.86 | 99.33% | 123.28 | 202.89 | 60.76% |
| 11 | 24128.65 | 15866.92 | 152.07% | 5.82 | 5.83 | 99.87% | 123.28 | 202.89 | 60.76% |
| 12 | 25762.85 | 16032.57 | 160.69% | 5.85 | 5.86 | 99.80% | 123.28 | 202.89 | 60.76% |
| 13 | 25668.82 | 15891.19 | 161.53% | 5.86 | 5.98 | 97.85% | 123.28 | 202.89 | 60.76% |
| 14 | 23467.65 | 15664.24 | 149.82% | 5.86 | 5.86 | 99.87% | 123.28 | 202.89 | 60.76% |
| 15 | 26038.06 | 15666.83 | 166.20% | 5.85 | 5.95 | 98.36% | 123.28 | 202.89 | 60.76% |
| 16 | 19016.64 | 15893.32 | 119.65% | 5.80 | 5.86 | 98.93% | 123.28 | 202.89 | 60.76% |
| 17 | 18497.41 | 16044.83 | 115.29% | 5.86 | 5.86 | 99.93% | 123.28 | 202.89 | 60.76% |
| 18 | 15381.50 | 12806.39 | 120.11% | 5.82 | 5.98 | 97.26% | 123.28 | 202.89 | 60.76% |
| 19 | 15418.31 | 12581.12 | 122.55% | 5.82 | 5.98 | 97.19% | 123.28 | 202.89 | 60.76% |
| 20 | 15329.99 | 12609.52 | 121.57% | 5.80 | 5.98 | 97.00% | 123.28 | 202.89 | 60.76% |
| 21 | 14465.68 | 12686.28 | 114.03% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 22 | 15327.44 | 12629.16 | 121.37% | 5.80 | 5.99 | 96.93% | 123.28 | 202.89 | 60.76% |

### Trial variability

| Level | Operation | C++ min–max (MiB/s) | Reference min–max (MiB/s) | C++ paired wins / trials |
|---:|---|---:|---:|---:|
| 1 | Compression | 2242.45–2304.39 | 1907.55–2100.72 | 3 / 3 |
| 1 | Decompression | 4487.08–4583.70 | 4043.08–4129.56 | 3 / 3 |
| 2 | Compression | 2733.07–2780.92 | 2458.36–2486.74 | 3 / 3 |
| 2 | Decompression | 8796.57–9165.19 | 7078.17–7220.85 | 3 / 3 |
| 3 | Compression | 1780.98–1951.40 | 1656.85–1795.34 | 3 / 3 |
| 3 | Decompression | 9139.80–9367.28 | 6835.26–7245.57 | 3 / 3 |
| 4 | Compression | 1861.28–1976.98 | 1576.13–1592.20 | 3 / 3 |
| 4 | Decompression | 9112.85–9196.33 | 6997.03–7247.57 | 3 / 3 |
| 5 | Compression | 2205.11–2422.96 | 1938.94–2079.29 | 3 / 3 |
| 5 | Decompression | 8728.57–9139.67 | 6932.96–7249.83 | 3 / 3 |
| 6 | Compression | 2473.10–2604.04 | 1866.06–1921.42 | 3 / 3 |
| 6 | Decompression | 22601.99–25719.55 | 15259.70–15416.12 | 3 / 3 |
| 7 | Compression | 1677.13–2397.99 | 1679.70–1747.01 | 2 / 3 |
| 7 | Decompression | 23672.36–25713.23 | 15950.43–16179.18 | 3 / 3 |
| 8 | Compression | 1471.11–1598.50 | 1123.00–1174.69 | 3 / 3 |
| 8 | Decompression | 23160.54–27344.58 | 14774.27–16194.39 | 3 / 3 |
| 9 | Compression | 1126.85–1238.77 | 948.40–1044.91 | 3 / 3 |
| 9 | Decompression | 24760.31–26480.28 | 15848.07–16065.44 | 3 / 3 |
| 10 | Compression | 1219.86–1295.92 | 923.18–994.52 | 3 / 3 |
| 10 | Decompression | 25041.65–26057.19 | 15367.84–16214.24 | 3 / 3 |
| 11 | Compression | 1030.38–1083.06 | 792.95–928.84 | 3 / 3 |
| 11 | Decompression | 23780.93–24885.90 | 15585.99–16011.07 | 3 / 3 |
| 12 | Compression | 1049.64–1286.13 | 885.90–1013.70 | 3 / 3 |
| 12 | Decompression | 19642.10–26424.00 | 15199.78–16061.63 | 3 / 3 |
| 13 | Compression | 436.93–570.39 | 441.07–540.24 | 1 / 3 |
| 13 | Decompression | 23889.68–26620.39 | 14424.23–16027.81 | 3 / 3 |
| 14 | Compression | 437.81–614.11 | 413.61–456.38 | 2 / 3 |
| 14 | Decompression | 23418.71–25533.20 | 14976.98–16053.92 | 3 / 3 |
| 15 | Compression | 446.44–618.96 | 338.60–504.79 | 2 / 3 |
| 15 | Decompression | 25861.68–26843.81 | 15573.50–15980.61 | 3 / 3 |
| 16 | Compression | 96.84–117.11 | 87.20–103.02 | 2 / 3 |
| 16 | Decompression | 18308.01–19318.72 | 15010.00–16360.81 | 3 / 3 |
| 17 | Compression | 95.46–105.53 | 88.19–99.57 | 3 / 3 |
| 17 | Decompression | 18056.63–19745.41 | 15911.48–16247.58 | 3 / 3 |
| 18 | Compression | 67.12–76.29 | 55.27–60.60 | 3 / 3 |
| 18 | Decompression | 14939.92–15758.95 | 12722.84–12934.87 | 3 / 3 |
| 19 | Compression | 70.12–74.33 | 51.24–63.34 | 3 / 3 |
| 19 | Decompression | 15013.91–15504.53 | 12517.70–12722.68 | 3 / 3 |
| 20 | Compression | 65.55–78.52 | 51.20–56.04 | 3 / 3 |
| 20 | Decompression | 15243.49–15367.59 | 12407.74–12703.25 | 3 / 3 |
| 21 | Compression | 71.05–75.48 | 52.25–61.36 | 3 / 3 |
| 21 | Decompression | 13528.41–15710.83 | 12505.91–13034.03 | 3 / 3 |
| 22 | Compression | 65.13–77.16 | 53.17–66.34 | 3 / 3 |
| 22 | Decompression | 14644.18–15981.78 | 12618.97–12758.40 | 3 / 3 |

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
