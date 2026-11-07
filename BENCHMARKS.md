# Benchmark results

## Scalar FSE scheduling and index trials, 2026-11-06

Traced the level-4 encoder hotspot at `16edc`: the stack store saves the next-state table load immediately before it. This confirms a state-transition dependency, but the sampled percentage does not prove that the store itself is expensive. Two isolated changes were tested against the same saved optimized GCC 13 worker on structured input, using six alternating CPU2 pairs of 3,000 frames each:

| Candidate | Median paired throughput change | Pair wins |
| --- | ---: | ---: |
| Load and assign next FSE state before appending emitted bits | -2.57% | 2/6 |
| Retain original ordering, use unsigned 32-bit next-state index | -3.25% | 1/6 |

All 24 timed workers matched the reference frame exactly and passed their worker guards. Neither candidate merits broader correctness/performance gates; both were rejected. The narrower arithmetic is already retained in a different grouped-writer context, but that result does not transfer to this scalar loop. Candidate headers, workers and raw results are archived in `out/fse-early-transition-20261106/` and `out/fse-scalar-index-20261106/`. Restored production source and rebuilt level-4 SHA-256 both match the saved baseline; executable SHA-256 is `dabfe44df6db35b396bb2dafdc027fc8bca523d04589efaba05069274ea0cc18`. README tables are unchanged because no improvement was retained.

Next architectural candidate: double-fast records currently retain logical offset plus repeat code, and the entropy encoder reconstructs offBase in both its gathering and reverse-writing passes. The retained GCC level-5 greedy specialization already stores offBase directly. Audit and adapt that representation for double-fast with an explicit parser/writer policy, covering ordinary matches, immediate repeat matches and the RLE shortcut. Keep ordinary parser callers unchanged. Current partition derivation and repeat repair are selected only for optimal strategies, but all remaining parsed-record consumers still need review before implementation. Require record-level equivalence, reference-frame parity, mixed/structured/random/repeated/zero controls, and unchanged neighboring workers before retention.

## Current structured compression confirmation, 2026-11-06

After the Linux memory-report correction, six alternating CPU2 trials confirmed level 17 at 480.91/483.14 MiB/s (99.54% of reference), substantially closer than its 90.85% short screen. Level 4 remains slower: six 3,000-frame trials gave 663.62/696.27 MiB/s (95.31%). Both runs passed byte-for-byte compression and decoding both encodings with both decoders. These are diagnostic confirmations, not a retained library change. Reports: `out/current-structured17-20261106/` and `out/current-structured4-20261106/`.

Matched level-4 `perf record` captures used 10,000 structured frames per implementation, CPU2, user cycles, period 100,003, and zero lost samples. Reference self samples: double-fast parser 54.47%, sequence writer 23.71%, sequence-to-codes 7.26%, simple histogram 4.78%. C++: parser 49.47%, broad sequence encoder 30.87%, enclosing frame code 4.21%, match counting 3.27%, three sequence-table selectors 6.46% combined. Boundaries differ, so these shares are not directly comparable phase costs. Profiled reference/C++ times were 19.051/19.101 seconds and sampled cycle totals 56.858/56.799 billion: profiling does not reproduce the unprofiled throughput gap. Both generated frames have SHA-256 `be0c6b514e5f0f24abdb2a95fc0560f5f4a7b3f86caa7104411eb2fe441bc50a`. The script's final comparison initially hit a CRLF filename error; independent SHA-256 verification established equality and the script was normalized to LF without rerunning completed captures.

The C++ encoder annotation places 11.59% of its local samples at a stack store (`16edc`, `mov %eax,0x88(%rsp)`) in the reverse writer. Sampling skid prevents attributing that fraction to the store itself. NEXT: map its producer/consumer dependencies and compare the corresponding reference writer before testing a narrowly scoped reduction in live FSE/bitwriter state. Earlier zero-literal copy skips and general state rewrites have already failed controls; avoid repeating them without a new mechanism. Recordings, executable hashes, worker metrics and annotation: `out/current-structured4-profile-20261106/`. No production source changed, and no new speed improvement is claimed.

## Grouped decoder confirmation and Linux memory-report correction, 2026-11-06

Confirmed all 30 distinct GCC decoder workloads representing 110 level/input rows. Each workload was calibrated from 2,000-frame probes to about one second for the slower worker, with a minimum of 10,000 frames. Six rounds alternate implementation order and deterministically shuffle workload order. All 360 timed workers passed exact-output/allocation guards, and every member's executable, frame and expected-output hashes matched both before and after the run. Full configuration, per-worker metrics and group membership are preserved under `out/decoder-group-confirm-20261106/`.

Every group's median paired throughput exceeded reference in this run. The closest were random2–22 at 100.81% (3/6 wins), structured16–17 at 100.84% (6/6), structured18 at 101.11% (5/6), random1 at 101.17% (4/6), and structured19–22 at 102.45% (6/6). Other structured groups ranged from 104.93% to 109.83%. This does not establish a reliable raw-input lead, nor complete the cross-platform compression/decompression objective. In particular, random2–22 is one shared workload, not 21 independent wins.

The run also exposed a memory-reporting error: Linux `getrusage(RUSAGE_SELF).ru_maxrss` can include memory used before the worker's executable was loaded. A minimal child reported 11,536 KiB with a small Python launcher and 142,608 KiB after the launcher allocated 128 MiB, whereas the child's `/proc/self/status` `VmHWM` was 3,472 and 3,528 KiB. The grouped run's roughly 19.9 MiB figures are therefore unsuitable as worker-memory comparisons. Earlier Python-launched confirmations can have the same contamination; their apparent equal peaks do not prove equal library memory use.

Linux evaluation workers now read `VmHWM` for the current executable's memory image, rejecting missing, invalid, zero, overflowing or wrong-unit values. Windows and other platform backends are unchanged. Parser tests passed on GCC and MSVC. Actual C++ workers now report 6.703/6.738 MiB under 0/128 MiB launcher allocations; reference workers report 6.680/6.633 MiB. These are whole-worker resident peaks, not library-only allocation sizes. The reporting call remains outside timing. The correction changes evaluation instrumentation, not the compression library.

The rebuilt GCC workers passed all 14 tests (25.85 seconds). The full matrix completed 968 timed workers, followed by 108 longer decoder workers; exact-byte, interoperability and allocation checks passed. Fresh rendered decoder throughput is 96.53% of reference for structured16, 97.97% for structured19 and 98.19% for random2. Corresponding C++/reference peaks are 5.72/5.72, 5.74/5.69 and 6.71/6.69 MiB; decoder executables are 126.69/211.24 KiB. These results belong to the rebuilt workers, whose code layout and executable size changed with the reporter, and must not be conflated with the earlier grouped timing wins. Fresh tables use the complete five-input, 22-level matrix and fresh longer decoder confirmations, with no old compression-confirmation overlays. Earlier compression confirmations remain historical evidence, but their potentially contaminated memory measurements are not mixed into the current tables. The refreshed compression rows are short screening trials, so near-parity speed differences require longer confirmation. README usage content and the native Windows section are preserved. This is a reporting correction, not a compression-library speed improvement. Artifacts and executable/frame hashes are under `out/worker-memory-report-20261106/`.

## GCC raw-copy width and helper-sharing controls, 2026-11-06

Compared bounded 32-, 64- and 128-byte forward loops in the frame-shaped copy diagnostic: eight 128 KiB copies with three-byte input gaps, 1 MiB output, six rotating triples per output offset. Median paired changes for 64/128 versus 32 bytes were +0.55%/+5.95% at offset0, +0.93%/+0.29% at16, +4.03%/+1.31% at64, and +9.25%/+8.49% at2048. Several cases split three wins out of six; these results justify full-decoder trials, not retention by themselves.

Six alternating CPU2 old/new decoder pairs produced the following results. Raw2 uses 100,000 frames per worker; other controls use 30,000. Percentages compare candidates against retained C++, not reference zstd.

