# Benchmark results

These measurements compare optimized, isolated sph-zstd++ (`C++`) and bundled reference-zstd (`Reference`) workers at the same level and on the same input. Throughput is MiB/s (higher is better); memory is peak resident set size and code is the complete worker executable (lower is better). Percentages are `C++ / Reference × 100`. Compression outputs are byte-compared and both decoders check both encodings in paired runs.

The [current 22-level mixed-input evaluation](#current-evaluation) was run on 2026-11-01 from this checkout with GCC 13 Release. The [additional input matrix](BENCHMARKS_INPUTS.md) covers zero-filled, repeated, structured, and seeded-random data from the same optimized build. The earlier focused tables further below are dated snapshots from prior changes; their compilers and trial counts differ. The [experiment history](PERFORMANCE_HISTORY.md) preserves prior reports and rejected trials.

## Confirmed mixed-input level 13 compression

The short all-level screen below measured 77.24% of reference at level 13, but its three trials varied widely. A pinned-core follow-up used six alternating trials of 1,000 frames each on the same current optimized workers and 1-MiB mixed input. Every trial passed exact compressed-byte comparison and both decoding directions. The README uses this longer result for its level 13 mixed-input compression cell; the original screen remains below for provenance.

| C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 522.50 | 466.52 | 112.00% | 21.19 | 21.93 | 96.62% | 126.88 | 628.16 | 20.20% |

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
| 1 | 258.38 | 258.38 | 2304.61 | 2092.66 | 110.13% | 5.47 | 6.12 | 89.35% | 131.25 | 628.16 | 20.89% |
| 2 | 273.71 | 273.71 | 2789.86 | 2534.70 | 110.07% | 5.59 | 6.24 | 89.61% | 127.40 | 628.16 | 20.28% |
| 3 | 273.66 | 273.66 | 1843.22 | 1777.79 | 103.68% | 6.25 | 6.76 | 92.49% | 127.38 | 628.16 | 20.28% |
| 4 | 273.66 | 273.66 | 1908.70 | 1710.51 | 111.59% | 7.75 | 7.99 | 96.97% | 127.38 | 628.16 | 20.28% |
| 5 | 273.62 | 273.62 | 2411.06 | 2044.48 | 117.93% | 7.88 | 8.48 | 92.95% | 127.35 | 628.16 | 20.27% |
| 6 | 259.00 | 259.00 | 2278.97 | 1883.51 | 121.00% | 7.74 | 8.47 | 91.38% | 122.84 | 628.16 | 19.56% |
| 7 | 259.00 | 259.00 | 2196.75 | 1498.80 | 146.57% | 10.25 | 10.98 | 93.38% | 122.84 | 628.16 | 19.56% |
| 8 | 259.06 | 259.06 | 1297.78 | 1139.17 | 113.92% | 10.25 | 10.98 | 93.31% | 122.84 | 628.16 | 19.56% |
| 9 | 259.06 | 259.06 | 1144.78 | 1073.64 | 106.63% | 15.24 | 15.99 | 95.33% | 122.84 | 628.16 | 19.56% |
| 10 | 259.06 | 259.06 | 1014.16 | 813.11 | 124.73% | 15.24 | 15.98 | 95.40% | 122.85 | 628.16 | 19.56% |
| 11 | 259.06 | 259.06 | 1156.90 | 890.52 | 129.91% | 15.25 | 15.93 | 95.73% | 122.85 | 628.16 | 19.56% |
| 12 | 259.06 | 259.06 | 1101.14 | 854.42 | 128.88% | 15.25 | 15.98 | 95.41% | 122.85 | 628.16 | 19.56% |
| 13 | 259.06 | 259.06 | 282.92 | 333.01 | 84.96% | 21.19 | 22.00 | 96.32% | 126.88 | 628.16 | 20.20% |
| 14 | 259.06 | 259.06 | 463.41 | 429.79 | 107.82% | 21.19 | 21.94 | 96.58% | 122.88 | 628.16 | 19.56% |
| 15 | 259.06 | 259.06 | 481.31 | 454.28 | 105.95% | 21.19 | 21.93 | 96.60% | 122.88 | 628.16 | 19.56% |
| 16 | 257.48 | 257.48 | 100.31 | 95.80 | 104.71% | 21.45 | 21.86 | 98.14% | 140.69 | 628.16 | 22.40% |
| 17 | 257.48 | 257.48 | 97.76 | 84.60 | 115.56% | 21.84 | 21.87 | 99.89% | 140.69 | 628.16 | 22.40% |
| 18 | 260.60 | 260.60 | 73.36 | 53.52 | 137.07% | 22.36 | 22.37 | 99.95% | 141.05 | 628.16 | 22.45% |
| 19 | 260.60 | 260.60 | 68.37 | 48.49 | 140.98% | 22.32 | 22.37 | 99.79% | 141.05 | 628.16 | 22.45% |
| 20 | 260.60 | 260.60 | 71.60 | 57.75 | 123.98% | 22.33 | 22.39 | 99.74% | 141.05 | 628.16 | 22.45% |
| 21 | 260.60 | 260.60 | 70.21 | 57.45 | 122.23% | 22.33 | 22.41 | 99.62% | 141.05 | 628.16 | 22.45% |
| 22 | 260.60 | 260.60 | 74.96 | 47.94 | 156.38% | 22.32 | 22.36 | 99.83% | 141.05 | 628.16 | 22.45% |

### Decompression

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4537.18 | 4101.40 | 110.63% | 5.82 | 5.86 | 99.27% | 123.28 | 202.89 | 60.76% |
| 2 | 8874.52 | 7168.47 | 123.80% | 5.86 | 5.95 | 98.42% | 123.28 | 202.89 | 60.76% |
| 3 | 9399.36 | 7228.20 | 130.04% | 5.85 | 5.95 | 98.23% | 123.28 | 202.89 | 60.76% |
| 4 | 9373.89 | 7262.38 | 129.07% | 5.85 | 5.98 | 97.78% | 123.28 | 202.89 | 60.76% |
| 5 | 8934.88 | 7148.64 | 124.99% | 5.82 | 5.98 | 97.32% | 123.28 | 202.89 | 60.76% |
| 6 | 25173.13 | 16037.46 | 156.96% | 5.86 | 5.86 | 99.87% | 123.28 | 202.89 | 60.76% |
| 7 | 25093.71 | 16121.03 | 155.66% | 5.85 | 5.94 | 98.55% | 123.28 | 202.89 | 60.76% |
| 8 | 26285.79 | 15564.12 | 168.89% | 5.86 | 5.86 | 99.87% | 123.28 | 202.89 | 60.76% |
| 9 | 26138.35 | 16005.54 | 163.31% | 5.86 | 5.98 | 97.85% | 123.28 | 202.89 | 60.76% |
| 10 | 25358.85 | 15146.46 | 167.42% | 5.82 | 5.94 | 97.96% | 123.28 | 202.89 | 60.76% |
| 11 | 25533.09 | 16174.73 | 157.86% | 5.86 | 5.95 | 98.36% | 123.28 | 202.89 | 60.76% |
| 12 | 26395.87 | 16224.47 | 162.69% | 5.86 | 5.86 | 99.93% | 123.28 | 202.89 | 60.76% |
| 13 | 25320.43 | 15682.38 | 161.46% | 5.80 | 5.86 | 99.07% | 123.28 | 202.89 | 60.76% |
| 14 | 25780.37 | 15923.91 | 161.90% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 15 | 24558.35 | 15114.59 | 162.48% | 5.82 | 5.82 | 99.87% | 123.28 | 202.89 | 60.76% |
| 16 | 19622.74 | 16353.29 | 119.99% | 5.86 | 5.83 | 100.47% | 123.28 | 202.89 | 60.76% |
| 17 | 19891.80 | 15806.03 | 125.85% | 5.82 | 5.86 | 99.40% | 123.28 | 202.89 | 60.76% |
| 18 | 15702.99 | 12615.91 | 124.47% | 5.86 | 5.98 | 97.91% | 123.28 | 202.89 | 60.76% |
| 19 | 15451.74 | 12795.80 | 120.76% | 5.84 | 5.98 | 97.71% | 123.28 | 202.89 | 60.76% |
| 20 | 15536.19 | 12887.22 | 120.56% | 5.85 | 5.95 | 98.23% | 123.28 | 202.89 | 60.76% |
| 21 | 15683.64 | 13023.37 | 120.43% | 5.86 | 5.98 | 97.85% | 123.28 | 202.89 | 60.76% |
| 22 | 15531.64 | 12845.51 | 120.91% | 5.86 | 5.98 | 97.85% | 123.28 | 202.89 | 60.76% |

### Trial variability

| Level | Operation | C++ min–max (MiB/s) | Reference min–max (MiB/s) | C++ paired wins / trials |
|---:|---|---:|---:|---:|
| 1 | Compression | 2295.12–2407.92 | 1985.82–2145.23 | 3 / 3 |
| 1 | Decompression | 4408.84–4681.05 | 4082.22–4134.86 | 3 / 3 |
| 2 | Compression | 2776.91–2813.46 | 2513.57–2540.76 | 3 / 3 |
| 2 | Decompression | 8805.68–8985.98 | 3921.17–7218.09 | 3 / 3 |
| 3 | Compression | 1731.98–1949.47 | 1667.63–1797.61 | 2 / 3 |
| 3 | Decompression | 9284.17–9445.98 | 6971.09–7358.64 | 3 / 3 |
| 4 | Compression | 1855.03–1917.94 | 1679.13–1721.55 | 3 / 3 |
| 4 | Decompression | 9173.27–9551.61 | 7203.50–7331.86 | 3 / 3 |
| 5 | Compression | 2219.36–2412.44 | 2021.32–2129.62 | 3 / 3 |
| 5 | Decompression | 8755.62–9196.22 | 7016.62–7320.34 | 3 / 3 |
| 6 | Compression | 2216.26–2474.25 | 1706.07–1941.42 | 3 / 3 |
| 6 | Decompression | 25149.59–26183.18 | 15955.20–16212.37 | 3 / 3 |
| 7 | Compression | 2091.62–2313.53 | 1455.83–1811.30 | 3 / 3 |
| 7 | Decompression | 24433.42–25205.45 | 15925.30–16184.93 | 3 / 3 |
| 8 | Compression | 1280.91–1475.81 | 1134.46–1189.30 | 3 / 3 |
| 8 | Decompression | 24504.65–26288.25 | 15500.89–16020.59 | 3 / 3 |
| 9 | Compression | 1098.53–1145.49 | 983.45–1084.27 | 3 / 3 |
| 9 | Decompression | 24662.27–26667.29 | 14922.12–16229.09 | 3 / 3 |
| 10 | Compression | 972.78–1154.83 | 722.43–857.19 | 3 / 3 |
| 10 | Decompression | 24115.52–25577.37 | 14624.91–15871.27 | 3 / 3 |
| 11 | Compression | 975.43–1240.18 | 863.01–973.93 | 3 / 3 |
| 11 | Decompression | 24304.98–26050.39 | 15849.91–16176.75 | 3 / 3 |
| 12 | Compression | 1022.53–1105.94 | 841.67–882.52 | 3 / 3 |
| 12 | Decompression | 26084.59–26733.89 | 16210.07–16539.09 | 3 / 3 |
| 13 | Compression | 222.21–517.28 | 263.88–390.57 | 2 / 3 |
| 13 | Decompression | 24575.54–25561.04 | 14245.48–16300.88 | 3 / 3 |
| 14 | Compression | 400.26–540.51 | 401.64–436.84 | 2 / 3 |
| 14 | Decompression | 25634.07–26095.09 | 15515.03–16274.37 | 3 / 3 |
| 15 | Compression | 447.33–526.39 | 418.64–467.90 | 2 / 3 |
| 15 | Decompression | 23937.97–24571.63 | 14966.09–16289.30 | 3 / 3 |
| 16 | Compression | 97.60–105.95 | 74.79–99.43 | 3 / 3 |
| 16 | Decompression | 19544.23–19986.94 | 16314.40–16550.75 | 3 / 3 |
| 17 | Compression | 67.99–98.40 | 84.33–90.19 | 2 / 3 |
| 17 | Decompression | 18467.01–19948.36 | 14709.98–16211.46 | 3 / 3 |
| 18 | Compression | 69.15–73.78 | 46.35–59.06 | 3 / 3 |
| 18 | Decompression | 15609.36–15890.34 | 12052.36–12858.82 | 3 / 3 |
| 19 | Compression | 67.14–70.87 | 43.43–55.45 | 3 / 3 |
| 19 | Decompression | 15395.19–15519.25 | 12707.01–12959.61 | 3 / 3 |
| 20 | Compression | 70.97–74.28 | 53.35–63.45 | 3 / 3 |
| 20 | Decompression | 15497.58–15768.14 | 12553.48–12969.67 | 3 / 3 |
| 21 | Compression | 69.31–73.53 | 57.01–59.04 | 3 / 3 |
| 21 | Decompression | 14851.74–15864.14 | 12861.63–13040.29 | 3 / 3 |
| 22 | Compression | 63.63–77.23 | 38.72–59.73 | 3 / 3 |
| 22 | Decompression | 15349.83–16082.55 | 11623.17–12996.65 | 3 / 3 |

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
