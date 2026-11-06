# Benchmark results

## Random-input confirmation, 2026-11-06

Six CPU2-pinned alternating 1,000-frame optimized GCC 13 compression pairs at level 14 measured **143.51/133.31 MiB/s (C++/reference), or 107.65%** using median elapsed times. The median paired ratio was 105.95%, with C++ ahead in five of six pairs. Every compressed frame matched reference bytes and both decoders recovered both encodings. This replaces the earlier 88.86% short-screen result; it is a measurement correction, not a new implementation speedup. Absolute rates varied substantially across trials.

Six alternating 100,000-frame decoder pairs measured random level 1 at **40,782.13/41,364.18 MiB/s (98.59%)** and level 2 at **41,359.54/42,768.83 MiB/s (96.70%)**. Exact output and ordinary-allocation guards passed. The README and current matrix include these measurements with their measured process RSS and unchanged executable sizes; other rows retain their dated measurements.

SHA-256 checks show that random reference frames at levels 2–22 are identical, and that all 22 C++ decoder executables are identical to each other, as are all 22 reference decoder executables. Level 1 has a different frame header. Thus the random decoder timing differences among levels 2–22 cannot indicate distinct level-specific decoder algorithms. They are repeated measurements of the same executable/input pair under varying conditions. The next target is their shared raw-block path, not separate optimizations for individual random-input levels. Peak RSS is the whole process and varies with the measurement launcher; the longer Python-driven decoder runs report approximately 13 MiB for both workers.

Raw compression trials and equivalence hashes: `out/l14-random-confirm-20261106/`. Decoder trials and the matrix refresh script: `out/random-decode-confirm-20261106/`. The full matrix's original run date remains November 2; these three rows were confirmed November 6.

These measurements compare optimized, isolated sph-zstd++ (`C++`) and bundled reference-zstd (`Reference`) workers at the same level and on the same input. Throughput is MiB/s (higher is better); memory is peak resident set size and code is the complete worker executable (lower is better). Percentages are `C++ / Reference × 100`. Compression outputs are byte-compared and both decoders check both encodings in paired runs.