| Candidate | Random2 | Mixed3 | Structured19 |
| --- | ---: | ---: | ---: |
| 128-byte class helper | +5.11% (3/6 wins) | -2.30% (0/6) | -2.44% (0/6) |
| 64-byte class helper | +2.29% (3/6) | +1.80% (6/6) | -0.38% (2/6) |

The 64-byte candidate warranted longer raw controls: six 300,000-frame counter pairs showed 95.83% median cycle cost and 108.39% throughput, with five cycle wins and six throughput wins. Instructions/frame fell from 296,047.86 to 214,127.59. However, further structured16/18 controls lost 3.46% (0/6) and 2.05% (1/6); repeated3 and zero3 were near neutral at +0.35%/+0.71%. Same-address full-decoder diagnostics confirmed that the 64-byte version still avoids the severe output-offset64 slowdown. None of this removes its structured-input tradeoff.

A structural alternative moved the 64-byte helper outside the decoder class template into a shared inline/noinline detail function, eliminating per-instantiation helper duplication. Six-pair screens gained mixed3 +3.96% (6/6) and random2 +15.61% (5/6, variable), but structured16 still lost 3.91% (0/6). Reject all three variants; their raw gains do not justify worsening the near-parity structured path. Complete executable size stayed 128,400 bytes for both class-helper widths. Generated-code/placement effects remain a possible explanation for movement on compressed inputs, not a proven cause.

All timed workers passed exact-output warmup and allocation guards. Comprehensive correctness tests were not run for the rejected candidates; the provisional tail-test expansion was reverted along with the code. Restored production executable exactly matches SHA-256 `12da3a33d974ed0978f510f52705f9af4b32724cdd7898fb172a2c3a2e2ae9b5`. README metrics unchanged. Sources, binaries, trials, placement controls and counters are in `out/gcc-raw-width-20261106/`. Next run consistent-duration confirmations across the 30 distinct workload groups, identifying repeatable reference deficits before further changes. Keep these raw-copy candidates available if a later decoder change removes the structured tradeoff.

## Decoder workload identity and structured15 confirmation, 2026-11-06

The short matrix reported structured15 decompression at 95.67% of reference. Six alternating CPU2-pinned 100,000-frame `perf stat` pairs on the retained decoder instead measured median paired throughput **106.89%** and cycle cost **93.65%**, with C++ faster in all six pairs. Median instructions/frame were C++/reference 587,641.09/565,285.55 and branch misses/frame 116.66/29.77. More instructions and branch misses did not imply slower decoding. The short-screen deficit is not confirmed; no code change is justified by it. These counter-instrumented results do not replace the uninstrumented README measurements.

Added `eval/group_decoder_cases.py` to audit content identity without merging measurements. It hashes both decoder executables, the compressed frame, and the expected output for every level/input row, records exact paths and hashes, and fails for missing artifacts or duplicate case identities. Four tests verify grouping identical content at different paths, separating cases when any artifact changes (even at the same size), and rejecting missing or duplicate cases.

The current 110 GCC decoder cases form **30 distinct artifact groups**. Structured11–15 form one group, so the structured15 confirmation applies to the same decoder code and frame used by those five rows; it is not five independent confirmations. Other groups include structured16–17, structured19–22, and random2–22. All row identities remain available in the manifest. Content identity does not eliminate process placement, timing variation or other run-condition differences, and the tool makes no automatic speed claims.

Reproduce the audit after building and generating the matrix:

```text
python3 -m unittest eval.test_group_decoder_cases
python3 eval/group_decoder_cases.py --matrix out/gcc-raw-copy-20261106/matrix --workers out/build/gcc13-all-levels/eval/bin --output out/decoder-groups.json
```

Use `--suffix .exe` for a complete set of Windows worker executables. Results and counters from this investigation are under `out/gcc-structured15-20261106/`. Library source and README metrics are unchanged. Next use the 30 groups to prioritize longer confirmations under consistent launch conditions, and investigate the remaining near-parity raw-copy workload rather than independent random-level algorithms. A raw-copy chunk-width experiment is distinct from the earlier rejected overlapping-match chunk changes: the raw source and destination are disjoint, and the recent frame-shaped placement benchmark supplies a reproducible target.

## GCC raw-block copy investigation, 2026-11-06

Six 300,000-frame CPU2-pinned production-worker counter pairs on the identical random2 frame measured retained C++ at 100.75% median paired reference throughput (3/6 wins), with cycle cost 98.42%. Instructions/frame were 1,321.89/1,305.32 for C++/reference. This did not reproduce the short matrix's large raw-input deficits and does not establish a universal lead.

A separate diagnostic executable runs both decoders against the same source and destination addresses, alternating six 100,000-frame pairs. Source page offset is 16; output offsets are controlled. The 64-byte output offset reproducibly slowed **both** decoders to about 5,000 MiB/s, versus roughly 37,000–42,000 MiB/s at offsets 0, 16 and 2048. Median paired C++/reference ratios at those four offsets were 98.37%, 99.17%, 100.09% and 99.82%. Independent throughput medians can give a different ratio because paired timing varies; the paired values are the appropriate comparison here. This diagnostic changes allocation and executable layout and is not a replacement for production-worker reporting.

A copy-only test with one 128 KiB region reproduced only a smaller alignment effect. Repeating eight 128 KiB copies across a 1 MiB working set, with three-byte source gaps matching raw-block headers, reproduced the large slowdown. At output offset 64, libc/forward32/reverse32 medians were 5,779.07/42,113.41/50,126.98 MiB/s. At offset 16 they were 36,763.85/42,041.86/34,784.75 MiB/s. These measurements establish a placement-sensitive copy bottleneck, not its exact microarchitectural cause.

The retained change uses an outlined 32-byte forward-copy loop for GCC x64 direct-output raw blocks of at least 4096 bytes. Capacity validation runs before copying, and the final 32-byte chunk ends exactly at the payload length, requiring no source or destination padding. Smaller blocks and streaming output keep the existing copy. No allocations are added. Six alternating old/new production-worker pairs measure random2 +6.20% (5/6, 100,000 frames), structured16 +1.80% (6/6), structured19 +1.75% (6/6), and mixed3 -5.67% (0/6); structured/mixed use 30,000 frames. The mixed-input slowdown is an explicit tradeoff. In the same-address full-decoder diagnostic at output offset 64, the candidate takes about 0.25 seconds versus reference about 2 seconds per 10,000 frames. That alignment-specific gain is separate from the ordinary-allocation result.

Validation passed: all 14 GCC tests (27.18 seconds), MSVC unit tests (3.10 seconds), and a focused ASan/UBSan check of seven threshold/tail/maximum block sizes at 64 output alignments with exact and undersized destinations. Permanent unit tests construct reference-decodable raw frames covering the 4096-byte threshold, all 32-byte tail remainders, maximum raw blocks, output guards and reset after errors. Final GCC worker SHA-256 is `12da3a33d974ed0978f510f52705f9af4b32724cdd7898fb172a2c3a2e2ae9b5`, unchanged by the explanatory comment; executable size is 128,400 bytes versus baseline 128,320. The full refresh completed 968 timed worker trials across 22 levels and five inputs, followed by 108 longer decoder confirmations. All parity, reciprocal-decoding and worker allocation checks passed. README and BENCHMARKS_INPUTS.md now contain the updated GCC tables; the README front page and native Windows section are unchanged. Artifacts and reproduction scripts are in `out/gcc-raw-copy-20261106/`.

