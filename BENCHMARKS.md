# Benchmark results

These measurements compare optimized, isolated sph-zstd++ (`C++`) and bundled reference-zstd (`Reference`) workers at the same level and on the same input. Throughput is MiB/s (higher is better); memory is peak resident set size and code is the complete worker executable (lower is better). Percentages are `C++ / Reference × 100`. Compression outputs are byte-compared and both decoders check both encodings in paired runs.

This is a record of the latest available measurements, **not one homogeneous all-level/all-corpus run**. Compiler, input size, and trial count vary by section. In particular, later source changes can supersede an earlier row; use the paired evaluator for an exact measurement of the current checkout. The [experiment history](PERFORMANCE_HISTORY.md) preserves every prior table and rejected trial. No values have been synthesized for unmeasured level/corpus combinations.

## Focused measurements on varied, balanced, structured, repeated, and mixed inputs

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

Six alternating Release `/O2 /Ob3 /Z7` trials of 10,000 reference-produced frames per row after the medium-block extra-bit-read change. The two workers decoded identical bytes. These are production-decoder results, unlike the older all-level table below.

| Input / operation | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Varied text / decode L3 | 10333.97 | 8908.93 | 116.00% | 6.34 | 6.34 | 99.97% | 234.00 | 311.00 | 75.24% |
| Varied text / decode L12 | 9728.95 | 8919.41 | 109.08% | 6.34 | 6.34 | 99.94% | 234.00 | 311.00 | 75.24% |
| Varied text / decode L16 | 3660.21 | 4996.38 | 73.26% | 6.34 | 6.34 | 100.00% | 234.00 | 311.00 | 75.24% |
| Mixed / decode L3 | 7536.46 | 5816.44 | 129.57% | 6.60 | 6.57 | 100.54% | 234.00 | 311.00 | 75.24% |
| Mixed / decode L12 | 23980.84 | 15725.24 | 152.50% | 6.59 | 6.55 | 100.63% | 234.00 | 311.00 | 75.24% |
| Mixed / decode L16 | 17887.03 | 15136.28 | 118.17% | 6.59 | 6.56 | 100.54% | 234.00 | 311.00 | 75.24% |

### GCC 13 decompression: levels 3 and 16, 1 MiB

Six alternating native-targeted Release trials per row after the single-load sequence-table change. The two workers decoded identical bytes.

| Native GCC 13 / decode, 1 MiB | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Level 16 / varied text | 4590.59 | 5671.00 | 80.95 | 5.69 | 5.72 | 99.59 | 191.34 | 170.12 | 112.47 |
| Level 3 / varied text | 10493.44 | 9855.97 | 106.47 | 5.64 | 5.72 | 98.50 | 191.34 | 170.12 | 112.47 |
| Level 3 / mixed | 9211.69 | 6972.97 | 132.11 | 5.89 | 5.95 | 98.98 | 191.34 | 170.12 | 112.47 |

## Last complete 22-level mixed-input screen

The following compression screen was collected on 2026-10-24 with GCC 13 Release `-O3`, one-MiB deterministic mixed input, six alternating trials per level, 300 frames per trial at levels 1–15 and 100 at levels 16–22. Every trial passed exact compressed-byte parity and reciprocal decoding. **It predates later retained optimizations**, so treat it as the last complete screen, not a current-checkout measurement.

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

## Prior generated all-level evaluation

The generated matrix below was captured with GNU 13.3.0 Release, six alternating trials of 2,000 timed iterations each on one-MiB deterministic mixed input. It predates later retained changes. More importantly, its decompression rows used an evaluator-only trusted-input decoder; they **do not establish production-decoder throughput**. A fresh full run should replace this block before using it to assess the current checkout.

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

<!-- SPH_ZSTDPP_EVAL_END -->

## How to reproduce

The full evaluator builds separate optimized workers for each implementation, operation, and configured level. On Windows:

```text
cmake --preset msvc-release-eval
cmake --build --preset msvc-release-eval
cmake --build --preset msvc-run-eval
```

The evaluator updates the marked generated section above and writes `metrics.csv`, `trials.csv`, worker CSVs, and `METRICS.md` to its results directory. It defaults to the configured levels 1, 3, 5, 7, 8, 9, and 15; set `SPH_ZSTDPP_EVAL_LEVELS` to the full semicolon-separated list `1;2;...;22` for a complete matrix. Its `--iterations`, `--trials`, `--output`, and `--report` options allow a fresh run without overwriting prior output. Benchmark both implementations with the same Release optimization and CPU target. Use alternating, unprofiled runs for throughput; profiler samples are for locating hotspots, not measuring speedups.

For a single level and input, build `msvc-pair`, `gcc-pair`, or `gcc-native-pair` and run `zstdpp_pair --level N --input FILE --iterations 1000 --trials 6`. The input must match the configured `SPH_ZSTDPP_EVAL_CORPUS_SIZE` (1 MiB by default). The pair tool saves exact frames and checks byte-level compression equality and both decoder directions on every trial.