The [22-level mixed-input screening evaluation](#mixed-input-screening-evaluation) was run on 2026-11-02 with GCC 13 Release. Its level-3 row predates the double-RLE writer below; use the confirmed table and [full current matrix](BENCHMARKS_INPUTS.md) for the current level-3 result. The full matrix covers mixed, zero-filled, repeated, structured, and seeded-random data. Other focused tables further below are dated snapshots from prior changes; their compilers and trial counts differ. The [experiment history](PERFORMANCE_HISTORY.md) preserves prior reports and rejected trials.

## Large long-offset match copy

A CPU2-pinned `perf` profile of 100,000 level-16 structured frames attributed 47.14% of C++ cycles to libc `memmove` and 48.37% to the sequence decoder; reference spent 87.75% in its inlined sequence decoder and 6.33% in `ZSTD_safecopy`. The C++ frame contains eight large overlapping matches, mostly 131,072 bytes long with offsets of 28,672 or 114,688 bytes. The decoder now copies these matches in exact 32-byte chunks when the offset is at least 1,024 bytes. Each source chunk precedes its destination chunk, so a fixed-width `memcpy` is valid even though the entire match overlaps. Short offsets keep the previous doubling copy: an unrestricted prototype lost 18.31% on level-3 mixed input, whose large matches use a 71-byte offset.

Six CPU2-pinned alternating 30,000-frame old/new pairs measured structured decoder gains of +6.97% at level 16 (6/6 wins), +7.54% at 17 (6/6), +1.81% at 18 (4/6), +4.07% at 19 (5/6), +3.84% at 20 (6/6), +2.12% at 21 (5/6), and +3.13% at 22 (6/6). Level-3 mixed and structured controls were near neutral at −0.44% and −0.13%; level-16 mixed and random were +0.31% and +0.19%. Level-18 mixed was −2.50% in six short pairs, then +3.92% in six 100,000-frame pairs with wide timing variation, so its effect remains uncertain. Every worker checked exact decoded bytes and zero ordinary allocations after warmup.

Fresh six-trial direct C++/reference structured measurements put levels 16–22 at 98.63%, 101.28%, 97.20%, 99.92%, 100.37%, 100.63%, and 98.64% of reference. Longer random checks at levels 3, 4, 17, and 21 measured 98.09%, 105.59%, 99.60%, and 96.19%; these near-parity rows varied substantially between runs. The refreshed [full current matrix](BENCHMARKS_INPUTS.md) and README report all nine throughput, peak memory, and executable-size metrics for each level and input. GCC 13 optimized tests passed 12/12; MSVC unit and compression-parity tests passed. Profile, traces, old/new pairs, and current evaluation outputs are under `out/l16-decode-profile-20261102/` and `out/large-match-wildcopy-20261102/`.

## Alternating compressed FSE table descriptions

The decoder's constructor-allocated workspace now retains one primary and three secondary compressed sequence tables per table kind. A trace of reference level-16/17 structured frames found two literal-length, three offset, and four match-length descriptions per frame; the previous single-entry cache missed all nine descriptions on the next frame. Secondary lookups run outside the primary hit path, and the first changed description in each frame is parsed normally. Level-18 structured frames used one description per table and continue through the primary cache.

Six CPU-pinned, alternating 30,000-frame old/new pairs measured level-16 structured decompression +5.98% (6/6 wins), level-17 structured +4.92% (5/6), level-18 structured −0.82% (2/6), and level-3 mixed −2.85% (0/6). The level-3 cost is a retained tradeoff; that row remains faster than reference in the current matrix. Longer six-trial direct C++/reference structured comparisons now measure 97.43% at level 16, 95.71% at level 17, and 95.44–98.00% at levels 18–22. Six 100,000-frame random-input checks put levels 3, 4, 17, and 21 at 101.34%, 100.30%, 100.01%, and 100.33% of reference, respectively; the short matrix had understated several of those rows. All workers checked exact output and no ordinary allocations after warmup. GCC 13 optimized tests passed 12/12, and MSVC unit and compression-parity tests passed. The refreshed [full current matrix](BENCHMARKS_INPUTS.md) and README report all nine C++ and reference throughput, peak memory, and executable-size metrics for every level and input. Raw trials and trace: `out/fse-cache-hit-audit-20261102/`.

## Confirmed cross-frame FSE table cache

The decoder now remembers the exact compressed FSE table descriptions and their built tables across frame resets. A matching description reuses the table; a changed description rebuilds it. One workspace containing three fixed-size caches is allocated when the decoder is constructed, with no allocation during warmed-up frame decoding. The stateful test decodes reference-produced structured, structured, mixed, and structured frames through one decoder with resets between them. Optimized GCC 13 tests passed 12/12, and MSVC unit and compression-parity tests passed.

The first inline-cache layout improved level-18 structured 7.65% over six alternating 100,000-frame old/new pairs but lost 3.11% on random input. Moving the caches out of the decoder object retained a 5.87% structured gain over six longer pairs (6/6 wins); level-3 mixed gained 7.05% (6/6), varied text 2.08% (6/6), level-18 mixed 5.09% (5/6), and the longer level-18 random control was +3.14% (4/6) with wide variation. Every worker checked the exact decoded bytes and zero ordinary allocations after warmup. A separate six-pair, 30,000-frame control alternated distinct level-18 structured and mixed reference frames through one decoder and verified both outputs; it was near neutral at +0.82% median (4/6 wins). Thus the cache helps repeated frame descriptions without a measured large cost when these two descriptions alternate.

The current all-level five-input Release matrix was rerun with identical C++ and reference settings. Six longer direct C++/reference structured checks at levels 16–22 are in the [current matrix](BENCHMARKS_INPUTS.md); level 18 now measures 18,839.73/19,628.74 MiB/s (95.98%). Levels 16–17 remain at 90.67% and 92.97%, so the all-level speed goal remains open. Six 100,000-frame random checks were refreshed at levels 3, 4, 12, and 20. The README and detailed matrix report all nine current metrics for every level and input; short rows remain screening measurements. Raw old/new pairs and the refreshed matrix: `out/l18-crossframe-fse-cache-20261102/` and `out/crossframe-fse-cache-matrix-20261102/`.

## Confirmed random-input decompression levels 12 and 20

The two-trial random-input screen reported level-12 and level-20 decoder throughput at 88.16% and 84.42% of reference. Six CPU2-pinned alternating 100,000-frame trials per level used the exact same 1-MiB reference frame for both optimized GCC 13 workers, and each worker verified every final decoded byte. At level 12, the median-of-elapsed-times result is 38,964.26/40,668.82 MiB/s (95.81%); the median paired ratio is 97.44%, with C++ ahead in one of six pairs. At level 20, the median-of-elapsed-times result is 40,632.47/40,406.50 MiB/s (100.56%); the median paired ratio is 97.14%, with C++ ahead in two of six pairs. The apparent level-20 lead from independent medians is not a demonstrated speedup: trial variation changes the ordering. The README and [current matrix](BENCHMARKS_INPUTS.md) use median elapsed times, consistent with their other rows; peak RSS comes from the same long trials, and executable sizes are unchanged. Raw CSVs and the exact-output driver are under `out/random-decode-confirm-20261102/`.

## Confirmed level-3 double-RLE sequence writer

The level-3 double-fast encoder now uses a match-length-only FSE state loop when both literal-length and offset tables are run-length. Both omitted states emit zero bits; their literal-length and offset extra bits are still written in the original order. A phase trace found this table combination in 98,098 of 99,099 structured sequence blocks and 31,031 of 35,035 mixed blocks over 1,000 frames. Six alternating old/new pairs of 10,000 frames gained 3.22% median on structured input (6/6 wins) and 4.45% on mixed (5/6). Zero, repeated, and random controls were near baseline. On a separate 1-MiB varied-text corpus, the literal-length table was never run-length and six 10,000-frame pairs were neutral (−0.40% median, 3/6 wins). Every candidate frame matched the reference bytes, optimized GCC acceptance passed 12/12, and focused MSVC compression parity passed. The level-4 worker's `.text` section is unchanged.

These independent six-trial candidate/reference comparisons checked compressed bytes and both decoders on both encodings. Structured, mixed, and random used 3,000 frames per trial; zero and repeated used 10,000. The level-3 structured lead is narrow and timing sensitive. Raw phase traces, old/new controls, and paired reports are under `out/l3-phase-modes-20261102/` and `out/l3-double-rle-writer-20261102/`.

| Input | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Zero | 19963.37 | 7753.26 | 257.48% | 6.00 | 6.59 | 91.02% | 127.38 | 628.16 | 20.28% |
| Repeated | 14747.55 | 8713.14 | 169.26% | 6.00 | 6.64 | 90.44% | 127.38 | 628.16 | 20.28% |
| Structured | 740.97 | 728.01 | 101.78% | 6.12 | 6.74 | 90.82% | 127.38 | 628.16 | 20.28% |
| Random | 3505.03 | 2846.51 | 123.13% | 6.98 | 6.61 | 105.59% | 127.38 | 628.16 | 20.28% |
| Mixed | 2105.57 | 1778.04 | 118.42% | 6.22 | 6.78 | 91.78% | 127.38 | 628.16 | 20.28% |

## Current optimal-parser specialization

The optimal parser now selects its accurate mode at compile time for production levels 16–22. Six CPU-pinned, alternating saved-binary comparisons measured median compression gains of 1.40% at level 16 structured, 1.04% at level 17 structured, 4.81% at level 16 varied, 5.14% at level 16 mixed, and 1.58% at level 18 structured. On equal-work level-16 structured input, `perf stat` measured 6.9% fewer cycles, 3.1% fewer instructions, and 18.8% fewer branch misses. Level-16/17 worker binaries became about 4.3 KiB smaller. Every timed candidate frame matched the reference bytes.

Independent six-trial, 1,000-frame candidate/reference checks measured 102.92% of reference compression speed at level 16 structured, 102.52% at level 17 structured, and 124.92% at level 16 mixed. Both decoders recovered both encodings. Optimized GCC acceptance passed 12/12, and the focused MSVC compression-parity test passed. The README and [full current matrix](BENCHMARKS_INPUTS.md) contain all absolute throughput, memory, and executable-size measurements. Raw old/new trials, counters, and paired reports are under `out/optimal-mode-template-20261102/`. Structured high-level decompression remains slower than reference and is the next target.

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

The following compression screen was collected on 2026-10-24 with GCC 13 Release `-O3`, one-MiB deterministic mixed input, six alternating trials per level, 300 frames per trial at levels 1–15 and 100 at levels 16–22. Every trial passed exact compressed-byte parity and reciprocal decoding. It predates later retained optimizations; use the [current matrix](BENCHMARKS_INPUTS.md) for this checkout.

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

## Mixed-input screening evaluation

Generated by `zstdpp_eval` using **GNU 13.3.0**, Release, 3 alternating trials of 200 timed iterations each, and a deterministic 1048576-byte mixed corpus. **Key: `C++` is sph-zstd++; `Reference` is reference zstd.** Code size is the complete worker executable; memory is peak resident set size, so both intentionally include runtime overhead. Each percentage is C++ / Reference * 100, computed before display rounding. Higher is better for throughput; lower is better for memory and code size.

Throughput is derived from median elapsed time; memory is the median of process peak RSS. For even trial counts, central values are averaged with integer rounding down. Each trial alternates which implementation runs first, for both operations. All measurements are preserved in `trials.csv` and per-trial worker CSVs; `metrics.csv` contains aggregates.

Compression workers receive identical input. Decompression workers both consume the same reference-zstd-generated frame at each level, so their throughput and memory results use identical compressed bytes.

### Compression

| Level | C++ size (KiB) | Reference size (KiB) | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 258.38 | 258.38 | 2448.98 | 2176.79 | 112.50% | 5.50 | 6.10 | 90.20% | 131.25 | 628.16 | 20.89% |
| 2 | 273.71 | 273.71 | 2965.37 | 2592.64 | 114.38% | 5.59 | 6.27 | 89.17% | 127.72 | 628.16 | 20.33% |
| 3 | 273.66 | 273.66 | 1892.50 | 1664.07 | 113.73% | 6.25 | 6.79 | 92.06% | 127.38 | 628.16 | 20.28% |
| 4 | 273.66 | 273.66 | 1923.27 | 1656.13 | 116.13% | 7.75 | 7.98 | 97.16% | 127.38 | 628.16 | 20.28% |
| 5 | 273.62 | 273.62 | 2615.56 | 2157.32 | 121.24% | 7.88 | 8.52 | 92.48% | 127.27 | 628.16 | 20.26% |
| 6 | 259.00 | 259.00 | 2360.75 | 1937.98 | 121.81% | 7.74 | 8.55 | 90.59% | 122.85 | 628.16 | 19.56% |
| 7 | 259.00 | 259.00 | 2348.14 | 1702.37 | 137.93% | 10.25 | 10.96 | 93.55% | 122.85 | 628.16 | 19.56% |
| 8 | 259.06 | 259.06 | 1303.30 | 1199.04 | 108.70% | 10.25 | 11.02 | 92.98% | 122.85 | 628.16 | 19.56% |
| 9 | 259.06 | 259.06 | 1305.06 | 978.13 | 133.42% | 15.25 | 15.96 | 95.52% | 122.85 | 628.16 | 19.56% |
| 10 | 259.06 | 259.06 | 1182.62 | 927.54 | 127.50% | 15.21 | 16.00 | 95.07% | 122.85 | 628.16 | 19.56% |
| 11 | 259.06 | 259.06 | 1081.02 | 889.58 | 121.52% | 15.24 | 16.03 | 95.10% | 122.85 | 628.16 | 19.56% |
| 12 | 259.06 | 259.06 | 1088.31 | 860.67 | 126.45% | 15.25 | 15.99 | 95.36% | 122.85 | 628.16 | 19.56% |
| 13 | 259.06 | 259.06 | 524.52 | 407.32 | 128.77% | 21.21 | 22.00 | 96.40% | 126.88 | 628.16 | 20.20% |
| 14 | 259.06 | 259.06 | 471.69 | 368.03 | 128.16% | 21.18 | 21.98 | 96.39% | 122.88 | 628.16 | 19.56% |
| 15 | 259.06 | 259.06 | 479.01 | 412.82 | 116.03% | 21.21 | 21.99 | 96.48% | 122.88 | 628.16 | 19.56% |
| 16 | 257.48 | 257.48 | 105.63 | 91.81 | 115.06% | 21.45 | 21.81 | 98.35% | 140.66 | 628.16 | 22.39% |
| 17 | 257.48 | 257.48 | 103.15 | 89.61 | 115.11% | 21.89 | 21.86 | 100.16% | 140.66 | 628.16 | 22.39% |
| 18 | 260.60 | 260.60 | 75.35 | 53.64 | 140.49% | 22.35 | 22.37 | 99.93% | 141.05 | 628.16 | 22.46% |
| 19 | 260.60 | 260.60 | 74.98 | 59.96 | 125.06% | 22.33 | 22.43 | 99.55% | 141.05 | 628.16 | 22.46% |
| 20 | 260.60 | 260.60 | 71.28 | 53.05 | 134.37% | 22.34 | 22.37 | 99.90% | 141.05 | 628.16 | 22.46% |
| 21 | 260.60 | 260.60 | 74.37 | 57.00 | 130.48% | 22.32 | 22.36 | 99.84% | 141.05 | 628.16 | 22.46% |
| 22 | 260.60 | 260.60 | 72.26 | 55.54 | 130.11% | 22.33 | 22.37 | 99.83% | 141.05 | 628.16 | 22.46% |

### Decompression

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4834.57 | 4142.17 | 116.72% | 5.86 | 5.86 | 99.87% | 123.45 | 202.89 | 60.85% |
| 2 | 9365.47 | 7318.34 | 127.97% | 5.84 | 5.95 | 98.16% | 123.45 | 202.89 | 60.85% |
| 3 | 9885.37 | 7151.26 | 138.23% | 5.78 | 5.95 | 97.18% | 123.45 | 202.89 | 60.85% |
| 4 | 9723.74 | 7325.47 | 132.74% | 5.88 | 5.99 | 98.24% | 123.45 | 202.89 | 60.85% |
| 5 | 8955.30 | 7315.05 | 122.42% | 5.86 | 5.98 | 97.85% | 123.45 | 202.89 | 60.85% |
| 6 | 27303.41 | 15877.08 | 171.97% | 5.86 | 5.86 | 99.87% | 123.45 | 202.89 | 60.85% |
| 7 | 26398.28 | 15938.08 | 165.63% | 5.86 | 5.86 | 99.87% | 123.45 | 202.89 | 60.85% |
| 8 | 27017.45 | 15891.87 | 170.01% | 5.86 | 5.83 | 100.47% | 123.45 | 202.89 | 60.85% |
| 9 | 26296.35 | 15908.97 | 165.29% | 5.84 | 5.95 | 98.16% | 123.45 | 202.89 | 60.85% |
| 10 | 25457.85 | 16017.86 | 158.93% | 5.85 | 5.99 | 97.65% | 123.45 | 202.89 | 60.85% |
| 11 | 27554.98 | 16166.71 | 170.44% | 5.82 | 5.86 | 99.33% | 123.45 | 202.89 | 60.85% |
| 12 | 26544.07 | 16161.75 | 164.24% | 5.86 | 5.86 | 99.87% | 123.45 | 202.89 | 60.85% |
| 13 | 27463.93 | 16190.70 | 169.63% | 5.79 | 5.99 | 96.74% | 123.45 | 202.89 | 60.85% |
| 14 | 28430.68 | 16170.38 | 175.82% | 5.86 | 5.94 | 98.62% | 123.45 | 202.89 | 60.85% |
| 15 | 27292.20 | 16375.21 | 166.67% | 5.85 | 5.86 | 99.80% | 123.45 | 202.89 | 60.85% |
| 16 | 19177.33 | 16231.23 | 118.15% | 5.86 | 5.83 | 100.47% | 123.45 | 202.89 | 60.85% |
| 17 | 19807.30 | 16162.56 | 122.55% | 5.86 | 5.86 | 99.93% | 123.45 | 202.89 | 60.85% |
| 18 | 17082.46 | 12883.89 | 132.59% | 5.86 | 5.93 | 98.75% | 123.45 | 202.89 | 60.85% |
| 19 | 17161.94 | 13046.87 | 131.54% | 5.86 | 5.95 | 98.36% | 123.45 | 202.89 | 60.85% |
| 20 | 16885.53 | 12901.75 | 130.88% | 5.80 | 5.95 | 97.38% | 123.45 | 202.89 | 60.85% |
| 21 | 16846.37 | 12905.92 | 130.53% | 5.85 | 5.99 | 97.65% | 123.45 | 202.89 | 60.85% |
| 22 | 17229.57 | 12820.58 | 134.39% | 5.85 | 5.97 | 97.97% | 123.45 | 202.89 | 60.85% |

### Trial variability

| Level | Operation | C++ min–max (MiB/s) | Reference min–max (MiB/s) | C++ paired wins / trials |
|---:|---|---:|---:|---:|
| 1 | Compression | 2436.13–2459.93 | 2131.80–2193.09 | 3 / 3 |
| 1 | Decompression | 4689.84–4874.83 | 3904.44–4180.33 | 3 / 3 |
| 2 | Compression | 2959.61–3024.87 | 2459.66–2602.64 | 3 / 3 |
| 2 | Decompression | 9288.91–9555.23 | 7286.57–7395.25 | 3 / 3 |
| 3 | Compression | 1700.19–1950.82 | 1518.29–1670.94 | 3 / 3 |
| 3 | Decompression | 9828.02–9945.90 | 7105.80–7472.91 | 3 / 3 |
| 4 | Compression | 1844.95–1937.69 | 1588.99–1758.85 | 3 / 3 |
| 4 | Decompression | 9714.14–9949.59 | 7293.16–7395.63 | 3 / 3 |
| 5 | Compression | 2587.55–2622.21 | 2096.76–2189.44 | 3 / 3 |
| 5 | Decompression | 8941.99–9214.37 | 7240.44–7325.34 | 3 / 3 |
| 6 | Compression | 2263.03–2522.95 | 1771.73–1945.93 | 3 / 3 |
| 6 | Decompression | 26382.22–27355.80 | 15681.00–16278.93 | 3 / 3 |
| 7 | Compression | 1791.50–2389.05 | 1560.59–1782.49 | 3 / 3 |
| 7 | Decompression | 25645.68–27145.69 | 15504.58–16083.07 | 3 / 3 |
| 8 | Compression | 1296.47–1653.56 | 1136.36–1267.98 | 3 / 3 |
| 8 | Decompression | 26472.10–27578.62 | 15721.61–16021.99 | 3 / 3 |
| 9 | Compression | 1094.31–1322.01 | 930.86–994.11 | 3 / 3 |
| 9 | Decompression | 25980.14–26803.32 | 15776.55–16006.31 | 3 / 3 |
| 10 | Compression | 1162.86–1288.12 | 913.84–947.35 | 3 / 3 |
| 10 | Decompression | 25443.42–26319.91 | 15725.66–16065.34 | 3 / 3 |
| 11 | Compression | 998.48–1202.68 | 827.77–905.35 | 3 / 3 |
| 11 | Decompression | 27507.92–27833.33 | 15153.91–16247.13 | 3 / 3 |
| 12 | Compression | 1054.58–1239.75 | 822.81–937.46 | 3 / 3 |
| 12 | Decompression | 26521.56–28710.69 | 15528.32–16315.30 | 3 / 3 |
| 13 | Compression | 445.56–533.67 | 330.68–412.39 | 3 / 3 |
| 13 | Decompression | 25727.67–27616.88 | 15982.38–16423.27 | 3 / 3 |
| 14 | Compression | 425.15–490.92 | 277.17–369.29 | 3 / 3 |
| 14 | Decompression | 27226.60–28443.82 | 13577.11–16196.81 | 3 / 3 |
| 15 | Compression | 415.65–495.73 | 400.17–418.29 | 3 / 3 |
| 15 | Decompression | 25058.38–27723.86 | 16164.39–16436.65 | 3 / 3 |
| 16 | Compression | 103.64–109.14 | 88.81–92.55 | 3 / 3 |
| 16 | Decompression | 18687.01–19674.64 | 16031.04–16464.97 | 3 / 3 |
| 17 | Compression | 98.20–110.23 | 88.30–93.02 | 3 / 3 |
| 17 | Decompression | 19676.47–20356.87 | 16032.91–16309.35 | 3 / 3 |
| 18 | Compression | 70.85–77.66 | 48.50–59.26 | 3 / 3 |
| 18 | Decompression | 17075.54–17131.70 | 12347.98–13105.88 | 3 / 3 |
| 19 | Compression | 74.13–76.53 | 50.40–60.07 | 3 / 3 |
| 19 | Decompression | 16251.77–17171.61 | 12879.64–13170.80 | 3 / 3 |
| 20 | Compression | 70.32–75.71 | 48.86–57.54 | 3 / 3 |
| 20 | Decompression | 16622.95–17186.75 | 12836.35–13028.74 | 3 / 3 |
| 21 | Compression | 71.10–78.36 | 51.42–59.20 | 3 / 3 |
| 21 | Decompression | 16641.35–17442.15 | 12640.33–12984.20 | 3 / 3 |
| 22 | Compression | 71.41–74.36 | 50.63–55.78 | 3 / 3 |
| 22 | Decompression | 16136.12–17339.02 | 12426.51–13134.85 | 3 / 3 |

<!-- SPH_ZSTDPP_EVAL_END -->







## How to reproduce

For Linux/WSL decoder diagnostics, `eval/compare_decoder_cycles.py` alternates two optimized workers on one CPU and records elapsed time alongside hardware cycle, instruction and branch counts. It saves executable/input SHA-256 hashes, raw counters, worker metrics and a summary, and rejects unavailable counters or event scheduling below 99%. Example from the repository root (use an empty output directory):

```sh
python3 eval/compare_decoder_cycles.py --perf /usr/local/bin/perf \
  --cpp out/build/gcc13-all-levels/eval/bin/sph_decompress_l18 \
  --reference out/build/gcc13-all-levels/eval/bin/reference_decompress_l18 \
  --encoded out/large-match-wildcopy-20261102/matrix/structured/reference_l18.zst \
  --expected out/large-match-wildcopy-20261102/matrix/structured/corpus.bin \
  --iterations 100000 --trials 6 --cpu 2 --output out/decoder-cycle-comparison
```

Use your own encoded and expected files if those local benchmark artifacts are absent. Counters cover the whole worker process, while worker elapsed time excludes setup and warmup. Each worker validates decoded output at warmup; a nonzero worker exit fails the comparison. Keep these diagnostic ratios separate from unprofiled throughput measurements. Parser validation tests run with `python3 -m unittest eval.test_compare_decoder_cycles`.

The full evaluator builds separate optimized workers for each implementation, operation, and configured level. On Windows:

```text
cmake --preset msvc-release-eval
cmake --build --preset msvc-release-eval
cmake --build --preset msvc-run-eval
```

The evaluator updates the marked generated section above and writes `metrics.csv`, `trials.csv`, worker CSVs, and `METRICS.md` to its results directory. It defaults to the configured levels 1, 3, 5, 7, 8, 9, and 15; set `SPH_ZSTDPP_EVAL_LEVELS` to the full semicolon-separated list `1;2;...;22` for a complete matrix. Its `--iterations`, `--trials`, `--output`, and `--report` options allow a fresh run without overwriting prior output. Benchmark both implementations with the same Release optimization and CPU target. Use alternating, unprofiled runs for throughput; profiler samples are for locating hotspots, not measuring speedups.

Pass `--input FILE` to run the same all-level evaluator on a different corpus. The file must have exactly `SPH_ZSTDPP_EVAL_CORPUS_SIZE` bytes; by default the evaluator generates its deterministic mixed input. For the [additional input matrix](BENCHMARKS_INPUTS.md), generate four files with `python3 eval/make_matrix_inputs.py out/matrix-inputs`, then run each through the all-level evaluator with a distinct empty output directory. `eval/render_current_matrix.py` renders their aggregate CSVs together with the mixed-input CSV into the README overview and detailed input report.

For a single level and input, build `msvc-pair`, `gcc-pair`, or `gcc-native-pair` and run `zstdpp_pair --level N --input FILE --iterations 1000 --trials 6`. The input must match the configured `SPH_ZSTDPP_EVAL_CORPUS_SIZE` (1 MiB by default). The pair tool saves exact frames and checks byte-level compression equality and both decoder directions on every trial.