The rendered tables use median elapsed time, which differs slightly from the confirmation script's median-throughput console summary. Structured19 now reports C++/reference 20,039.66/19,429.19 MiB/s (103.14%), memory 13.05/13.05 MiB (100.00%), and executable size 125.39/202.89 KiB (61.80%). Structured16 reports 23,392.31/23,078.61 MiB/s (101.36%) with the same displayed memory and code metrics. Mixed3 reports 9,715.57/7,282.26 MiB/s (133.41%), memory 5.86/5.94 MiB (98.62%), and the same code sizes. Random2 remains near parity at 41,716.33/41,827.36 MiB/s (99.73%), memory 13.09/13.09 MiB (100.00%), and the same code sizes. All structured16–22 confirmation rows exceed reference in this run, but several margins are narrow. None of these before/after report snapshots supersedes the paired old/new controls above.

All 22 GCC C++ decoder executables have the same SHA-256, and random levels 2–22 produce byte-identical reference frames, verified in `provenance.json`. Their widely varying short-screen rows therefore do not establish distinct level-specific decoder deficits. The longer random2 result still does not prove an all-level speed lead. Follow up on benchmark grouping for identical worker/frame combinations and confirm the separate structured15 short-screen deficit before selecting another code target. The overall objective remains incomplete.

## GCC sequence countdown and zero-literal combination, 2026-11-06

Tested a single countdown exit before FSE state updates, replacing the index/final-state/loop-termination arrangement only on GCC x64. Six alternating CPU2-pinned 30,000-frame old/new pairs measured structured19 -1.83% (1/6 wins), mixed3 +3.75% (4/6, variable). Three longer 100,000-frame counter pairs measured 100.32% cycle cost and 99.54% throughput. Instructions/frame fell from 1,121,906.45 to 1,107,461.31; branch misses/frame fell from 27.21 to 18.96. Removing instructions did not establish a target speedup. Counter labels compare candidate with retained C++, not reference zstd.

Scoped the countdown to non-medium blocks, preserving the original medium iteration direction and checks. Structured19 improved 2.11% initially (5/6), then 1.66% in six 100,000-frame pairs (5/6). However, mixed3 lost 5.98% in six 100,000-frame pairs (0/6), and structured16 lost 2.00% (1/6, 30,000 frames). Structured18 gained 1.27% (6/6). These controls expose compiler interactions even in the medium path whose loop semantics are unchanged.

Combined the scoped countdown with the saved outer zero-literal specialization to test whether the complementary gains survive together. Six 30,000-frame pairs preserved structured16 +11.06% (6/6), but structured19's initially neutral -0.31% became -1.34% in six 100,000-frame pairs (1/6). Other 30,000-frame controls: structured18 -1.26% (1/6), structured6 -0.33% (1/6), mixed3 -0.76% (1/6), repeated3 +1.85% (5/6), zero3 +2.94% (3/6, variable), random2 +0.49% (5/6). The separate gains are not additive; this version still worsens near-parity structured cases.

All three variants are rejected. Timed workers passed exact-output warmup and allocation guards; comprehensive malformed-input tests were not run for these rejected candidates. Restored source and rebuilt production exactly to baseline SHA-256 `83dfa13880328d2d64de9a1b57ea0be3a2f4bcf178fd64198874fc7738db4ba9`. README unchanged. Artifacts are in `out/gcc-sequence-countdown-20261106/`. Next investigate the GCC raw-input copy path with long paired counters against reference and controlled buffer placement, treating identical level-2–22 frames as one case. Native buffer-placement findings are useful context, not proof of GCC behavior.

## Reference structured19 profile and short-match copy trial, 2026-11-06

Profiled reference and retained GCC C++ sequentially on CPU 2 with the same frame, 300,000 decodes and `cycles:u` period 100003. Reference worker elapsed time was 20.4027 seconds versus C++ 20.5497 seconds. Reference sampled shares were 89.11% in sequence decoding, 5.13% in safe copying and 4.36% in FSE table construction. C++ shares were 55.99% in padded medium decoding, 30.18% in parsing, 9.74% in the tiny-match helper and 3.06% in libc copying. Different inlining boundaries prevent interpreting the symbol-share difference as excess parsing work. Profiles, annotations and worker snapshots are in `out/gcc-reference19-profile-20261106/`; recording used Linux `/tmp` and copied results back.

Reference `ZSTD_overlapCopy8` builds eight valid bytes for short offsets using four sequential byte assignments and an offset-adjusted four-byte copy. Tested that operation on GCC 13 x64 medium blocks only, for matches at most eight bytes with offsets below sixteen and at least sixteen spare output bytes. Offset one retains the existing fill; exact-output bounds remain checked before the path. This directly tests a reference architectural difference without the zero-literal prototype.

Six alternating CPU2-pinned 30,000-frame pairs gained 0.86% on structured19 (4/6) but lost 2.38% on mixed3 (0/6). Moving the same operation to a noinline helper worsened results to -4.07% and -6.44%, respectively (0/6 each). Both rejected. All benchmark workers passed exact-output warmup and ordinary-allocation guards; comprehensive malformed-input validation was not run for these rejected prototypes. Sources, workers and trials are in `out/gcc-short-match8-20261106/`.

Restored the header and verified the rebuilt worker matches baseline SHA-256 `83dfa13880328d2d64de9a1b57ea0be3a2f4bcf178fd64198874fc7738db4ba9`. README metrics remain unchanged. Next compare the annotated reference/C++ sequence loops, separating entry loads, extra-bit extraction, state transitions and copying. Do not repeat the already rejected local-output cursor, 64-byte chunk, AVX2 copy or batching experiments based only on broad symbol shares.

## GCC medium-block profiles and tiny-copy controls, 2026-11-06

Three alternating 100,000-frame CPU2 `perf stat` pairs reproduced the outer-dispatch candidate's structured19 regression against retained C++: median cycle cost 102.56%, throughput 97.84%. Median instructions/frame fell from 1,121,906.45 to 1,118,200.41; branch misses/frame rose from 19.40 to 29.80. These aggregate counters do not isolate a cause. The diagnostic report's `reference` label denotes the saved C++ baseline.

Sequential 300,000-frame cycle profiles (`cycles:u`, period 100003) put baseline/candidate sampled shares at 55.84%/56.35% in the padded medium decoder, 30.13%/29.72% in frame parsing, and 9.44%/10.07% in the tiny-match helper. Worker elapsed times were 20.6343/20.8500 seconds: the candidate remained slower, but the profiles do not reproduce the full counter-run gap. Seven-byte copy instructions account for substantial samples inside the helper, motivating two bounded-copy experiments rather than proving a root cause. Recording directly to the Windows-mounted directory failed with `Bad address`; recording under a fresh Linux `/tmp` directory succeeded, with data copied back afterwards and no lost samples reported.

Both following candidates include the saved outer zero-literal dispatch. Six alternating 30,000-frame pairs compare against the retained C++ decoder:

| Tiny-copy variant | Structured19 | Mixed3 |
| --- | ---: | ---: |
| Seven-byte branch before switch | -0.79% (1/6 wins) | -0.50% (1/6) |
| Range-based exact copies | -0.26% (2/6) | +0.47% (4/6) |

The range version selects two bounded four-byte copies for lengths 4–7, a two-byte copy and final byte for lengths 2–3, and a single byte for length 1, preserving no-op behavior outside 1–7. Longer structured19 pairs (100,000 frames, six pairs) still lose 0.82% (1/6). Structured18 is neutral at -0.08% (3/6), while structured16 retains +11.33% (6/6). Further 30,000-frame controls measure structured6 -0.81% (1/6), repeated3 +0.35% (4/6), zero3 -1.07% (3/6), and random2 +1.38% (4/6; variable). Crucially, a direct six-pair 100,000-frame comparison against reference zstd measures structured19 at 99.22% median paired throughput, winning only one pair. These results do not establish an acceptable all-level improvement.

Neither helper variant retained. All workers passed exact-output warmup and allocation guards; comprehensive malformed-input tests were not run for rejected variants. Restored source and rebuilt production exactly match baseline SHA-256 `83dfa13880328d2d64de9a1b57ea0be3a2f4bcf178fd64198874fc7738db4ba9`. README metrics remain unchanged. Profiles, annotations, counters, binaries, scripts and trial CSVs are in `out/gcc-zero-outer-20261106/`. Next profile reference decoding of the same structured19 frame and compare sequence/copy work with retained C++, before further zero-literal compiler tuning.

