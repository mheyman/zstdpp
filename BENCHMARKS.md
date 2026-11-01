# Benchmark results

These measurements compare optimized, isolated sph-zstd++ (`C++`) and bundled reference-zstd (`Reference`) workers at the same level and on the same input. Throughput is MiB/s (higher is better); memory is peak resident set size and code is the complete worker executable (lower is better). Percentages are `C++ / Reference × 100`. Compression outputs are byte-compared and both decoders check both encodings in paired runs.

The [current 22-level mixed-input evaluation](#current-evaluation) was run on 2026-11-01 from this checkout with GCC 13 Release. The [additional input matrix](BENCHMARKS_INPUTS.md) covers zero-filled, repeated, structured, and seeded-random data from the same optimized build. The earlier focused tables further below are dated snapshots from prior changes; their compilers and trial counts differ. The [experiment history](PERFORMANCE_HISTORY.md) preserves prior reports and rejected trials.

## Confirmed mixed-input level 13 compression

Short level-13 screens have varied widely. A pinned-core follow-up used six alternating trials of 1,000 frames each on optimized workers with the same unchanged level-13 compressor and 1-MiB mixed input. Every trial passed exact compressed-byte comparison and both decoding directions. The README uses this longer result for its level-13 mixed-input compression cell; the current short screen remains below for provenance.

| C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 522.50 | 466.52 | 112.00% | 21.19 | 21.93 | 96.62% | 126.88 | 628.16 | 20.20% |

## Confirmed mixed-input level 16 compression

The fresh short all-level screen was noisy at level 16. A pinned-core follow-up used six alternating trials of 2,000 frames each on the current optimized workers and the same 1-MiB mixed input. Every trial passed exact compressed-byte comparison and both decoding directions. The README uses this longer result for its level-16 mixed-input compression cell; the short-screen row remains below for provenance. Raw paired trials were generated locally under `out/packed-key-cache-20261101/confirm-pair-l16-mixed/`.

| C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 102.62 | 89.56 | 114.58% | 21.44 | 21.88 | 98.00% | 140.69 | 628.16 | 22.40% |

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
| 1 | 258.38 | 258.38 | 2337.74 | 2100.22 | 111.31% | 5.47 | 6.05 | 90.50% | 131.25 | 628.16 | 20.89% |
| 2 | 273.71 | 273.71 | 2734.78 | 2514.20 | 108.77% | 5.63 | 6.23 | 90.29% | 127.40 | 628.16 | 20.28% |
| 3 | 273.66 | 273.66 | 1877.40 | 1732.14 | 108.39% | 6.25 | 6.73 | 92.75% | 127.38 | 628.16 | 20.28% |
| 4 | 273.66 | 273.66 | 1912.47 | 1709.82 | 111.85% | 7.75 | 8.00 | 96.83% | 127.38 | 628.16 | 20.28% |
| 5 | 273.62 | 273.62 | 2356.44 | 2032.70 | 115.93% | 7.83 | 8.51 | 92.01% | 127.35 | 628.16 | 20.27% |
| 6 | 259.00 | 259.00 | 2598.44 | 1851.79 | 140.32% | 7.75 | 8.51 | 91.05% | 122.84 | 628.16 | 19.56% |
| 7 | 259.00 | 259.00 | 916.94 | 1777.66 | 51.58% | 10.21 | 10.99 | 92.93% | 122.84 | 628.16 | 19.56% |
| 8 | 259.06 | 259.06 | 1157.91 | 1000.25 | 115.76% | 10.25 | 11.02 | 93.05% | 122.84 | 628.16 | 19.56% |
| 9 | 259.06 | 259.06 | 1190.76 | 928.56 | 128.24% | 15.25 | 16.02 | 95.17% | 122.84 | 628.16 | 19.56% |
| 10 | 259.06 | 259.06 | 1165.65 | 926.68 | 125.79% | 15.10 | 15.98 | 94.48% | 122.85 | 628.16 | 19.56% |
| 11 | 259.06 | 259.06 | 1106.62 | 946.64 | 116.90% | 15.24 | 15.95 | 95.59% | 122.85 | 628.16 | 19.56% |
| 12 | 259.06 | 259.06 | 1203.35 | 911.77 | 131.98% | 15.25 | 15.99 | 95.38% | 122.85 | 628.16 | 19.56% |
| 13 | 259.06 | 259.06 | 566.09 | 420.46 | 134.64% | 21.19 | 21.97 | 96.44% | 126.88 | 628.16 | 20.20% |
| 14 | 259.06 | 259.06 | 508.48 | 422.00 | 120.49% | 21.19 | 21.97 | 96.43% | 122.88 | 628.16 | 19.56% |
| 15 | 259.06 | 259.06 | 524.38 | 437.54 | 119.85% | 21.14 | 21.98 | 96.18% | 122.88 | 628.16 | 19.56% |
| 16 | 257.48 | 257.48 | 99.20 | 109.65 | 90.47% | 21.41 | 21.89 | 97.82% | 140.69 | 628.16 | 22.40% |
| 17 | 257.48 | 257.48 | 103.34 | 86.06 | 120.08% | 21.79 | 21.87 | 99.64% | 140.69 | 628.16 | 22.40% |
| 18 | 260.60 | 260.60 | 67.81 | 52.55 | 129.04% | 22.33 | 22.35 | 99.90% | 141.05 | 628.16 | 22.45% |
| 19 | 260.60 | 260.60 | 69.07 | 59.55 | 115.98% | 22.33 | 22.33 | 99.98% | 141.05 | 628.16 | 22.45% |
| 20 | 260.60 | 260.60 | 73.39 | 55.12 | 133.16% | 22.20 | 22.38 | 99.21% | 141.05 | 628.16 | 22.45% |
| 21 | 260.60 | 260.60 | 74.11 | 57.58 | 128.70% | 22.32 | 22.38 | 99.72% | 141.05 | 628.16 | 22.45% |
| 22 | 260.60 | 260.60 | 73.32 | 55.99 | 130.95% | 22.32 | 22.34 | 99.91% | 141.05 | 628.16 | 22.45% |

### Decompression

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4383.47 | 4006.37 | 109.41% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 2 | 9126.52 | 7128.31 | 128.03% | 5.80 | 5.95 | 97.51% | 123.28 | 202.89 | 60.76% |
| 3 | 9283.43 | 7092.17 | 130.90% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 4 | 9237.42 | 7144.95 | 129.29% | 5.85 | 5.98 | 97.78% | 123.28 | 202.89 | 60.76% |
| 5 | 8952.40 | 7303.69 | 122.57% | 5.85 | 5.98 | 97.72% | 123.28 | 202.89 | 60.76% |
| 6 | 26271.76 | 15558.93 | 168.85% | 5.86 | 5.94 | 98.55% | 123.28 | 202.89 | 60.76% |
| 7 | 24979.90 | 15076.07 | 165.69% | 5.86 | 5.86 | 99.87% | 123.28 | 202.89 | 60.76% |
| 8 | 24899.30 | 15464.87 | 161.01% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 9 | 24569.42 | 15543.08 | 158.07% | 5.86 | 5.93 | 98.81% | 123.28 | 202.89 | 60.76% |
| 10 | 24810.81 | 15752.22 | 157.51% | 5.85 | 5.86 | 99.73% | 123.28 | 202.89 | 60.76% |
| 11 | 25812.63 | 16026.53 | 161.06% | 5.85 | 5.95 | 98.23% | 123.28 | 202.89 | 60.76% |
| 12 | 24149.14 | 15475.86 | 156.04% | 5.82 | 5.92 | 98.35% | 123.28 | 202.89 | 60.76% |
| 13 | 25335.66 | 15908.56 | 159.26% | 5.86 | 5.94 | 98.55% | 123.28 | 202.89 | 60.76% |
| 14 | 24728.20 | 15972.50 | 154.82% | 5.85 | 5.86 | 99.87% | 123.28 | 202.89 | 60.76% |
| 15 | 26120.33 | 15961.30 | 163.65% | 5.85 | 5.99 | 97.65% | 123.28 | 202.89 | 60.76% |
| 16 | 19738.00 | 16146.91 | 122.24% | 5.86 | 5.86 | 100.00% | 123.28 | 202.89 | 60.76% |
| 17 | 19300.30 | 15756.45 | 122.49% | 5.85 | 5.86 | 99.87% | 123.28 | 202.89 | 60.76% |
| 18 | 15931.97 | 12837.21 | 124.11% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 19 | 15229.38 | 12694.00 | 119.97% | 5.84 | 5.95 | 98.10% | 123.28 | 202.89 | 60.76% |
| 20 | 15502.02 | 12604.19 | 122.99% | 5.85 | 5.99 | 97.72% | 123.28 | 202.89 | 60.76% |
| 21 | 15779.44 | 12612.33 | 125.11% | 5.86 | 5.99 | 97.78% | 123.28 | 202.89 | 60.76% |
| 22 | 15801.24 | 12754.95 | 123.88% | 5.86 | 5.86 | 99.87% | 123.28 | 202.89 | 60.76% |

### Trial variability

| Level | Operation | C++ min–max (MiB/s) | Reference min–max (MiB/s) | C++ paired wins / trials |
|---:|---|---:|---:|---:|
| 1 | Compression | 2324.39–2371.32 | 2001.18–2108.21 | 3 / 3 |
| 1 | Decompression | 4275.63–4595.74 | 3530.44–4076.79 | 3 / 3 |
| 2 | Compression | 2581.28–2764.90 | 2500.26–2525.81 | 3 / 3 |
| 2 | Decompression | 9082.47–9162.34 | 6884.32–7317.77 | 3 / 3 |
| 3 | Compression | 1835.27–1941.88 | 1674.52–1757.40 | 3 / 3 |
| 3 | Decompression | 9238.81–9378.76 | 5622.23–7350.69 | 3 / 3 |
| 4 | Compression | 1790.15–1912.63 | 1705.08–1712.28 | 3 / 3 |
| 4 | Decompression | 8898.39–9339.06 | 6995.62–7149.41 | 3 / 3 |
| 5 | Compression | 2331.79–2369.17 | 1990.87–2145.09 | 3 / 3 |
| 5 | Decompression | 8805.56–9087.42 | 7256.46–7352.98 | 3 / 3 |
| 6 | Compression | 2523.27–2614.45 | 1726.08–1902.85 | 3 / 3 |
| 6 | Decompression | 26029.66–26627.75 | 15331.09–16161.23 | 3 / 3 |
| 7 | Compression | 728.95–2302.73 | 853.65–1779.36 | 2 / 3 |
| 7 | Decompression | 23982.53–25378.59 | 13411.07–15386.63 | 3 / 3 |
| 8 | Compression | 1140.07–1369.48 | 944.61–1129.65 | 3 / 3 |
| 8 | Decompression | 24775.57–25208.16 | 15163.13–15553.84 | 3 / 3 |
| 9 | Compression | 1178.00–1232.98 | 922.30–986.87 | 3 / 3 |
| 9 | Decompression | 24106.18–24947.75 | 15023.16–15978.40 | 3 / 3 |
| 10 | Compression | 1160.95–1299.99 | 925.89–1004.80 | 3 / 3 |
| 10 | Decompression | 24488.73–26444.21 | 15615.24–15845.95 | 3 / 3 |
| 11 | Compression | 1054.14–1318.71 | 919.63–962.75 | 3 / 3 |
| 11 | Decompression | 25169.47–26531.33 | 15531.87–16208.32 | 3 / 3 |
| 12 | Compression | 1160.11–1318.74 | 854.32–932.17 | 3 / 3 |
| 12 | Decompression | 22728.26–26401.05 | 14910.57–16275.58 | 3 / 3 |
| 13 | Compression | 420.70–601.94 | 385.09–506.07 | 3 / 3 |
| 13 | Decompression | 24798.00–25563.72 | 14255.17–16314.50 | 3 / 3 |
| 14 | Compression | 418.63–515.28 | 402.94–430.46 | 2 / 3 |
| 14 | Decompression | 24454.35–26941.92 | 14343.51–16242.88 | 3 / 3 |
| 15 | Compression | 465.52–581.33 | 433.06–546.34 | 2 / 3 |
| 15 | Decompression | 24096.80–26405.04 | 13784.30–16293.77 | 3 / 3 |
| 16 | Compression | 95.07–109.76 | 92.77–111.55 | 1 / 3 |
| 16 | Decompression | 19675.05–19974.57 | 15697.14–16327.14 | 3 / 3 |
| 17 | Compression | 99.11–114.05 | 84.08–108.81 | 3 / 3 |
| 17 | Decompression | 18095.24–19550.58 | 15159.63–16219.88 | 3 / 3 |
| 18 | Compression | 56.89–76.52 | 51.74–57.50 | 3 / 3 |
| 18 | Decompression | 15728.22–16026.70 | 12653.00–12949.65 | 3 / 3 |
| 19 | Compression | 67.02–76.11 | 48.66–66.76 | 3 / 3 |
| 19 | Decompression | 15203.50–15399.72 | 12619.89–12944.54 | 3 / 3 |
| 20 | Compression | 70.93–73.44 | 53.91–56.63 | 3 / 3 |
| 20 | Decompression | 15387.45–15945.08 | 12368.48–12957.57 | 3 / 3 |
| 21 | Compression | 73.47–78.91 | 41.73–59.04 | 3 / 3 |
| 21 | Decompression | 15736.34–16016.76 | 12415.69–12810.41 | 3 / 3 |
| 22 | Compression | 68.43–83.65 | 53.85–61.93 | 3 / 3 |
| 22 | Decompression | 14649.76–15853.06 | 12520.55–12863.71 | 3 / 3 |

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
