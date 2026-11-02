# Benchmark results

These measurements compare optimized, isolated sph-zstd++ (`C++`) and bundled reference-zstd (`Reference`) workers at the same level and on the same input. Throughput is MiB/s (higher is better); memory is peak resident set size and code is the complete worker executable (lower is better). Percentages are `C++ / Reference × 100`. Compression outputs are byte-compared and both decoders check both encodings in paired runs.

The [current 22-level mixed-input evaluation](#current-evaluation) was run on 2026-11-01 from this checkout with GCC 13 Release. The [full current matrix](BENCHMARKS_INPUTS.md) covers mixed, zero-filled, repeated, structured, and seeded-random data from the same optimized build. The earlier focused tables further below are dated snapshots from prior changes; their compilers and trial counts differ. The [experiment history](PERFORMANCE_HISTORY.md) preserves prior reports and rejected trials.

## Confirmed level-2 double-RLE sequence writer

The dense grouped writer now uses a block-level path when both literal-length and offset FSE tables are run-length. Those states emit no bits, so the path advances only the match-length FSE state and keeps the same extra-bit output. A phase trace found the double run-length shape in most structured level-2 blocks. The hinted production build gained 6.03% median against the saved optimized C++ binary on six alternating 3,000-frame structured pairs (6/6 wins); mixed gained 4.12% (6/6). Equal-work `perf` counters found 5.65% fewer instructions and cycles on structured input. Zero-filled input lost 3.50% in old/new pairs but remains far faster than reference. The unhinted layout was rejected after a 0.47% median structured loss. Reports and counters are under `out/l2-double-rle-likely-20261101/` and `out/l2-double-rle-20261101/`.

The table uses six alternating candidate/reference trials per input. Structured, mixed, and random used 3,000 frames per trial; zero and repeated used 10,000. Every compressed frame matched the reference, both decoders recovered both encodings, and optimized GCC CTest passed 12/12. The structured lead over reference is narrow and should be repeated on other machines.

| Input | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| zero | 41707.11 | 8591.99 | 485.42% | 5.23 | 6.09 | 85.89% | 127.72 | 628.16 | 20.33% |
| repeated | 26415.17 | 9842.97 | 268.37% | 5.21 | 6.13 | 84.94% | 127.72 | 628.16 | 20.33% |
| structured | 853.39 | 836.53 | 102.02% | 5.34 | 6.23 | 85.71% | 127.72 | 628.16 | 20.33% |
| random | 3715.75 | 3089.79 | 120.26% | 6.23 | 6.21 | 100.35% | 127.72 | 628.16 | 20.33% |
| mixed | 2951.10 | 2562.09 | 115.18% | 5.59 | 6.24 | 89.64% | 127.72 | 628.16 | 20.33% |

## Confirmed level-1 common sequence writer

The compact collected-literal writer skips LL and OF extra-bit appends for the common record whose literal and offset codes are both zero. Its FSE state bits are still emitted. A saved-binary comparison with exact reference frames gained 2.30% median on six alternating 3,000-frame structured pairs (5/6 wins) and 2.44% on seven 1,000-frame mixed pairs (7/7 wins). A larger bit-writer output chunk was tried separately and reverted after a 0.56% median loss on seven structured pairs. The optimized GCC acceptance suite passed 12/12. Raw reports are under `out/l1-common-bit-20261101/`.

The table uses six alternating candidate/reference trials per input. Structured, mixed, and random used 3,000 frames per trial; zero and repeated used 10,000. Every compressed frame matched reference bytes, and both decoders recovered both encodings. The structured lead is narrow and sensitive to timing variation.

| Input | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| zero | 41528.33 | 8534.57 | 486.59% | 5.11 | 5.87 | 87.12% | 131.25 | 628.16 | 20.89% |
| repeated | 25518.54 | 9896.44 | 257.86% | 5.09 | 5.86 | 86.84% | 131.25 | 628.16 | 20.89% |
| structured | 676.36 | 667.89 | 101.27% | 5.23 | 6.13 | 85.25% | 131.25 | 628.16 | 20.89% |
| random | 3849.93 | 2972.76 | 129.51% | 6.08 | 5.99 | 101.50% | 131.25 | 628.16 | 20.89% |
| mixed | 2407.44 | 2127.73 | 113.15% | 5.51 | 6.12 | 89.98% | 131.25 | 628.16 | 20.89% |

## Confirmed direct offBase level 5 compression

The GCC 13 greedy parser now stores the encoded `offBase` in its existing 12-byte sequence record. The encoder reads that value in its code-gathering and reverse-bitstream passes. With the final source form, six alternating saved-binary old/new pairs gained 8.60% median on 3,000 structured frames and 6.23% on 10,000 mixed frames; both won 6/6. Repeated input was nearly unchanged (-0.05%, 3/6) over 10,000 frames, random was +0.13% (4/6) over 3,000, and zero lost 2.29% (1/6) over 10,000 while remaining more than twice as fast as reference. Every trial emitted the exact reference frame. Optimized GCC CTest passed 12/12, and a unit control checks the direct record against the logical sequence fields. The rebuilt level-6 worker's `.text` section matches its saved baseline exactly.

The following independent final-candidate/reference pairs checked exact compressed bytes and both decoders on both encodings. Structured input used six 10,000-frame trials and measured 102.56% of reference. Zero and repeated used 10,000 frames per trial; random and mixed used 3,000. Results remain sensitive to timing variation, especially the narrow structured lead. Raw final reports are under `out/l5-direct-offbase-final-20261101/`; the earlier code-shape trial is under `out/l5-direct-offbase-20261101/`.

| Input | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| zero | 9622.57 | 4675.01 | 205.83% | 7.50 | 8.38 | 89.58% | 127.27 | 628.16 | 20.26% |
| repeated | 8248.76 | 4925.82 | 167.46% | 7.51 | 8.38 | 89.67% | 127.27 | 628.16 | 20.26% |
| structured | 1040.24 | 1014.27 | 102.56% | 7.72 | 8.47 | 91.08% | 127.27 | 628.16 | 20.26% |
| random | 2373.86 | 1704.49 | 139.27% | 8.50 | 8.47 | 100.37% | 127.27 | 628.16 | 20.26% |
| mixed | 2231.56 | 1877.98 | 118.83% | 7.85 | 8.49 | 92.44% | 127.27 | 628.16 | 20.26% |

## Confirmed random-input level 15 compression

The two-trial 100-frame screen reported 78.0% of reference, but a longer pinned-core comparison did not reproduce that gap. Six alternating 3,000-frame trials used the same optimized workers and deterministic 1-MiB random input. Five of six paired throughput ratios favored C++; the one contrary trial shows that timing remains variable. Every trial produced byte-identical compressed frames, and both decoders recovered both encodings. The README and full input matrix use the longer median result. Raw trials are under `out/combined-medium-extras-20261101/confirm-random-l15-long/`.

| C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 183.52 | 165.29 | 111.03% | 21.89 | 21.98 | 99.63% | 122.88 | 628.16 | 19.56% |

## Earlier greedy level 5 match-counter result

GCC 13's level-5 greedy row matcher now uses a simple one-word match counter. Six alternating saved-binary old/new pairs of 10,000 frames each produced byte-identical reference frames on every input. Median changes on the final GCC-only build were structured +0.86% (6/6 wins), mixed +1.15% (3/6, with one large timing outlier), repeated -0.69% (2/6), random +5.25% (5/6), and zero +0.82% (5/6). The repeated path remains well ahead of reference; the structured path remains below it. The optimized GCC acceptance suite passed 12/12. The following independent C++/reference comparisons used six alternating 3,000-frame trials per input and checked exact frames plus both decoding directions. Raw reports are under `out/greedy-count-loop-20261101/`.

| Input | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| zero | 9903.09 | 4811.06 | 205.84% | 7.50 | 8.35 | 89.85% | 127.27 | 628.16 | 20.26% |
| repeated | 8599.83 | 5140.86 | 167.28% | 7.50 | 8.37 | 89.64% | 127.27 | 628.16 | 20.26% |
| structured | 974.38 | 1028.87 | 94.70% | 7.75 | 8.53 | 90.95% | 127.27 | 628.16 | 20.26% |
| random | 2443.77 | 1732.20 | 141.08% | 8.47 | 8.40 | 100.86% | 127.27 | 628.16 | 20.26% |
| mixed | 2426.89 | 2134.02 | 113.72% | 7.88 | 8.54 | 92.23% | 127.27 | 628.16 | 20.26% |

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

Six alternating, CPU-pinned trials of 100,000 frames per level used the current optimized decoder workers and reference-produced 1-MiB random frames. Both workers verified the decoded output, and the C++ worker checked for allocations after warmup. This batch is above reference, but previous long batches were near parity; individual trial ratios remain sensitive to run conditions. Raw CSVs are under `out/decoder-bytecopy-20261101/confirmed-random-decode/`.

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3 | 40212.98 | 38585.74 | 104.22% | 13.91 | 13.91 | 100.00% | 123.45 | 202.89 | 60.85% |
| 4 | 41986.45 | 38227.79 | 109.83% | 13.91 | 13.91 | 100.00% | 123.45 | 202.89 | 60.85% |

## Confirmed structured-input decompression

Six alternating, CPU-pinned trials of 10,000 frames per level used the current optimized decoder and reference workers on reference-produced 1-MiB structured frames. Both workers verified the output, and the C++ worker checked for allocations after warmup. These longer results supersede the short-screen decoder rows for levels 16–22 in the README and input matrix. Raw CSVs are under `out/decoder-bytecopy-20261101/confirmed-structured-decode/`.

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 20455.03 | 22482.93 | 90.98% | 13.99 | 13.99 | 100.00% | 123.45 | 202.89 | 60.85% |
| 17 | 20698.56 | 22922.76 | 90.30% | 13.99 | 13.99 | 100.00% | 123.45 | 202.89 | 60.85% |
| 18 | 17504.80 | 19590.70 | 89.35% | 13.99 | 13.99 | 100.00% | 123.45 | 202.89 | 60.85% |
| 19 | 17784.88 | 19572.15 | 90.87% | 13.99 | 13.99 | 100.00% | 123.45 | 202.89 | 60.85% |
| 20 | 17865.58 | 19145.64 | 93.31% | 13.99 | 13.99 | 100.00% | 123.45 | 202.89 | 60.85% |
| 21 | 17455.78 | 19299.65 | 90.45% | 13.99 | 13.99 | 100.00% | 123.45 | 202.89 | 60.85% |
| 22 | 17841.21 | 19368.80 | 92.11% | 13.99 | 13.99 | 100.00% | 123.45 | 202.89 | 60.85% |

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
| 1 | 258.38 | 258.38 | 2316.08 | 2141.49 | 108.15% | 5.47 | 6.13 | 89.24% | 131.25 | 628.16 | 20.89% |
| 2 | 273.71 | 273.71 | 2736.67 | 2464.94 | 111.02% | 5.63 | 6.28 | 89.67% | 127.40 | 628.16 | 20.28% |
| 3 | 273.66 | 273.66 | 1938.83 | 1718.75 | 112.80% | 6.20 | 6.76 | 91.79% | 127.38 | 628.16 | 20.28% |
| 4 | 273.66 | 273.66 | 1943.39 | 1586.69 | 122.48% | 7.61 | 7.95 | 95.68% | 127.38 | 628.16 | 20.28% |
| 5 | 273.62 | 273.62 | 2338.06 | 2113.16 | 110.64% | 7.87 | 8.51 | 92.52% | 127.35 | 628.16 | 20.27% |
| 6 | 259.00 | 259.00 | 2347.37 | 1795.94 | 130.70% | 7.61 | 8.49 | 89.60% | 122.84 | 628.16 | 19.56% |
| 7 | 259.00 | 259.00 | 2195.34 | 1619.27 | 135.58% | 10.25 | 11.03 | 92.92% | 122.84 | 628.16 | 19.56% |
| 8 | 259.06 | 259.06 | 1352.46 | 1200.27 | 112.68% | 10.24 | 10.98 | 93.28% | 122.84 | 628.16 | 19.56% |
| 9 | 259.06 | 259.06 | 1160.32 | 975.22 | 118.98% | 15.20 | 16.00 | 95.00% | 122.84 | 628.16 | 19.56% |
| 10 | 259.06 | 259.06 | 1202.16 | 921.25 | 130.49% | 15.25 | 15.99 | 95.36% | 122.85 | 628.16 | 19.56% |
| 11 | 259.06 | 259.06 | 1170.65 | 893.10 | 131.08% | 15.20 | 16.03 | 94.83% | 122.85 | 628.16 | 19.56% |
| 12 | 259.06 | 259.06 | 1096.98 | 886.39 | 123.76% | 15.21 | 16.00 | 95.09% | 122.85 | 628.16 | 19.56% |
| 13 | 259.06 | 259.06 | 522.40 | 478.95 | 109.07% | 21.19 | 21.97 | 96.44% | 126.88 | 628.16 | 20.20% |
| 14 | 259.06 | 259.06 | 456.91 | 291.52 | 156.73% | 21.19 | 21.99 | 96.36% | 122.88 | 628.16 | 19.56% |
| 15 | 259.06 | 259.06 | 492.71 | 378.95 | 130.02% | 21.20 | 22.03 | 96.22% | 122.88 | 628.16 | 19.56% |
| 16 | 257.48 | 257.48 | 108.90 | 90.28 | 120.62% | 21.42 | 21.88 | 97.93% | 144.95 | 628.16 | 23.07% |
| 17 | 257.48 | 257.48 | 105.14 | 94.01 | 111.84% | 21.82 | 21.89 | 99.70% | 144.95 | 628.16 | 23.07% |
| 18 | 260.60 | 260.60 | 75.63 | 59.08 | 128.03% | 22.27 | 22.38 | 99.55% | 145.16 | 628.16 | 23.11% |
| 19 | 260.60 | 260.60 | 69.29 | 55.06 | 125.83% | 22.29 | 22.44 | 99.36% | 145.16 | 628.16 | 23.11% |
| 20 | 260.60 | 260.60 | 74.63 | 55.05 | 135.56% | 22.29 | 22.37 | 99.65% | 145.16 | 628.16 | 23.11% |
| 21 | 260.60 | 260.60 | 74.75 | 56.63 | 132.00% | 22.33 | 22.47 | 99.36% | 145.16 | 628.16 | 23.11% |
| 22 | 260.60 | 260.60 | 67.94 | 59.27 | 114.63% | 22.29 | 22.34 | 99.79% | 145.16 | 628.16 | 23.11% |

### Decompression

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4702.82 | 4140.02 | 113.59% | 5.86 | 5.99 | 97.78% | 123.45 | 202.89 | 60.85% |
| 2 | 9004.85 | 6879.00 | 130.90% | 5.84 | 5.96 | 98.10% | 123.45 | 202.89 | 60.85% |
| 3 | 9492.84 | 7061.29 | 134.43% | 5.86 | 5.99 | 97.78% | 123.45 | 202.89 | 60.85% |
| 4 | 9583.99 | 7118.21 | 134.64% | 5.86 | 5.95 | 98.36% | 123.45 | 202.89 | 60.85% |
| 5 | 8792.38 | 6996.38 | 125.67% | 5.85 | 5.98 | 97.84% | 123.45 | 202.89 | 60.85% |
| 6 | 25212.39 | 15631.36 | 161.29% | 5.86 | 5.95 | 98.36% | 123.45 | 202.89 | 60.85% |
| 7 | 26358.37 | 15792.69 | 166.90% | 5.86 | 5.83 | 100.47% | 123.45 | 202.89 | 60.85% |
| 8 | 25277.32 | 15868.03 | 159.30% | 5.85 | 5.98 | 97.72% | 123.45 | 202.89 | 60.85% |
| 9 | 25847.78 | 15826.70 | 163.32% | 5.85 | 5.86 | 99.73% | 123.45 | 202.89 | 60.85% |
| 10 | 26623.96 | 15366.93 | 173.25% | 5.86 | 5.86 | 99.93% | 123.45 | 202.89 | 60.85% |
| 11 | 26999.92 | 15864.32 | 170.19% | 5.85 | 5.99 | 97.72% | 123.45 | 202.89 | 60.85% |
| 12 | 27023.51 | 15851.21 | 170.48% | 5.86 | 5.83 | 100.47% | 123.45 | 202.89 | 60.85% |
| 13 | 24976.13 | 16078.15 | 155.34% | 5.82 | 5.98 | 97.26% | 123.45 | 202.89 | 60.85% |
| 14 | 27240.67 | 16195.68 | 168.20% | 5.86 | 5.86 | 99.87% | 123.45 | 202.89 | 60.85% |
| 15 | 24895.16 | 16073.73 | 154.88% | 5.85 | 5.99 | 97.72% | 123.45 | 202.89 | 60.85% |
| 16 | 18984.23 | 15474.24 | 122.68% | 5.86 | 5.94 | 98.62% | 123.45 | 202.89 | 60.85% |
| 17 | 19892.29 | 16166.79 | 123.04% | 5.85 | 5.95 | 98.29% | 123.45 | 202.89 | 60.85% |
| 18 | 16536.40 | 12521.11 | 132.07% | 5.82 | 5.99 | 97.20% | 123.45 | 202.89 | 60.85% |
| 19 | 16895.83 | 12320.53 | 137.14% | 5.82 | 5.99 | 97.20% | 123.45 | 202.89 | 60.85% |
| 20 | 16782.56 | 12955.00 | 129.55% | 5.82 | 5.95 | 97.77% | 123.45 | 202.89 | 60.85% |
| 21 | 16714.34 | 12866.98 | 129.90% | 5.85 | 5.98 | 97.84% | 123.45 | 202.89 | 60.85% |
| 22 | 16890.24 | 12733.72 | 132.64% | 5.86 | 5.98 | 97.85% | 123.45 | 202.89 | 60.85% |

### Trial variability

| Level | Operation | C++ min–max (MiB/s) | Reference min–max (MiB/s) | C++ paired wins / trials |
|---:|---|---:|---:|---:|
| 1 | Compression | 2306.96–2324.63 | 2006.57–2166.00 | 3 / 3 |
| 1 | Decompression | 4642.53–4731.59 | 3941.43–4152.42 | 3 / 3 |
| 2 | Compression | 2691.59–2753.34 | 2399.44–2506.62 | 3 / 3 |
| 2 | Decompression | 8722.28–9082.38 | 6863.74–7219.35 | 3 / 3 |
| 3 | Compression | 1832.09–1995.52 | 1591.27–1731.49 | 3 / 3 |
| 3 | Decompression | 9082.22–9528.92 | 6778.24–7109.04 | 3 / 3 |
| 4 | Compression | 1934.16–1954.87 | 1518.71–1611.53 | 3 / 3 |
| 4 | Decompression | 9567.07–9591.07 | 7113.74–7176.32 | 3 / 3 |
| 5 | Compression | 2204.04–2385.09 | 2077.31–2131.97 | 3 / 3 |
| 5 | Decompression | 8716.94–9014.67 | 6912.85–7242.41 | 3 / 3 |
| 6 | Compression | 2243.45–2524.54 | 1598.83–1865.56 | 3 / 3 |
| 6 | Decompression | 25127.81–25412.09 | 15084.70–15961.01 | 3 / 3 |
| 7 | Compression | 2117.29–2393.86 | 1424.05–1772.25 | 3 / 3 |
| 7 | Decompression | 25591.18–26788.39 | 15501.79–15890.88 | 3 / 3 |
| 8 | Compression | 1327.96–1437.36 | 1192.70–1266.42 | 3 / 3 |
| 8 | Decompression | 24492.59–26105.24 | 15499.45–15997.24 | 3 / 3 |
| 9 | Compression | 1102.43–1334.05 | 968.05–1025.05 | 3 / 3 |
| 9 | Decompression | 25700.96–26581.32 | 15474.60–16018.14 | 3 / 3 |
| 10 | Compression | 1082.06–1247.00 | 883.32–1039.55 | 3 / 3 |
| 10 | Decompression | 24999.38–26975.31 | 15343.91–15835.52 | 3 / 3 |
| 11 | Compression | 1065.38–1258.03 | 891.25–933.10 | 3 / 3 |
| 11 | Decompression | 25000.00–27809.94 | 15746.31–16098.15 | 3 / 3 |
| 12 | Compression | 1078.88–1156.16 | 509.00–969.58 | 3 / 3 |
| 12 | Decompression | 26769.74–27609.23 | 15417.37–15928.00 | 3 / 3 |
| 13 | Compression | 436.29–570.78 | 386.77–565.95 | 2 / 3 |
| 13 | Decompression | 24599.78–25926.93 | 16046.18–16198.92 | 3 / 3 |
| 14 | Compression | 359.78–465.89 | 288.20–500.02 | 2 / 3 |
| 14 | Decompression | 26223.94–27286.09 | 15646.62–16227.08 | 3 / 3 |
| 15 | Compression | 286.12–532.37 | 253.80–512.65 | 2 / 3 |
| 15 | Decompression | 21531.10–25165.37 | 15015.74–16490.06 | 3 / 3 |
| 16 | Compression | 106.16–118.24 | 85.43–115.90 | 2 / 3 |
| 16 | Decompression | 17992.54–19276.41 | 15196.43–15921.86 | 3 / 3 |
| 17 | Compression | 99.25–110.70 | 84.05–95.30 | 3 / 3 |
| 17 | Decompression | 19156.25–19932.41 | 15978.19–16168.29 | 3 / 3 |
| 18 | Compression | 70.99–75.80 | 58.58–61.15 | 3 / 3 |
| 18 | Decompression | 16275.45–16854.44 | 12473.78–12612.38 | 3 / 3 |
| 19 | Compression | 63.26–71.27 | 44.69–60.06 | 3 / 3 |
| 19 | Decompression | 16277.80–16977.18 | 10574.54–12646.03 | 3 / 3 |
| 20 | Compression | 72.12–75.33 | 48.38–57.51 | 3 / 3 |
| 20 | Decompression | 16105.29–17051.26 | 12643.75–12964.39 | 3 / 3 |
| 21 | Compression | 63.76–78.78 | 52.91–57.14 | 3 / 3 |
| 21 | Decompression | 16270.31–17346.29 | 12611.25–13125.62 | 3 / 3 |
| 22 | Compression | 67.21–76.53 | 55.02–60.91 | 3 / 3 |
| 22 | Decompression | 16589.96–17308.02 | 12481.33–12971.37 | 3 / 3 |

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