## GCC outer zero-literal dispatch controls, 2026-11-06

Moved GCC's zero-literal selection to `decode_sequences`, before entering the general implementation. The specialized decoder still performs all header/table checks. Medium blocks bypass this dispatch. Six alternating CPU2-pinned 30,000-frame old/new pairs improved structured16 by 12.54% (6/6); mixed3 was nearly neutral at -0.24% (2/6). Six longer 100,000-frame mixed pairs confirmed -0.07% (3/6), resolving the preceding prototype's repeatable mixed regression.

Medium-block controls still lost: structured18 -1.05% (1/6) and structured19 -1.59% (0/6), six 30,000-frame pairs each. Disassembly demonstrates that moving the dispatch outside the general decoder does not preserve its generated code: the padded-medium hot function shrinks from 0x1483 to 0x137a bytes, and the unpadded-medium function from 0x13f6 to 0x1223. Both retain their medium-block source path. The candidate introduces an out-of-line `read_sequence_count` call absent from the baseline padded-medium function. These observations identify compiler changes, not their runtime contribution.

A second variant forced that parser inline on GCC x64. It worsened structured18 to -2.58% and structured19 to -3.28% (0/6 wins each, 30,000 frames). Reject both variants pending a solution for the medium-block tradeoff; the inlining experiment does not support blaming the parser call for the regression. The outer candidate executable is 129,656 bytes versus baseline 128,320.

All timed workers passed decoded-output warmup and ordinary-allocation guards. Comprehensive mutation tests were not run for these rejected candidates. Restored source and production executable exactly to SHA-256 `83dfa13880328d2d64de9a1b57ea0be3a2f4bcf178fd64198874fc7738db4ba9`; README metrics remain unchanged. Artifacts, binaries, assembly, symbol tables, source snapshots and trial CSVs are in `out/gcc-zero-outer-20261106/`. Next obtain medium-block instruction/cycle measurements and profiles against the saved baseline before further code-generation changes. The outer-dispatch candidate remains available for that diagnostic work.

## GCC zero-literal specialization screen, 2026-11-06

Longer checks of the retained GCC 13 Release decoder against reference used six alternating 100,000-frame pairs, pinned to CPU 2. Structured16 measured a median paired throughput ratio of 102.42% (4/6 wins), and structured19 measured 101.61% (4/6). These confirmations weaken the deficits in the shorter matrix, but do not prove every level/input is faster. Existing README measurements remain unchanged because no new implementation was retained.

An isolated prototype extended the MSVC zero-literal block dispatch to GCC x64 and supplied a compile-time zero literal entry in GCC's existing packed-entry loop. GCC retained its bit reader, grouped FSE state reads, and append implementation. This removes constant work without adopting the rejected arbitrary-RLE cache. Six 100,000-frame old/new pairs improved structured16 by 11.48%, with all six pairs faster.

However, six alternating 30,000-frame pairs per control found regressions. These percentages compare the prototype against the previous **C++ decoder**, not reference zstd:

| Input / frame level | Median paired throughput change | Faster pairs |
| --- | ---: | ---: |
| Structured 19 | -0.68% | 2/6 |
| Structured 18 | -1.18% | 0/6 |
| Structured 6 | +1.85% | 5/6 |
| Mixed 3 | -3.44% | 0/6 |
| Repeated 3 | +2.03% | 5/6 |
| Zero 3 | -0.92% | 2/6 |
| Random 2 | -2.22% | 1/6 |

Three further mixed3 pairs under `perf stat`, 100,000 frames each, reproduced the regression: candidate/old-C++ throughput 95.71% and cycle cost 104.04%. Median instructions/frame fell from 2,139,765.35 to 2,124,193.19 (about 0.73%), while branch misses/frame stayed nearly unchanged at 539.95/540.56. The counter report's `reference` label denotes the saved old C++ worker. These counters establish higher execution cost despite fewer instructions; they do not isolate its cause. Complete executable size increased from 128,320 to 129,160 bytes.

Rejected this version. Every timed worker passed exact-output warmup and allocation guards; comprehensive malformed-frame validation was not run for the rejected candidate. Restored the header and rebuilt production: SHA-256 exactly matches the saved baseline, `83dfa13880328d2d64de9a1b57ea0be3a2f4bcf178fd64198874fc7738db4ba9`. Scripts, source snapshots, binaries, trial CSVs, and counters are under `out/gcc-zero-literal-20261106/`. Next investigate how the dispatch changes the general loop's generated code before trying another clone; an outer dispatch that preserves the existing general function is a testable alternative, not an established fix.

## MSVC zero-literal sequence specialization, 2026-11-06

Tracing the structured level-16 frame found three blocks containing 2,972 sequences whose literal-length table is constant zero. The retained general decoder performed literal-copy and length/state work for those sequences. An MSVC-only specialization now recognizes explicit zero-symbol RLE tables or repeated zero-literal tables once per padded non-medium block, then uses a compile-time zero literal entry and omits literal copying and literal-state updates. All table loading, repeat eligibility, output/history bounds, and bitstream-end validation remain in place. Nonzero constant tables retain the general path. GCC's path is unchanged.

This differs from the rejected arbitrary-entry cache and clone: zero literals also make the repeat-offset zero/nonzero decision constant and eliminate speculative padded literal copies. The selected block repeats its inexpensive count/header parsing but loads and validates tables only once.

Four fresh executable copies per version and two rotating pairs per case produced 144 old/new C++ worker measurements on CPU 2. These percentages compare against the previous **C++ decoder**, not reference zstd. Each worker checks decoded output at warmup and rejects ordinary allocations during timing.

| Input / frame level | Median paired throughput change | Faster pairs |
| --- | ---: | ---: |
| Structured 16 | +11.47% | 8/8 |
| Structured 18 | +0.82% | 8/8 |
| Structured 6 | +1.05% | 8/8 |
| Structured 19 | +1.01% | 7/8 |
| Structured 2 | +0.10% | 6/8 |
| Mixed 3 | +0.61% | 5/8 |
| Repeated 3 | -0.79% | 3/8 |
| Zero 3 | +0.23% | 5/8 |
| Random 2 | +2.63% | 6/8 |

The four level-16 copy medians improve 8.75–12.72%. Keep the repeated-input tradeoff visible; raw-input copy medians are noisy (-0.47% to +8.57%), so that result does not establish a reliable gain. All cases use 30,000 decodes per worker except structured2 at 10,000 and random2 at 100,000. Inputs produce 1 MiB. Source snapshots, binaries, hashes, scripts and measurements are in `out/msvc-zero-literal-20261106/{multi-image,extended}` and its parent directory. Executable size increases from 102.50 to 106.00 KiB.

Final validation passed: MSVC unit tests, 48,142 malformed-frame cases, and compression/interoperability acceptance (292.52 seconds total), plus all 14 GCC tests (27.26 seconds). The GCC worker is byte-identical to its prior build, SHA-256 `83dfa13880328d2d64de9a1b57ea0be3a2f4bcf178fd64198874fc7738db4ba9`. The final native `.text` exactly matches the screened candidate: 74,225 bytes, SHA-256 `cbdc2cde840f1589aff7f5f4987f6e19414ed7fdda3c0c97f2893799b3376c92`. Logs and hash verification are preserved with the experiment artifacts.

The final 678-trial native refresh updates all 110 level/input rows and all nine metrics in README and [BENCHMARKS_NATIVE.md](BENCHMARKS_NATIVE.md), preserving the GCC tables. Structured16 measures 21,673.98/19,540.50 MiB/s (110.92% of reference), with C++/reference memory 6.91/6.82 MiB (101.32%) and executable size 106.00/113.50 KiB (93.39%). Structured17/18/19 measure 109.71%/100.85%/101.98%; structured6 reaches 102.82%. Mixed3 measures 8,800.05/5,380.25 MiB/s (163.56%), memory 7.19/7.09 MiB (101.32%), and the same executable sizes. The native timing caveat still applies. Worker hashes match the recorded provenance; full trials are in `out/msvc-zero-literal-20261106/current-matrix/`. Remaining GCC and native deficits keep the all-level objective incomplete.

