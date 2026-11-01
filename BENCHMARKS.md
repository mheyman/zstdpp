# Benchmark results

These measurements compare optimized, isolated sph-zstd++ (`C++`) and bundled reference-zstd (`Reference`) workers at the same level and on the same input. Throughput is MiB/s (higher is better); memory is peak resident set size and code is the complete worker executable (lower is better). Percentages are `C++ / Reference × 100`. Compression outputs are byte-compared and both decoders check both encodings in paired runs.

The [current 22-level mixed-input evaluation](#current-evaluation) was run on 2026-11-01 from this checkout with GCC 13 Release. The [additional input matrix](BENCHMARKS_INPUTS.md) covers zero-filled, repeated, structured, and seeded-random data from the same optimized build. The focused tables immediately below are dated snapshots from earlier changes; their compilers and trial counts differ. The [experiment history](PERFORMANCE_HISTORY.md) preserves prior reports and rejected trials.

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
| 1 | 258.38 | 258.38 | 2212.14 | 1969.03 | 112.35% | 5.51 | 6.14 | 89.69% | 131.25 | 628.16 | 20.89% |
| 2 | 273.71 | 273.71 | 2730.81 | 2449.08 | 111.50% | 5.62 | 6.25 | 89.94% | 127.40 | 628.16 | 20.28% |
| 3 | 273.66 | 273.66 | 1753.56 | 1689.40 | 103.80% | 6.25 | 6.78 | 92.22% | 127.38 | 628.16 | 20.28% |
| 4 | 273.66 | 273.66 | 1657.29 | 1502.28 | 110.32% | 7.72 | 7.92 | 97.48% | 127.38 | 628.16 | 20.28% |
| 5 | 273.62 | 273.62 | 2091.40 | 1863.94 | 112.20% | 7.84 | 8.53 | 91.98% | 127.35 | 628.16 | 20.27% |
| 6 | 259.00 | 259.00 | 2263.18 | 1568.39 | 144.30% | 7.70 | 8.54 | 90.12% | 122.84 | 628.16 | 19.56% |
| 7 | 259.00 | 259.00 | 1302.14 | 1282.26 | 101.55% | 10.24 | 10.97 | 93.34% | 122.84 | 628.16 | 19.56% |
| 8 | 259.06 | 259.06 | 1250.18 | 1068.63 | 116.99% | 10.21 | 10.98 | 93.03% | 122.84 | 628.16 | 19.56% |
| 9 | 259.06 | 259.06 | 860.90 | 777.90 | 110.67% | 15.24 | 16.02 | 95.12% | 122.84 | 628.16 | 19.56% |
| 10 | 259.06 | 259.06 | 967.40 | 696.46 | 138.90% | 15.11 | 16.00 | 94.43% | 122.85 | 628.16 | 19.56% |
| 11 | 259.06 | 259.06 | 1004.13 | 764.39 | 131.36% | 15.17 | 16.02 | 94.71% | 122.85 | 628.16 | 19.56% |
| 12 | 259.06 | 259.06 | 849.56 | 717.70 | 118.37% | 15.21 | 16.03 | 94.93% | 122.85 | 628.16 | 19.56% |
| 13 | 259.06 | 259.06 | 209.93 | 271.78 | 77.24% | 21.19 | 21.95 | 96.55% | 126.88 | 628.16 | 20.20% |
| 14 | 259.06 | 259.06 | 328.72 | 320.32 | 102.62% | 21.19 | 21.97 | 96.44% | 122.88 | 628.16 | 19.56% |
| 15 | 259.06 | 259.06 | 412.81 | 286.51 | 144.08% | 21.18 | 21.97 | 96.41% | 122.88 | 628.16 | 19.56% |
| 16 | 257.48 | 257.48 | 74.55 | 66.56 | 112.00% | 21.46 | 21.87 | 98.12% | 140.69 | 628.16 | 22.40% |
| 17 | 257.48 | 257.48 | 65.23 | 66.25 | 98.46% | 21.83 | 21.93 | 99.54% | 140.69 | 628.16 | 22.40% |
| 18 | 260.60 | 260.60 | 49.02 | 37.44 | 130.94% | 22.21 | 22.32 | 99.49% | 141.05 | 628.16 | 22.45% |
| 19 | 260.60 | 260.60 | 56.31 | 37.70 | 149.35% | 22.20 | 22.35 | 99.34% | 141.05 | 628.16 | 22.45% |
| 20 | 260.60 | 260.60 | 59.60 | 40.09 | 148.66% | 22.33 | 22.47 | 99.36% | 141.05 | 628.16 | 22.45% |
| 21 | 260.60 | 260.60 | 59.32 | 39.92 | 148.60% | 22.35 | 22.43 | 99.63% | 141.05 | 628.16 | 22.45% |
| 22 | 260.60 | 260.60 | 56.02 | 40.20 | 139.35% | 22.32 | 22.31 | 100.05% | 141.05 | 628.16 | 22.45% |

### Decompression

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4729.65 | 3942.73 | 119.96% | 5.85 | 5.86 | 99.80% | 123.38 | 202.89 | 60.81% |
| 2 | 9161.11 | 7073.68 | 129.51% | 5.82 | 5.99 | 97.20% | 123.38 | 202.89 | 60.81% |
| 3 | 9111.29 | 6920.32 | 131.66% | 5.85 | 5.95 | 98.29% | 123.38 | 202.89 | 60.81% |
| 4 | 9032.71 | 7091.48 | 127.37% | 5.86 | 5.99 | 97.78% | 123.38 | 202.89 | 60.81% |
| 5 | 9172.88 | 7103.00 | 129.14% | 5.82 | 5.93 | 98.09% | 123.38 | 202.89 | 60.81% |
| 6 | 25774.81 | 15104.52 | 170.64% | 5.82 | 5.98 | 97.26% | 123.38 | 202.89 | 60.81% |
| 7 | 25254.92 | 15247.41 | 165.63% | 5.86 | 5.99 | 97.78% | 123.38 | 202.89 | 60.81% |
| 8 | 26568.82 | 15608.62 | 170.22% | 5.86 | 5.86 | 99.87% | 123.38 | 202.89 | 60.81% |
| 9 | 24644.94 | 14666.62 | 168.03% | 5.86 | 5.82 | 100.54% | 123.38 | 202.89 | 60.81% |
| 10 | 25050.42 | 15476.63 | 161.86% | 5.84 | 5.95 | 98.10% | 123.38 | 202.89 | 60.81% |
| 11 | 26214.14 | 15489.73 | 169.24% | 5.86 | 5.86 | 99.87% | 123.38 | 202.89 | 60.81% |
| 12 | 25822.78 | 15411.08 | 167.56% | 5.86 | 5.95 | 98.36% | 123.38 | 202.89 | 60.81% |
| 13 | 25109.50 | 15647.61 | 160.47% | 5.86 | 5.95 | 98.36% | 123.38 | 202.89 | 60.81% |
| 14 | 26479.48 | 15755.59 | 168.06% | 5.84 | 5.96 | 97.97% | 123.38 | 202.89 | 60.81% |
| 15 | 25870.96 | 15818.97 | 163.54% | 5.84 | 5.98 | 97.65% | 123.38 | 202.89 | 60.81% |
| 16 | 20702.84 | 16252.78 | 127.38% | 5.86 | 5.82 | 100.67% | 123.38 | 202.89 | 60.81% |
| 17 | 19542.39 | 15283.46 | 127.87% | 5.86 | 5.93 | 98.68% | 123.38 | 202.89 | 60.81% |
| 18 | 14151.68 | 12827.62 | 110.32% | 5.89 | 5.98 | 98.43% | 123.38 | 202.89 | 60.81% |
| 19 | 14232.41 | 12742.33 | 111.69% | 5.85 | 5.95 | 98.29% | 123.38 | 202.89 | 60.81% |
| 20 | 14434.15 | 13078.71 | 110.36% | 5.85 | 5.95 | 98.23% | 123.38 | 202.89 | 60.81% |
| 21 | 14082.48 | 12255.15 | 114.91% | 5.86 | 5.99 | 97.78% | 123.38 | 202.89 | 60.81% |
| 22 | 14496.39 | 12948.02 | 111.96% | 5.82 | 5.98 | 97.32% | 123.38 | 202.89 | 60.81% |

### Trial variability

| Level | Operation | C++ min–max (MiB/s) | Reference min–max (MiB/s) | C++ paired wins / trials |
|---:|---|---:|---:|---:|
| 1 | Compression | 2014.46–2226.59 | 1767.20–2084.26 | 3 / 3 |
| 1 | Decompression | 4027.09–4812.64 | 3491.94–3990.51 | 3 / 3 |
| 2 | Compression | 2697.03–2757.29 | 2291.44–2453.42 | 3 / 3 |
| 2 | Decompression | 8656.52–9214.55 | 6907.54–7092.11 | 3 / 3 |
| 3 | Compression | 1745.26–1883.90 | 1439.13–1713.35 | 3 / 3 |
| 3 | Decompression | 8816.79–9149.05 | 6902.00–7146.33 | 3 / 3 |
| 4 | Compression | 1637.50–1747.47 | 1344.98–1508.46 | 3 / 3 |
| 4 | Decompression | 8784.03–9159.47 | 6830.30–7116.95 | 3 / 3 |
| 5 | Compression | 1843.67–2200.44 | 1836.84–1865.80 | 2 / 3 |
| 5 | Decompression | 9135.26–9254.12 | 7047.95–7121.16 | 3 / 3 |
| 6 | Compression | 1445.09–2360.08 | 1560.42–1641.89 | 2 / 3 |
| 6 | Decompression | 24923.50–26256.97 | 14209.84–15624.97 | 3 / 3 |
| 7 | Compression | 1058.72–1983.03 | 1238.03–1301.55 | 2 / 3 |
| 7 | Decompression | 24439.96–25413.85 | 14698.62–15499.99 | 3 / 3 |
| 8 | Compression | 1051.93–1382.57 | 1024.26–1107.01 | 2 / 3 |
| 8 | Decompression | 25559.39–26797.12 | 14041.83–15870.24 | 3 / 3 |
| 9 | Compression | 855.16–924.17 | 743.65–868.06 | 2 / 3 |
| 9 | Decompression | 21837.80–26273.79 | 13211.15–15788.35 | 3 / 3 |
| 10 | Compression | 944.10–1049.67 | 654.34–719.97 | 3 / 3 |
| 10 | Decompression | 23659.26–25315.15 | 13399.40–15561.23 | 3 / 3 |
| 11 | Compression | 656.87–1020.01 | 734.70–774.54 | 2 / 3 |
| 11 | Decompression | 25300.46–26470.99 | 15433.68–15595.67 | 3 / 3 |
| 12 | Compression | 546.22–1039.29 | 520.06–806.06 | 3 / 3 |
| 12 | Decompression | 22562.25–26002.50 | 14680.73–15844.42 | 3 / 3 |
| 13 | Compression | 174.36–393.14 | 242.84–335.41 | 1 / 3 |
| 13 | Decompression | 24113.37–26251.98 | 15220.41–16013.01 | 3 / 3 |
| 14 | Compression | 316.06–364.36 | 300.76–360.92 | 2 / 3 |
| 14 | Decompression | 25167.62–27022.01 | 15427.27–15983.79 | 3 / 3 |
| 15 | Compression | 309.43–425.30 | 226.74–307.99 | 3 / 3 |
| 15 | Decompression | 24333.06–27021.43 | 15417.54–16008.48 | 3 / 3 |
| 16 | Compression | 69.80–76.37 | 63.22–70.20 | 3 / 3 |
| 16 | Decompression | 20600.63–20903.85 | 15490.50–16346.34 | 3 / 3 |
| 17 | Compression | 63.36–71.65 | 53.64–72.50 | 2 / 3 |
| 17 | Decompression | 19069.52–19979.26 | 15153.94–15393.11 | 3 / 3 |
| 18 | Compression | 45.48–52.19 | 34.85–40.12 | 3 / 3 |
| 18 | Decompression | 14078.07–14381.18 | 12116.01–12853.52 | 3 / 3 |
| 19 | Compression | 55.32–58.19 | 37.38–40.17 | 3 / 3 |
| 19 | Decompression | 14226.61–14465.46 | 12248.28–12815.45 | 3 / 3 |
| 20 | Compression | 59.41–59.94 | 37.64–43.55 | 3 / 3 |
| 20 | Decompression | 13603.35–14517.04 | 12885.26–13082.27 | 3 / 3 |
| 21 | Compression | 51.36–61.09 | 38.14–43.58 | 3 / 3 |
| 21 | Decompression | 14074.41–14651.74 | 12253.92–12619.85 | 3 / 3 |
| 22 | Compression | 55.87–58.00 | 35.72–43.95 | 3 / 3 |
| 22 | Decompression | 14480.81–14690.42 | 12135.65–12967.17 | 3 / 3 |

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
