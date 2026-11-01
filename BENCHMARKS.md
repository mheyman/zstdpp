# Benchmark results

These measurements compare optimized, isolated sph-zstd++ (`C++`) and bundled reference-zstd (`Reference`) workers at the same level and on the same input. Throughput is MiB/s (higher is better); memory is peak resident set size and code is the complete worker executable (lower is better). Percentages are `C++ / Reference × 100`. Compression outputs are byte-compared and both decoders check both encodings in paired runs.

The [current 22-level mixed-input evaluation](#current-evaluation) was run on 2026-11-01 from this checkout with GCC 13 Release. The [full current matrix](BENCHMARKS_INPUTS.md) covers mixed, zero-filled, repeated, structured, and seeded-random data from the same optimized build. The earlier focused tables further below are dated snapshots from prior changes; their compilers and trial counts differ. The [experiment history](PERFORMANCE_HISTORY.md) preserves prior reports and rejected trials.

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
| 1 | 258.38 | 258.38 | 2289.47 | 2115.42 | 108.23% | 5.32 | 6.11 | 87.09% | 131.25 | 628.16 | 20.89% |
| 2 | 273.71 | 273.71 | 2821.55 | 2581.92 | 109.28% | 5.62 | 6.25 | 89.88% | 127.40 | 628.16 | 20.28% |
| 3 | 273.66 | 273.66 | 1925.06 | 1781.49 | 108.06% | 6.11 | 6.72 | 90.88% | 127.38 | 628.16 | 20.28% |
| 4 | 273.66 | 273.66 | 1916.78 | 1688.19 | 113.54% | 7.75 | 7.95 | 97.50% | 127.38 | 628.16 | 20.28% |
| 5 | 273.62 | 273.62 | 2397.26 | 2162.28 | 110.87% | 7.82 | 8.55 | 91.54% | 127.35 | 628.16 | 20.27% |
| 6 | 259.00 | 259.00 | 2289.70 | 1784.96 | 128.28% | 7.71 | 8.52 | 90.60% | 122.84 | 628.16 | 19.56% |
| 7 | 259.00 | 259.00 | 2137.48 | 1475.36 | 144.88% | 10.22 | 11.03 | 92.67% | 122.84 | 628.16 | 19.56% |
| 8 | 259.06 | 259.06 | 1401.04 | 1253.52 | 111.77% | 10.21 | 10.98 | 93.03% | 122.84 | 628.16 | 19.56% |
| 9 | 259.06 | 259.06 | 1184.14 | 992.69 | 119.29% | 15.25 | 15.98 | 95.41% | 122.84 | 628.16 | 19.56% |
| 10 | 259.06 | 259.06 | 1127.95 | 934.21 | 120.74% | 15.21 | 15.98 | 95.16% | 122.85 | 628.16 | 19.56% |
| 11 | 259.06 | 259.06 | 1025.40 | 874.25 | 117.29% | 15.25 | 16.02 | 95.22% | 122.85 | 628.16 | 19.56% |
| 12 | 259.06 | 259.06 | 1060.74 | 852.89 | 124.37% | 15.24 | 16.04 | 95.05% | 122.85 | 628.16 | 19.56% |
| 13 | 259.06 | 259.06 | 434.10 | 398.85 | 108.84% | 21.19 | 21.99 | 96.36% | 126.88 | 628.16 | 20.20% |
| 14 | 259.06 | 259.06 | 436.76 | 382.74 | 114.12% | 21.19 | 21.99 | 96.34% | 122.88 | 628.16 | 19.56% |
| 15 | 259.06 | 259.06 | 468.13 | 431.74 | 108.43% | 21.15 | 22.01 | 96.11% | 122.88 | 628.16 | 19.56% |
| 16 | 257.48 | 257.48 | 90.41 | 95.03 | 95.13% | 21.45 | 21.88 | 98.04% | 140.69 | 628.16 | 22.40% |
| 17 | 257.48 | 257.48 | 100.66 | 91.21 | 110.36% | 21.83 | 21.87 | 99.82% | 140.69 | 628.16 | 22.40% |
| 18 | 260.60 | 260.60 | 72.69 | 55.24 | 131.58% | 22.28 | 22.34 | 99.70% | 140.98 | 628.16 | 22.44% |
| 19 | 260.60 | 260.60 | 69.27 | 52.32 | 132.38% | 22.33 | 22.36 | 99.86% | 141.05 | 628.16 | 22.45% |
| 20 | 260.60 | 260.60 | 69.51 | 55.17 | 126.00% | 22.32 | 22.38 | 99.72% | 141.05 | 628.16 | 22.45% |
| 21 | 260.60 | 260.60 | 70.75 | 54.43 | 129.98% | 22.33 | 22.32 | 100.05% | 141.05 | 628.16 | 22.45% |
| 22 | 260.60 | 260.60 | 70.42 | 55.56 | 126.74% | 22.39 | 22.34 | 100.23% | 141.05 | 628.16 | 22.45% |

### Decompression

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4624.01 | 4130.61 | 111.94% | 5.85 | 5.86 | 99.87% | 123.28 | 202.89 | 60.76% |
| 2 | 9377.97 | 7088.28 | 132.30% | 5.82 | 5.98 | 97.32% | 123.28 | 202.89 | 60.76% |
| 3 | 9463.69 | 7270.91 | 130.16% | 5.85 | 5.98 | 97.84% | 123.28 | 202.89 | 60.76% |
| 4 | 9516.38 | 7253.91 | 131.19% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 5 | 8932.89 | 7089.72 | 126.00% | 5.89 | 5.98 | 98.43% | 123.28 | 202.89 | 60.76% |
| 6 | 27062.85 | 16120.34 | 167.88% | 5.80 | 5.95 | 97.51% | 123.28 | 202.89 | 60.76% |
| 7 | 26066.45 | 16005.23 | 162.86% | 5.86 | 5.98 | 97.91% | 123.28 | 202.89 | 60.76% |
| 8 | 27128.27 | 16169.54 | 167.77% | 5.86 | 5.95 | 98.36% | 123.28 | 202.89 | 60.76% |
| 9 | 26946.06 | 15830.38 | 170.22% | 5.85 | 5.95 | 98.29% | 123.28 | 202.89 | 60.76% |
| 10 | 26995.89 | 16024.42 | 168.47% | 5.85 | 5.86 | 99.73% | 123.28 | 202.89 | 60.76% |
| 11 | 27271.17 | 16267.38 | 167.64% | 5.86 | 5.93 | 98.68% | 123.28 | 202.89 | 60.76% |
| 12 | 27980.74 | 16350.40 | 171.13% | 5.86 | 5.82 | 100.54% | 123.28 | 202.89 | 60.76% |
| 13 | 27063.49 | 15957.93 | 169.59% | 5.86 | 5.95 | 98.36% | 123.28 | 202.89 | 60.76% |
| 14 | 26766.38 | 15812.74 | 169.27% | 5.85 | 5.86 | 99.73% | 123.28 | 202.89 | 60.76% |
| 15 | 26909.00 | 14845.98 | 181.25% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 16 | 19829.88 | 16301.72 | 121.64% | 5.85 | 5.95 | 98.23% | 123.28 | 202.89 | 60.76% |
| 17 | 18640.28 | 15982.66 | 116.63% | 5.82 | 5.83 | 99.87% | 123.28 | 202.89 | 60.76% |
| 18 | 15779.44 | 12901.00 | 122.31% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 19 | 15546.34 | 12692.20 | 122.49% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 20 | 15722.07 | 12458.34 | 126.20% | 5.86 | 5.98 | 97.91% | 123.28 | 202.89 | 60.76% |
| 21 | 16198.94 | 12853.68 | 126.03% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 22 | 16191.90 | 12780.61 | 126.69% | 5.86 | 5.95 | 98.42% | 123.28 | 202.89 | 60.76% |

### Trial variability

| Level | Operation | C++ min–max (MiB/s) | Reference min–max (MiB/s) | C++ paired wins / trials |
|---:|---|---:|---:|---:|
| 1 | Compression | 2286.86–2299.14 | 2032.44–2171.42 | 3 / 3 |
| 1 | Decompression | 4453.96–4649.82 | 4101.49–4160.17 | 3 / 3 |
| 2 | Compression | 2801.34–2879.41 | 2572.33–2596.96 | 3 / 3 |
| 2 | Decompression | 9171.42–9531.56 | 7044.26–7305.20 | 3 / 3 |
| 3 | Compression | 1911.44–1928.64 | 1729.34–1794.11 | 3 / 3 |
| 3 | Decompression | 9389.14–9485.78 | 6986.55–7394.27 | 3 / 3 |
| 4 | Compression | 1860.48–1988.97 | 1491.01–1736.63 | 3 / 3 |
| 4 | Decompression | 9515.77–9559.14 | 7161.88–7397.77 | 3 / 3 |
| 5 | Compression | 2264.63–2407.41 | 2134.86–2165.78 | 3 / 3 |
| 5 | Decompression | 8842.40–9050.03 | 7008.51–7309.92 | 3 / 3 |
| 6 | Compression | 2239.32–2349.33 | 1783.99–1951.10 | 3 / 3 |
| 6 | Decompression | 25513.10–27566.15 | 16031.54–16151.22 | 3 / 3 |
| 7 | Compression | 1973.12–2349.51 | 1414.04–1719.15 | 3 / 3 |
| 7 | Decompression | 24876.15–26153.30 | 15982.85–16072.26 | 3 / 3 |
| 8 | Compression | 1293.06–1433.95 | 1234.54–1265.66 | 3 / 3 |
| 8 | Decompression | 26250.96–28350.20 | 15822.16–16233.64 | 3 / 3 |
| 9 | Compression | 1162.98–1213.59 | 932.45–1028.10 | 3 / 3 |
| 9 | Decompression | 26184.59–27722.63 | 15422.85–15994.05 | 3 / 3 |
| 10 | Compression | 1115.66–1178.64 | 918.64–987.21 | 3 / 3 |
| 10 | Decompression | 23914.95–27441.84 | 15824.69–16242.92 | 3 / 3 |
| 11 | Compression | 997.68–1271.42 | 872.31–929.92 | 3 / 3 |
| 11 | Decompression | 24937.53–28552.49 | 14175.24–16462.39 | 3 / 3 |
| 12 | Compression | 1020.13–1164.24 | 799.90–880.73 | 3 / 3 |
| 12 | Decompression | 27207.85–28215.75 | 15920.63–16366.60 | 3 / 3 |
| 13 | Compression | 415.80–461.63 | 376.77–435.95 | 3 / 3 |
| 13 | Decompression | 26961.58–27166.68 | 15666.72–16001.17 | 3 / 3 |
| 14 | Compression | 431.13–489.18 | 378.91–392.56 | 3 / 3 |
| 14 | Decompression | 25623.11–27062.05 | 14768.73–16138.25 | 3 / 3 |
| 15 | Compression | 446.90–469.81 | 405.10–451.93 | 2 / 3 |
| 15 | Decompression | 20192.80–27565.97 | 13012.21–15896.42 | 3 / 3 |
| 16 | Compression | 83.07–98.93 | 91.80–95.04 | 1 / 3 |
| 16 | Decompression | 19610.11–19845.30 | 15997.63–16379.55 | 3 / 3 |
| 17 | Compression | 91.49–103.84 | 67.76–94.94 | 3 / 3 |
| 17 | Decompression | 15973.20–19626.45 | 15554.33–16255.60 | 3 / 3 |
| 18 | Compression | 69.37–72.80 | 53.19–55.73 | 3 / 3 |
| 18 | Decompression | 14891.15–15993.57 | 12863.72–12917.90 | 3 / 3 |
| 19 | Compression | 69.05–69.44 | 51.34–53.28 | 3 / 3 |
| 19 | Decompression | 15541.07–16121.76 | 12126.91–12875.38 | 3 / 3 |
| 20 | Compression | 60.65–69.65 | 49.53–55.59 | 3 / 3 |
| 20 | Decompression | 15596.57–16210.32 | 12358.07–12768.92 | 3 / 3 |
| 21 | Compression | 66.53–71.51 | 53.42–56.68 | 3 / 3 |
| 21 | Decompression | 15916.49–16855.00 | 12699.63–12880.34 | 3 / 3 |
| 22 | Compression | 69.65–72.36 | 53.54–56.64 | 3 / 3 |
| 22 | Decompression | 16059.61–16315.34 | 12773.47–12836.45 | 3 / 3 |

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