## Scoped MSVC long-overlap helper, 2026-11-06

The general decoder now uses an outlined 32-byte pointer-copy loop for long overlapping matches. The medium-block specialization retains its original inline loop: outlining that specialization regressed the isolated first-block control by 4.23%. Chunk width, the exact final chunk, and the caller's offset/length checks are unchanged; this adds no allocations or output padding requirements. GCC keeps its original implementation.

The screening comparison uses four fresh executable copies per version and two rotating-order pairs per case, pinned to Windows CPU 2. These percentages compare the candidate with the previous **C++ implementation**, not reference zstd. Each worker verifies decoded bytes at warmup and checks that ordinary allocations do not increase during timing.

| Input / frame level | Median paired throughput change | Faster pairs |
| --- | ---: | ---: |
| Structured 6 | +2.86% | 8/8 |
| Structured 8 | +3.34% | 8/8 |
| Structured 11 | +1.85% | 6/8 |
| Mixed 3 | +5.95% | 8/8 |
| Structured 2 | +0.09% | 4/8 |
| Structured 16 | -0.73% | 1/8 |
| Structured 18 | -0.22% | 3/8 |
| Structured 19 | -0.22% | 4/8 |
| Structured 18, first block only | -0.67% | 2/8 |
| Random 2 | +0.84% | 4/8 |

The level-16 tradeoff remains a follow-up target. Level 19 also has variable per-copy changes (approximately -3.14% to +0.58%); its near-neutral paired median does not establish a universal gain. Raw-input per-copy changes span -5.90% to +10.73%, so its median is not evidence of a stable improvement. The existing native timing caveat still applies.

All 160 screening workers completed successfully. Trials use 30,000 decodes, except random2 at 100,000. The first-block frame produces 128 KiB; other inputs produce 1 MiB. Sources, binaries, hashes and measurements are in `out/msvc-scoped-overlap-helper-20261106/{multi-image,extended}` and the parent directory's reproduction scripts. The warning-clean final build differs from the screened build in one of 18,622 decoded instructions: an equivalent `lea` adds the same registers in reverse operand order at the same address. Both `.text` sections are 70,961 bytes, 112 fewer than the previous C++ build; complete executable size remains 102.50 KiB. The final `.text` hash is `5cf369a2e8487d18df438c54c90a03c0be7183f230f8d05a6e914acad3e4a838`.

Final-source validation passed: MSVC unit tests, 36,578 malformed-frame cases, and compression/interoperability acceptance (288.99 seconds total); all 14 GCC tests (26.56 seconds). The final GCC worker remains byte-identical to the previous implementation, SHA-256 `83dfa13880328d2d64de9a1b57ea0be3a2f4bcf178fd64198874fc7738db4ba9`. Test logs are preserved beside the trial artifacts.

The final 678-trial native refresh updates all 110 level/input rows and all nine metrics in README and [BENCHMARKS_NATIVE.md](BENCHMARKS_NATIVE.md); GCC tables are preserved. Mixed3 measures 8,936.74/5,523.74 MiB/s (161.79% of reference), with C++/reference memory 7.18/7.09 MiB (101.27%) and executable size 102.50/113.50 KiB (90.31%). Structured6 and structured11 measure 101.48% and 101.74% of reference; structured8/16/19 remain at 98.91%/98.77%/98.90%. Structured18 is near parity at 100.27%. These results retain a useful improvement without satisfying the all-level objective. Full trials and worker/frame provenance are in `out/msvc-scoped-overlap-helper-20261106/current-matrix/`.

## Native raw-buffer placement control, 2026-11-06

To investigate raw-copy regressions caused by changes outside the raw decoder path, a separate diagnostic worker records encoded-input and output addresses before timing. It uses the retained decoder header and MSVC `/O2 /Ob3`, with ordinary allocation or a caller-owned output span at a controlled offset from a 4 KiB boundary. Four byte-identical executable copies each ran two rotated trials per offset, pinned to CPU 2, decoding the same random level-2 frame 100,000 times per trial. All 40 workers passed warmup exact-output and timed ordinary-allocation checks.

| Output offset within page | Median MiB/s | Minimum MiB/s | Maximum MiB/s |
| --- | ---: | ---: | ---: |
| Ordinary allocation: 96 | 42,312.04 | 40,560.15 | 46,074.72 |
| 0 | 42,898.76 | 37,494.63 | 46,774.30 |
| 16 | 42,558.09 | 37,063.82 | 46,739.35 |
| 64 | 41,819.44 | 40,159.37 | 47,473.36 |
| 2,048 | 42,770.12 | 37,391.84 | 48,776.44 |

The encoded allocation starts at page offset 96 in every trial. Ordinary output allocations also consistently start at offset 96, so differing low-order buffer alignment cannot alone explain their spread. Controlled output offsets do not remove the variation. This does not rule out higher address bits, physical placement, code placement, frequency effects, or other execution effects, and does not identify a root cause. Encoded allocation alignment is not the alignment of every raw block payload: frame and block headers shift those payloads.

This diagnostic changes the worker's output storage and generated code; it is not a direct replication of the production worker or a C++/reference comparison. Its medians are not replacement README benchmarks. No production or harness alignment change is retained. Source, runner, hashes, addresses, CPU times, individual measurements and summary are in `out/native-buffer-placement-20261106/` (`worker.cpp`, `run.ps1`, `hashes.json`, `trials.csv`, `summary.csv`). Next, prioritize the reproducible structured-frame regressions and their generated sequence/copy code rather than tuning alignment against these noisy raw results.

## Native level-2 raw-literal investigation, 2026-11-06

The structured level-2 deficit persisted across four fresh copies of each implementation, three alternating CPU2 trials of 10,000 frames per copy. C++ lost all 12 pairs, with median paired throughput 9.01% below reference. Copy-median C++ rates were 1,758.26–1,769.59 MiB/s versus reference 1,933.57–1,948.44 MiB/s. `out/native-l2-confirm-20261106/multi-image/` contains hashes, image addresses, and results; `base` denotes reference and `cand` denotes current C++ in those files.

Optimized, symbolized uProf captures preserved the deficit: 50,000 frames took 28.4288 seconds in C++ and 25.7599 in reference. C++ sampled time was mainly `decode_sequences_impl<0,1>` (21.55 seconds), with 3.03 seconds in `<1,0>` and 0.98 seconds in the medium match-copy helper. Reference sampled 24.00 seconds in sequence decoding and 1.03 in FSE table construction. These are 10 ms timer profiles, not hardware-cycle totals. Profiles are in `out/uprof-native2-20261106/`.

Exact-output traces show the first block has 8,449 sequences; the remaining fourteen blocks each have 4,096 sequences, 4,097 raw literal bytes, and only 14 trailing encoded bytes after the literal section. Those raw literals are therefore unpadded for C++'s 16-byte copy requirement. The reference's `set_basic` literal handler copies literals into workspace when trailing input cannot support wildcopy; C++ previously retained an unpadded borrowed span. The frame contains 65,264 sequences with one literal and a 15-byte match. Trace artifacts: `out/native-l2-confirm-20261106/{blocks,sequences}.csv`.

An initial MSVC staging trial gained 20.87% median paired throughput at structured level 2, but lost 6.14% on mixed3 across four copy pairs (0/8 wins), so it was rejected. Extending the existing short-copy fast path to padded literals recovered only part of the mixed loss (-3.10%, 0/8); this variant was also rejected despite +21.79% at structured2. Artifacts: `out/msvc-raw-literal-padding-20261106/` and `out/msvc-raw-literal-padding-v2-20261106/`.

The scoped version stages unpadded raw literals only for the existing medium-block copy strategy, reusing the literal workspace reserved at construction. It preserves the smaller-block borrowed-literal path and does not introduce another block-size threshold. Four fresh copies per version, two rotating-order pairs per copy, produced the following old/new controls; all 96 workers passed exact-output and ordinary-allocation guards.

| Input / frame level | Frames per trial | Median paired throughput change | Winning pairs |
| --- | ---: | ---: | ---: |
| Structured / 2 | 10,000 | +20.85% | 8/8 |
| Structured / 1 | 3,000 | -0.07% | 4/8 |
| Structured / 16 | 30,000 | +0.38% | 7/8 |
| Structured / 18 | 30,000 | +0.03% | 4/8 |
| Mixed / 3 | 30,000 | +0.49% | 5/8 |
| Random / 2 | 100,000 | -0.84% | 4/8 |

Every level-2 copy pair gained between 20.15% and 21.23% by its per-copy median. The raw-random control was noisy (per-copy changes -4.83% to +3.59%); it does not establish a stable regression or improvement. Executable size remains 102.50 KiB. The finalized MSVC `.text` matches the screened candidate exactly: 71,073 bytes, SHA-256 `7a60c112ed0e7d14db495c20b9c7f4326d1b666ec019a05702eaa36e54ac6689`.

The unit suite now checks exact and undersized output buffers for this reference-generated level-2 frame, including decoder reuse after failure. MSVC unit, 36,578 malformed-frame cases, and compression parity passed (287.01 seconds total). All 14 final GCC tests passed. An initially shared dispatch refactor changed GCC's binary; isolating that refactor to MSVC restored GCC SHA-256 `83dfa13880328d2d64de9a1b57ea0be3a2f4bcf178fd64198874fc7738db4ba9`, preserving its published measurements. Scoped trial scripts, binaries, hashes, and results are under `out/msvc-raw-literal-padding-v3-20261106/`.

The refreshed [native matrix](BENCHMARKS_NATIVE.md) and README contain all nine metrics from 678 trials. Structured level 2 now measures **2,126.66/1,928.48 MiB/s, 110.28% of reference**, with peak memory **6.96/6.89 MiB, 101.08%**, and executable size **102.50/113.50 KiB, 90.31%**. Current structured16/18 ratios are 99.69%/99.71%; these near-parity results and the noisy raw-copy controls leave the overall goal open. The native identical-binary caveat still applies. Data and provenance: `out/msvc-raw-literal-padding-v3-20261106/current-matrix/`. GCC tables remain unchanged.

## MSVC boundary checks and paired padded states, 2026-11-06

The combined decoder change moves medium-block scheduled-reader truncation checks to refill boundaries (with a final exact-end check) and pairs adjacent literal/match state-bit reads in padded MSVC blocks. Checked reads remain available; malformed-tail shifts are masked, and refill records overflow before further input loads. The GCC production decoder path is unchanged.

To avoid attributing one slow executable instance to the algorithm, the comparison used four fresh byte-identical copies of the baseline and four of the candidate. Each copy pair ran three alternating 30,000-frame trials on CPU2, with copy order rotated each trial. All 72 trials passed exact-output and ordinary-allocation guards.

| Input / frame level | Median paired throughput change | Winning trial pairs | Range of changes between per-copy medians |
| --- | ---: | ---: | ---: |
| Mixed / 3 | +1.26% | 10/12 | -0.26% to +1.67% |
| Structured / 16 | +1.77% | 10/12 | +1.47% to +2.39% |
| Structured / 18 | +3.37% | 12/12 | +2.55% to +3.98% |

The optimized worker shrank from 103.00 to 102.50 KiB; its `.text` shrank from 71,489 to 71,025 bytes. The rebuilt production worker's `.text` hash matches the screened candidate. The mixed-input improvement is less consistent than the structured improvements; the earlier apparent 19% gain from one slow baseline instance is not evidence for this change.

Validation: MSVC unit, differential bit-reader, and 36,578-case malformed-frame checks passed; MSVC compression parity passed in 247.47 seconds. All 14 GCC tests passed, and the expanded reader test passed ASan/UBSan. The rebuilt GCC decoder retains SHA-256 `83dfa13880328d2d64de9a1b57ea0be3a2f4bcf178fd64198874fc7738db4ba9`. Trial data, executable hashes, image addresses, and scripts are under `out/msvc-combined-reader-20261106/multi-image/` and its parent directory. Native measurements remain subject to the identical-binary caveat below.

The [current native matrix](BENCHMARKS_NATIVE.md) and README were refreshed from 678 trials covering all 22 levels and five inputs. Structured16 measured 18,370.68/18,164.77 MiB/s (101.13% of reference); structured18 15,341.46/15,176.03 (101.09%); mixed3 8,352.05/5,492.12 (152.07%). These are current/reference measurements, separate from the old/new copy controls above. Full speed, peak-memory, and executable-size metrics are published for every row. Raw data and provenance: `out/msvc-combined-reader-20261106/current-matrix/`. GCC tables are unchanged.

## Native identical-binary control, 2026-11-06

A four-way CPU2-pinned comparison of the retained baseline, two individual experiments, and their combination initially suggested improvements in both structured controls. The mixed-input baseline was unexpectedly slow. A second rotating-order control compared two copies each of baseline and combined binaries, using equal-length executable names and 30,001 iterations per run. The baseline copies have identical SHA-256 `6126ccf646bb8b31032e25471801f722943fb4a1a41c9fb2f601bbf41ed0d082`, yet `baseline.exe` measured 6,123.94–7,481.25 MiB/s while `basecopy.exe` measured 8,459.36–8,592.23 MiB/s. The median paired difference was **21.86%**, with the duplicate faster in all four trials. The combined executable's two copies were much closer to one another.

This does not establish the cause: executable mapping, process scheduling, or another host effect still needs investigation. It does establish that this apparent mixed-input gain cannot be attributed solely to changed C++ code. The combined change was not retained. Existing native tables preserve their measured numbers and provenance, but speed comparisons are provisional until this control discrepancy is resolved. GCC results were not part of this observation. Artifacts, equal-binary hashes, and scripts are under `out/msvc-combined-reader-20261106/`.

Six further alternating 100,000-frame pairs reproduced the discrepancy with normal priority and affinity mask 4 for both workers. Median throughput was 6,720.75 versus 8,533.13 MiB/s; worker elapsed time was 14.8793 versus 11.7191 seconds, and whole-process user+kernel CPU time was 14.7891 versus 11.7109 seconds. Thus scheduler waiting is not the dominant difference. The original repeatedly mapped at `0x7FF6FA030000`, the duplicate at `0x7FF7EF590000` (one early metadata query failed and is recorded). Disposable copies with dynamic-base/high-entropy image flags cleared ran closer together, but **did not stay at their preferred address**: runtime policy reported forced relocation, bottom-up randomization, and high entropy enabled. This is not a successful fixed-address experiment or proof that ASLR causes the slowdown. No system policy or production binary settings were changed. Process metadata, CPU times, and policy observations are in `process-control/`, `fixed-control/`, and `mitigation-control.txt` beneath the artifact directory. The next control must sample multiple unchanged image instances and distinguish image-related execution effects from other per-process effects.

Three rotating 30,000-frame trials across eight byte-identical images confirmed that the original is an outlier. Median mixed3 throughput was 6,349.84 MiB/s for `baseline.exe`; the other seven images ranged from 8,423.95 to 8,540.47 MiB/s. All hashes were rechecked, all processes used normal priority and affinity mask 4, and the runtime DLL mapped at the same address. Each executable retained its own image address across trials. Data: `out/msvc-combined-reader-20261106/replica-control/processes.csv`.

AMD uProf 5.3 hardware-counter collection works on this Windows/Hyper-V installation, but its `assess` profile did **not reproduce the slow/fast difference**. Sequential 300,000-frame baseline/copy runs took 60.2176/60.0051 seconds (4,981.93/4,999.57 MiB/s). Reported process CPI was 0.3127/0.3112 and branch mispredictions per thousand instructions were 0.6235/0.6194. These are sampled profile metrics, not exact counter totals or unprofiled performance results. Three unprofiled elevated control pairs still showed baseline throughput varying from 5,922.23 to 8,176.08 MiB/s versus copy throughput 8,300.37–8,510.59 MiB/s, with the same image addresses. Thus elevation alone does not explain the discrepancy, and these hardware profiles cannot identify its cause. Reports: `out/uprof-identical-assess-20261106/`; control script/data: `out/msvc-combined-reader-20261106/elevated-control.ps1` and `elevated-control/`.

Lighter `tbp` profiling (1 ms timer, no call stacks) also failed to preserve the gap: baseline/copy took 37.0141/37.4116 seconds for 300,000 frames. Their hottest sampled application address was the same RVA `0x61fc`, accounting for 7.409/7.503 sampled seconds. The matching production disassembly maps this to `mov r8d,[rbp-0x14]` in sequence decoding; sampling attribution is not proof that this load causes the slowdown. Reports: `out/uprof-identical-tbp-20261106/`. Next: try substantially sparser sampling with unprofiled controls before and after it, and verify the slowdown survives collection before interpreting profile differences. No optimization or benchmark-table update is justified by these profiles alone.

An explicit 50 ms timer profile preserved a smaller difference: baseline/copy worker times were 45.5412/42.1660 seconds for 300,000 frames. Three unprofiled pairs immediately before profiling had median throughput 7,468.91/8,464.89 MiB/s; three after had 6,445.12/8,457.80 MiB/s, with unchanged image addresses. Sampled application-module time was 40.35/36.30 seconds, while runtime-DLL time was 3.85/4.75 seconds. This locates the observed extra sampled time in application code, but one sparse pair cannot establish an instruction-level cause or distinguish cache/predictor effects. The report confirms a 50 ms interval. Artifacts: `out/uprof-identical-sparse-20261106/` and `out/msvc-combined-reader-20261106/sparse-{before,after}/`.

The experiment also added permanent malformed-frame interoperability coverage: 36,578 cases flip each compressed-payload bit, shorten declared payloads to reach entropy decoding, compare reference/C++ acceptance and successful output, and verify output guards. This passed for the candidate and restored MSVC implementation, and for the restored GCC implementation.

## MSVC scheduled sequence reservoir, 2026-11-06

The medium-block MSVC decoder now uses a byte-aligned 64-bit reservoir with a consumed-bit count. It reloads after initial FSE states and after each non-final sequence, with an extra reload between match/literal extras when their combined widths require it. The validated table-width bounds keep interior reads within the reservoir; short input heads retain per-read truncation checks. This implements the reference's x64 refill schedule rather than preparing the previous absolute-bit-position reader. No allocations are added.

Six alternating CPU2-pinned 30,000-frame old/new pairs measured **+7.56%** on structured level 18 (6/6 wins), **−0.87%** on structured level 16 (2/6 wins), and **−3.02%** on mixed level 3 (0/6 wins). The tradeoff targets the larger level-18 deficit. Final executable size increases from **102.00 to 103.00 KiB** (the initial prototype was 103.50 KiB). Final formatting and an explicit unsigned conversion preserved all 18,785 instruction addresses/opcodes/operands except RIP-relative data addresses; current/reference measurements use the finalized worker.

MSVC unit, reservoir, and byte-parity acceptance tests passed (247.05 seconds for parity). After a signed-conversion warning was fixed, all 13 GCC tests passed; the final permanent reservoir test also passed ASan/UBSan. Its differential coverage includes 32,768 random streams, short heads, exact exhaustion, zero-width reads, and missing markers. The GCC decoder retains SHA-256 `83dfa13880328d2d64de9a1b57ea0be3a2f4bcf178fd64198874fc7738db4ba9`, so the GCC performance matrix is preserved. Artifacts are under `out/msvc-scheduled-reader-20261106/`.

The [current native matrix](BENCHMARKS_NATIVE.md) and README contain all nine metrics for 22 levels and five inputs, from 678 worker trials. The three longer comparisons below use six 30,000-frame pairs; other rows use three 1,000-frame screening pairs. Every worker checked exact output at warmup, and C++ workers checked output size and ordinary allocations during timing. Trial data and worker/frame hashes are in `out/msvc-scheduled-reader-20261106/current-matrix/`.

| Input | Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| structured | 16 | 17974.65 | 18106.13 | 99.27% | 6.91 | 6.82 | 101.32% | 103.00 | 113.50 | 90.75% |
| structured | 18 | 14517.80 | 14896.12 | 97.46% | 6.91 | 6.82 | 101.32% | 103.00 | 113.50 | 90.75% |
| mixed | 3 | 8448.74 | 5427.22 | 155.67% | 7.19 | 7.09 | 101.38% | 103.00 | 113.50 | 90.75% |

Both structured rows remain below reference, so the all-level goal remains open. Absolute speeds vary between measurement sessions; the paired old/new experiment establishes the improvement, while these contemporaneous C++/reference pairs describe the remaining gap.

## MSVC paired literal/match state reads, 2026-11-06

The medium-block decoder now reads the adjacent literal-length and match-length FSE state fields together under MSVC, then reads the offset state separately. This removes one checked reader operation per sequence without the splitting work of the previously rejected three-field grouping. Validated table logs bound the paired field to 18 bits; the existing reader retains overflow detection. No buffer, allocation, or unchecked-input mode is added.

Six alternating CPU2-pinned 30,000-frame old/new comparisons measured **+3.45%** on structured level 18 (6/6 wins), **−0.10%** on structured level 16 (2/6 wins, effectively flat), and **+2.02%** on mixed level 3 (6/6 wins). The executable remains **102.00 KiB**. These paired improvements compare the immediate prior implementation, not the reference library.

MSVC unit and byte-parity acceptance tests passed (262.76 seconds for parity), as did all 12 GCC tests. The rebuilt GCC decoder retains SHA-256 `83dfa13880328d2d64de9a1b57ea0be3a2f4bcf178fd64198874fc7738db4ba9`, preserving the existing GCC matrix. Native comparison artifacts are under `out/msvc-paired-state-20261106/`.

The refreshed [native matrix](BENCHMARKS_NATIVE.md) and README include all nine metrics for 22 levels and five inputs (678 worker trials). Longer current/reference comparisons are below. Other rows use three 1,000-frame pairs and remain screening measurements; the three rows below use six 30,000-frame pairs. Every worker checked exact decoded bytes at warmup, and C++ workers checked output size and ordinary allocations during timing.

| Input | Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| structured | 16 | 19906.24 | 19537.48 | 101.89% | 6.91 | 6.82 | 101.26% | 102.00 | 113.50 | 89.87% |
| structured | 18 | 14701.70 | 16434.07 | 89.46% | 6.91 | 6.82 | 101.26% | 102.00 | 113.50 | 89.87% |
| mixed | 3 | 8775.28 | 5647.84 | 155.37% | 7.19 | 7.10 | 101.29% | 102.00 | 113.50 | 89.87% |

Raw trials and worker/frame hashes are in `out/msvc-paired-state-20261106/current-matrix/`. Native structured level 18 remains below reference; this improvement does not complete the all-level performance goal. The older measurements below are dated snapshots.

## MSVC exact tiny matches and overlap seeds, 2026-11-06

MSVC now uses the existing noinline exact-width helper for 1–7-byte nonoverlapping matches and the initial seed of overlapping matches in the medium-block decoder. The helper writes only the requested bytes, and subsequent doubling copies are unchanged. No buffer or allocation is added. Rebuilding the GCC 13 worker produced the same SHA-256 as before, so the GCC matrix remains applicable.

Six alternating CPU2-pinned 30,000-frame old/new native pairs measured structured level 18 **+6.94%** and level 16 **+1.39%**, both with six wins. Mixed level 3 regressed **2.74%** (one win); the retained tradeoff improves the larger structured deficit while mixed still substantially leads reference. Enabling only nonoverlapping tiny matches gained 1.95% at level 18; adding overlap seeds produced the larger improvement.

The refreshed [native matrix](BENCHMARKS_NATIVE.md) covers all 22 frame levels and five inputs. Longer candidate/reference rows are below; other rows use three 1,000-frame screening pairs. These current/reference ratios are distinct from the paired old/new improvement above.

| Input | Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| structured | 16 | 19767.12 | 19679.12 | 100.45% | 6.90 | 6.82 | 101.26% | 102.00 | 113.50 | 89.87% |
| structured | 18 | 13798.76 | 16341.31 | 84.44% | 6.90 | 6.82 | 101.20% | 102.00 | 113.50 | 89.87% |
| mixed | 3 | 8201.80 | 5390.77 | 152.15% | 7.18 | 7.09 | 101.27% | 102.00 | 113.50 | 89.87% |

All 12 GCC tests and MSVC unit/byte-parity acceptance tests passed. Every native benchmark worker checked exact output at warmup, and C++ workers checked output size and no ordinary allocations during timing. The full native matrix used the same reference frames as the GCC matrix; it does not claim a new native compression measurement. Old/new trials, matrix provenance, and commands are under `out/msvc-tiny-match-20261106/` and `out/msvc-tiny-seed-20261106/`.

To reproduce native decoder measurements after building the optimized workers and generating the reference-frame matrix, run PowerShell 7 with an empty output directory. Rendering requires Python 3 (shown as `python`; WSL `python3` also works from the repository root):

```powershell
./eval/benchmark_native_decoders.ps1 -CppWorker ./out/build/msvc-release-eval/eval/bin/sph_decompress_l1.exe -ReferenceWorker ./out/build/msvc-release-eval/eval/bin/reference_decompress_l1.exe -MatrixDirectory ./out/overlap-seed-helper-20261106/matrix -OutputDirectory ./out/native-decoder-matrix
python eval/render_native_decoder_matrix.py ./out/native-decoder-matrix/matrix-trials.csv --date YYYY-MM-DD
```

The report renderer checks complete coverage, distinct trials, equal work, and the documented trial counts before publishing. The GCC renderer preserves the separate native README section. Older native results below predate this helper port.

## Native MSVC decoder recheck, 2026-11-06

Rebuilt the native decoder worker from current source using the existing MSVC Release `/O2 /Ob3` evaluation configuration. Six alternating 30,000-frame pairs, pinned to Windows logical CPU 2, decoded the same structured reference frames used in the GCC matrix. The generic level-1 decoder executable was used for both frame levels; decoding settings do not depend on its filename. Workers passed exact warmup output and allocation checks. These native results are separate from the GCC README matrix and are not a claimed implementation improvement.

| Frame level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 19382.69 | 19775.56 | 98.01% | 6.90 | 6.82 | 101.20% | 102.00 | 113.50 | 89.87% |
| 18 | 13178.06 | 16373.06 | 80.49% | 6.90 | 6.82 | 101.14% | 102.00 | 113.50 | 89.87% |

Level 18 provides a larger optimization target than the recent near-parity GCC controls. A concrete implementation difference to investigate is the exact tiny-match helper: GCC 13 uses it for short nonoverlapping matches and overlap seeds, while MSVC currently does not. This is a hypothesis requiring profiling or a controlled port, not attribution established by these timing measurements. Commands, raw worker metrics, and executable hashes: `out/native-decoder-recheck-20261106/`.

## Structured levels 7 and 14 confirmed, 2026-11-06

The earlier short-screen decoder ratios of 92.38% and 92.73% did not persist. Three alternating 100,000-frame `perf stat` pairs measured level 7 at 95.15% of reference cycle cost and 103.94% of reference throughput, and level 14 at 94.81% cycle cost and 106.13% throughput. Every worker passed its output and allocation checks.

Six further alternating 100,000-frame pairs without the profiler measured the following medians. These are measurement corrections for the unchanged implementation, not new algorithmic speedups. The README and current matrix now use these two rows.

| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 7 | 26829.32 | 25872.45 | 103.70% | 12.97 | 12.97 | 100.00% | 125.31 | 202.89 | 61.76% |
| 14 | 27669.58 | 26308.71 | 105.17% | 12.97 | 12.97 | 100.00% | 125.31 | 202.89 | 61.76% |

Hash checks show identical structured frames within levels 6–7, 8–10, 11–15, 16–17, and 19–22; levels 3–4 also match. All 22 decoder executables are identical within each implementation. Differences among short-screen decoder rows within these groups are measurement variation, not different decoder algorithms. The report preserves individual measurements rather than replacing unmeasured rows with another level's timing.

`eval/render_current_matrix.py --confirmed-decoder-pair structured 7 DIRECTORY` now accepts six alternating 100,000-frame decoder CSV pairs for any supported input and level. Files are named `cpp-1.csv` through `cpp-6.csv` and `reference-1.csv` through `reference-6.csv`. Duplicate overrides, unsupported inputs/levels, and conflicts with existing decoder-confirmation options are rejected before writing reports. Raw trials, executable/frame hashes, verification, and the complete refresh command are under `out/structured-mid-gap-20261106/`.

## Exact tiny overlap seeds, 2026-11-06

The GCC 13 medium-block decoder now uses its existing exact-width tiny-copy helper for the initial 1–7-byte seed of an overlapping match. The seed source ends at the destination, so this copy does not overlap. Subsequent doubling copies retain `memcpy`; no buffer or allocation is added. Other compiler paths retain the previous behavior.

Three alternating 100,000-frame counter pairs on level-3 mixed input measured **2.93% fewer cycles** and **2.27% higher throughput** against the saved previous C++ decoder, with all three cycle pairs improving. Instructions/frame were essentially unchanged (2,139,763 versus 2,139,755), suggesting a code-generation/layout effect rather than fewer executed instructions. Level-21 structured cycle cost was effectively neutral at 100.21% of the previous decoder. The counter tool labels the old worker `reference`; these diagnostic comparisons are against old C++, not bundled zstd.

Applying the helper to both the seed and doubling steps improved structured level 18 but lost 6.71% on the short mixed level-3 control. Applying it only to doubling steps was neutral. Only the seed change is retained. Its six 30,000-frame old/new pairs measured mixed level 3 +2.84% (6/6 wins), structured level 18 +0.13% (3/6), and structured level 21 +1.20% (4/6).

The fresh five-input, 22-level matrix passed. Six longer alternating decoder pairs measured structured levels 16–22 at **99.74%, 100.37%, 101.66%, 97.88%, 102.54%, 102.87%, and 99.23%** of bundled-reference throughput. Random levels 1 and 2 measured **102.04% and 98.23%**. These near-parity measurements do not establish superiority at every level. The README and [current matrix](BENCHMARKS_INPUTS.md) contain all nine speed, RSS, and executable-size metrics for every level/input. Longer confirmations use 10,000 frames for structured data and 100,000 for random; unchanged compression retains existing longer confirmations. Older sections below are dated snapshots.

Validation: all 12 optimized GCC tests and the MSVC unit and compression-parity tests passed. Focused ASan/UBSan checks passed guarded output bounds, truncated frames, reset after errors, and an exact overlapping-copy oracle. The C++ decoder workers validate exact output during warmup and check output size and ordinary allocation counts during measurement; this is not a byte comparison of every timed frame.

All 22 rebuilt C++ decoder workers have SHA-256 `83dfa13880328d2d64de9a1b57ea0be3a2f4bcf178fd64198874fc7738db4ba9`, matching the counter experiment. Raw trials, counter provenance, and report scripts: `out/overlap-seed-helper-20261106/`.

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
