# Benchmark results

This is the detailed record of profiling, optimization experiments, and benchmark reproduction. Entries include rejected changes and historical measurements; they are not all results for the current implementation. See the [README](README.md#performance) for current performance tables, the [full input matrix](BENCHMARKS_INPUTS.md) and [Windows decoder matrix](BENCHMARKS_NATIVE.md) for their measurement conditions, and the [performance work list](PERFORMANCE_PLAN.md) for priorities and task status. Earlier progress notes are preserved in [development history](PERFORMANCE_HISTORY.md).

## Mixed-input batching regression and state-isolation controls, 2026-11-07

Continuing the paired LL/ML experiment, sampled mixed-input cycles with the saved production baseline and outlined candidate for 30,000 frames each. Both captures have zero lost samples and zero throttle records. Approximate sampled totals are 40.893B versus 45.061B cycles. The candidate has separate `encode_bits<true>` (7.06%), `count_matching_bytes` (6.74%) and dominant-prefix counting (5.75%) symbols above the report threshold; none appears separately above 0.5% in the baseline. Baseline parser/encoder shares are 60.29%/23.92%, candidate 49.80%/18.59%. Different inlining means these percentages cannot be compared as equal source scopes, nor do they prove code alignment is the cause. Annotated assembly confirms changed FSE call sites; both binaries still have some non-inlined FSE transitions.

Two isolated controls were then measured independently with optimized GCC13, CPU2, six rotating production/candidate/reference triples per input and 10,000 frames per trial. The first forces the small FSE `encode_bits` method inline in the previous outlined candidate. The second starts from the piecewise prototype, moves the entire new batching writer into a separate non-inlined helper owning its own FSE/writer state, and checks eligibility before constructing the ordinary writer states. It does not force FSE inlining. Failed table/range eligibility returns before mutating output; general and existing RLE writers remain the fallback.

| Control | Input | Old C++ MiB/s | Candidate MiB/s | Reference MiB/s | Median paired change | Wins vs old |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Inline FSE transition | varied | 3355.64 | 3555.22 | 3463.08 | +6.82% | 6/6 |
| Inline FSE transition | mixed | 3271.13 | 3189.55 | 2703.68 | -3.65% | 0/6 |
| Standalone batching state | varied | 3425.19 | 3652.62 | 3708.97 | +7.24% | 6/6 |
| Standalone batching state | mixed | 3298.19 | 3180.48 | 2664.42 | -3.85% | 0/6 |

Forcing the transition inline does not resolve the mixed regression. Its varied/reference paired median is 100.90% (4/6 wins), but reference timing is lower than in the independent standalone run, so this is not a confirmed reference-speed win. The standalone candidate reaches 98.78% of reference by paired median (1/6 wins). Its mixed throughput remains above reference, but the cost versus current production still needs attribution and all-input validation. Worker sizes are 128.9765625/129.1015625 KiB versus current128.21875 KiB; raw results include all memory/code metrics and identities. Neither control is retained and published production tables are unchanged.

Both candidates pass an expanded 2,160-case ASan/UBSan scalar-versus-batched bitstream oracle: table logs5/7/9, rare counts-1/1/2/3/7/15, changing payloads, interrupted runs, dispatch boundaries and output prefixes. All72 plain timing frames match reference and C++ allocation guards pass; eight decoder checks recover the original inputs. The two mixed profiling frames and subsequent standalone varied profiling frame also match reference and pass guards. These controls do not replace all-level acceptance before integration.

A separate 30,000-frame standalone varied cycle profile has zero lost/throttled records and37.216B approximate cycles: parser52.39%, shared encoder10.01%, Huffman literal work7.17%, Huffman stream5.08%, Huffman tree4.94%, ordinary wide sequence writer3.52%, and new batching helper2.70%. The ordinary wide writer now appears as a separate function, whereas the baseline mixed profile has it folded into the shared encoder. This provides a concrete next control: explicitly inline the existing `write_sequence_bitstream` while keeping new batching state in the standalone helper, then test mixed and varied together. Avoid treating source-level `inline` as proof of emitted inlining or tuning division solely because it appears in assembly. Even with batching, parser and Huffman costs remain larger targets.

Artifacts: `out/paired-fse-mixed-profile-20261107/` (perf data, annotated assembly, throttle audit); `out/paired-fse-inline-transition-20261107/`; `out/standalone-paired-fse-20261107/` (preparation, plain triples, expanded oracles, standalone profile). Next retain only a fully validated revision with explicit tradeoffs and refreshed nine-metric all-shape results; the all-level objective remains open.

## Paired length-state FSE batching experiments, 2026-11-07

The varied level-2 run audit found 7,027 of 7,033 reverse-loop sequences per frame with zero literal extra bits, seven match extra bits and offset code zero. There are 75 identical-code runs across eight blocks; 6,961 sequences lie in runs of at least four. Most payloads happen to match, but exceptions exist: the implementation must read each actual match length. Only the first block has a compressed offset table; the other seven have OF-RLE. Mixed and structured inputs have no eligible seven-bit sequences in this audit.

An isolated prototype advances both non-RLE LL and ML states over validated zero-bit ranges and packs four actual seven-bit payloads per write. Ordinary transitions handle bit-emitting boundaries and interrupting symbols. A single affine range only covers two of the seven eligible varied blocks: six paired trials were effectively neutral on varied (+0.10%) and lost mixed throughput (-2.88%). A revision represents up to eight contiguous constant-increment ranges per symbol, rejects unsupported tables, and keeps the existing fallback. Its range storage is explicitly zero-initialized. Setup, bounded code scans and actual payload reads are included in timings; no corpus-specific payload constant or persistent cache is used.

The corrected piecewise version and a second version with its batching body in a non-inlined lambda were each measured in six rotating old/candidate/reference triples per input, 10,000 frames per worker, optimized GCC 13 `-O3 -DNDEBUG`, CPU 2. Baseline is retained commit `5655a4f`. These are experimental measurements, not published production replacements.

| Candidate | Input | Old C++ MiB/s | Candidate MiB/s | Reference MiB/s | Median paired change | Wins vs old |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Piecewise | varied | 3435.53 | 3586.33 | 3739.48 | +4.20% | 6/6 |
| Piecewise | mixed | 3326.55 | 3152.47 | 2718.01 | -5.13% | 0/6 |
| Outlined piecewise | varied | 3419.13 | 3580.06 | 3687.40 | +4.46% | 5/6 |
| Outlined piecewise | mixed | 3301.54 | 3148.50 | 2730.02 | -4.56% | 0/6 |

Outlining preserves the varied gain but does not recover mixed throughput. It is not evidence that code placement alone caused the mixed regression. Candidate executable sizes are 128.859375 and 129.078125 KiB, versus baseline 128.21875 KiB and reference 632.515625 KiB. Per-trial RSS, code sizes, speeds and binary/input identities are stored with the raw results. The initial piecewise run before explicit range initialization is excluded from these conclusions.

Three additional rotating counter triples on varied input (10,000 frames each) give these medians for the outlined version. Counters ran separately from plain timing and sanitizer compilation.

| Counter | Old C++ | Outlined candidate | Reference |
| --- | ---: | ---: | ---: |
| Cycles | 13,186,349,343 | 12,487,824,113 | 12,106,130,363 |
| Instructions | 46,349,130,568 | 42,531,908,621 | 39,684,406,018 |
| Branches | 6,775,832,053 | 6,308,845,127 | 5,156,446,738 |
| Branch misses | 5,775,139 | 5,658,021 | 31,748,419 |

Instructions fall 8.24% and cycles 5.30%, confirming useful work removal, but candidate cycles remain 3.15% above reference. Neither fewer instructions nor fewer branch misses proves the all-level speed objective.

All 72 plain-timing compression frames and nine counter-run frames match reference. C++ workers pass their post-warmup ordinary/aligned allocation guards. Eight decoder checks recover the source. Each candidate passes the independent 240-case scalar-versus-batched stream oracle under ASan/UBSan: changing seven-bit payloads, rare normalized counts of either sign, interrupted runs, sizes around the 256-sequence dispatch boundary, and output prefixes. This is prototype validation, not a substitute for all-level acceptance and broader normalized-table tests before integration.

Neither candidate is retained. Production headers and README performance numbers remain unchanged. Next: profile the mixed-input regression against the saved baseline, distinguishing existing writer/parser costs from the new path's guard and stack-frame costs; then reconsider placement or dispatch using that evidence. Before retaining a revision, broaden state-range tests, run all-level byte parity/interoperability, and measure the full input matrix plus short-input controls. Varied level 2 and high-level decoder gaps remain open.

Artifacts: `out/varied-run-audit-20261107/`, `out/paired-fse-runs-20261107/`, corrected `out/piecewise-fse-runs-20261107/final/`, and `out/outlined-piecewise-fse-20261107/` (`final/`, `counters.sh`, counter CSVs, and `oracle.txt`). The latter's `prepare.py` and `run.sh` reproduce the isolated outlining control.

## Batched zero-extra-bit FSE runs retained, 2026-11-07

The adaptive wide sequence writer now batches identical ML symbols when the existing LL/OF-RLE path has no literal or offset extra bits and the ML symbol has no extra bits. It scans matching code bytes with bounded8-byte loads plus an exact scalar tail. For each run, `encode_zero_extra_run` validates whether the zero-bit state range has a constant nonnegative increment, advances multiple states at once within that range, and uses the ordinary FSE transition at bit-emitting boundaries or on nonlinear tables. RLE validation, initial/final states, bit ordering and output capacity handling remain intact. Short runs skip range validation unless their length reaches the table size, bounding setup cost relative to useful work. No persistent cache, workspace growth or heap allocation is introduced.

This removes iterations in long runs rather than adding the rejected per-symbol specialization. It is reached by the existing GCC13 level2 adaptive policy at1MiB and short double-fast sizes. Other production paths retain their previous behavior; rebuilt level1/3/18 executables are byte-identical to their saved versions. The level2 worker grows280 bytes,127.9453125→128.21875KiB. Memory figures below remain whole-process peak RSS and contain page-level variation, not evidence of a workspace change.

Final optimized GCC13 `-O3 -DNDEBUG` CPU2 results use six rotating saved-production/current/reference triples per input. All108 compression workers match reference frames and pass ordinary/aligned allocation guards; all12 decoder checks recover the source bytes. These are measurements of the final short-run-bounded implementation, not the initial prototype.

| Input | Frames/trial | Median paired change vs old C++ | Wins |
| --- | ---: | ---: | ---: |
| structured | 2000 | 14.42% | 6/6 |
| mixed | 10000 | 11.63% | 6/6 |
| varied | 10000 | -0.23% | 3/6 |
| zero | 10000 | 0.65% | 5/6 |
| repeated | 10000 | 0.14% | 3/6 |
| random | 4000 | -1.21% | 1/6 |

| Input | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| structured | 989.03 | 844.95 | 117.05% | 5.48 | 6.23 | 87.84% | 128.22 | 632.52 | 20.27% |
| mixed | 3286.82 | 2666.22 | 123.28% | 5.65 | 6.24 | 90.60% | 128.22 | 632.52 | 20.27% |
| varied | 3435.21 | 3740.78 | 91.83% | 5.36 | 6.11 | 87.60% | 128.22 | 632.52 | 20.27% |
| zero | 39355.72 | 11367.36 | 346.22% | 5.33 | 6.12 | 87.19% | 128.22 | 632.52 | 20.27% |
| repeated | 23301.69 | 14002.40 | 166.41% | 5.31 | 6.14 | 86.57% | 128.22 | 632.52 | 20.27% |
| random | 3687.32 | 2947.63 | 125.09% | 6.29 | 6.22 | 101.10% | 128.22 | 632.52 | 20.27% |
Structured and mixed improve14.42%/11.63% in all six pairs each. Varied is effectively unchanged (-0.23%,3/6 wins) and remains below reference; random loses1.21% (1/6), an explicit retained tradeoff while still125.09% of reference by ratio of medians. Repeated/zero are near-neutral. The initial unbounded prototype appeared to improve repeated13.59%; that gain did not survive the final source change and is not claimed for this implementation. Initial and final full matrices are stored separately under `out/fse-zero-runs-20261107/final/` and `bounded-final/`.

The other affected256-KiB level2 specialization passes six alternating12,000-frame pairs per input: varied+0.05%(3/6;3445.50→3443.09MiB/s) and mixed+1.19%(6/6;2151.94→2176.39MiB/s). All24 frames/guards and four decoder checks pass. These controls use the established genuine mixed and varied short inputs from `out/l3-short-offbase-20261107/`; baseline headers come from commit9701edf. Per-trial paired changes and ratios of separate speed medians are different statistics.

Strict optimized unit, compact-sequence, FSE-transition and all22-level byte-parity/interoperability tests pass (19.12s total,18.21s acceptance). Acceptance includes the256-KiB level2 cases. Permanent tests compare batched emitted streams/final states with the unchanged scalar transition across generated normalized and RLE tables, and compare wide batched streams with scalar streams in612 cases spanning run boundaries1/7/8/9/127/128/511/512/1024/4096, interrupting symbols with extra bits, nonlinear tables, and output prefixes. The expanded FSE tests pass ASan/UBSan; the existing index oracle covers3,676,128 generated/negative-one state cases. No MSVC throughput claim is made; its production policy does not select this adaptive wide path.

README and BENCHMARKS_INPUTS now carry all nine final metrics for level2's five main shapes; README also refreshes the all-level varied table's level2 row. Other rows are preserved. Source, scripts, oracle, raw trials, binary/input identities and validation artifacts: `out/fse-zero-runs-20261107/`.

Next audit runs of identical LL/ML/OF symbols and the actual seven-bit ML payload values in varied2 before attempting batching with a non-RLE literal table. Such an extension must preserve interleaving at every bit-emitting state boundary; symbol dominance alone does not prove constant payloads or permit skipping records. The varied2 gap and high-level decoder gaps remain open; this retained improvement does not complete the all-level objective.

## Dominant-symbol linear FSE transition control, 2026-11-07

The isolated table audit inspects the actual selected table, dominant code, table log, and every incoming state in that code's zero-bit range. Each input uses one warmup plus one frame; all three diagnostic frames match reference. Varied level2 uses log7 length tables (128 states), correcting the earlier conservative512-state setup estimate. Dominant LL3 has either124 zero-bit incoming states with increments3–4 or126 with constant increment2. Dominant ML43 likewise alternates between constant+2 and increments3–4. OF is RLE in seven of eight blocks. Structured input instead has a mixed first block followed by14 blocks of4096 sequences: LL/OF are RLE, ML12 uses log9 with510 zero-bit states and constant increment1. Audit artifacts: `out/dominant-fse-audit-20261107/`.

An isolated candidate chooses a proposed common symbol from the middle sequence, validates a constant increment across its entire zero-bit incoming-state range, and records the symbol, range limit, and increment. Both length states are prepared once per adaptive wide/grouped block. On a matching symbol/state it adds the validated increment and returns zero emitted bits; otherwise it calls the existing general transition. No histogram pass, expanded transition cache, cross-frame memoization, or heap allocation is added. Setup cost is included in all timing. The choice of middle symbol affects opportunity only, not correctness.

Six alternating optimized GCC13 CPU2 pairs of10,000 frames per input:

| Input | Production MiB/s | Candidate MiB/s | Median paired change | Wins |
| --- | ---: | ---: | ---: | ---: |
| Varied | 3457.92 | 3346.90 | -3.30% | 1/6 |
| Mixed | 2960.31 | 2963.07 | +0.17% | 3/6 |

Three additional varied counter pairs measure production/candidate medians: cycles13.124889/13.418449B (+2.24%), instructions46.333430/45.706158B (-1.35%), branches6.773952/6.861231B, branch misses5.826541/8.147678M. Reducing the table work per successful transition does not improve overall execution.

The modified transition oracle passes ASan/UBSan. It compares every specialized emitted value/width and final state with the unchanged general transition across RLE tables and generated normalized tables, including negative-one counts; its existing index checks cover3,676,128 cases. All24 plain timing workers,6 counter workers, and2 profile workers emit exact reference frames and pass allocation guards. Broad all-level acceptance was not run for the rejected candidate.

Both30,000-frame cycle profiles have zero lost samples and zero throttle records. Production/candidate sampled totals39.692/40.293B; parser shares49.70%/47.89%, broad sequence encoder22.23%/22.31%, Huffman literal selector6.83%/8.27%. The parser moved from0x101b0 to0xfdc0, but the profiles do not support blaming a slower parser for the regression. These are sampled functions, not isolated stage timings; do not infer that setup alone caused the slowdown. Worker size falls131016→130936 bytes without a throughput benefit.

Reject the candidate; production headers and README metrics remain unchanged. Reproduction, oracle, assembly, counters, profiles and raw trials are in `out/linear-fse-transition-20261107/`.

Next assess bulk advancement through runs of identical zero-extra-bit sequences in the existing LL/OF-RLE shortcut. A validated constant-increment range can permit advancing several FSE transitions at once, avoiding loop iterations rather than adding a per-symbol branch. Preserve the general fallback and exact state/output order, bound all code-array scans, and include varied2 as a regression control. This opportunity primarily affects structured/mixed blocks; it does not resolve the still-open varied2 gap by itself. Keep that gap and the high-level decoder gaps on the task list, and avoid further per-symbol branch/metadata variants without new evidence.

## Level-2 flush cadence and empty FSE emissions, 2026-11-07

An isolated instrumented header copy records adaptive writer selection, requested widths, flush occupancy, grouped FSE widths (including the ML-only shortcut), and sequence symbol frequencies. Each diagnostic runs one warmup plus one measured frame; all counts below cover both frames. Instrumented timings are not performance results. All three diagnostic frames match reference exactly. Artifacts: `out/l2-flush-audit-20261107/`, including the reproducible instrumentation script and raw histograms.

| Input | Narrow / wide blocks | Narrow / wide sequences | Zero-bit FSE emissions / loop emissions | Wide pre-overflow / post-threshold flushes | Mean bits at flush |
| --- | ---: | ---: | ---: | ---: | ---: |
| Varied | 0 / 16 | 0 / 14082 | 13416 / 14066 (95.38%) | 0 / 1786 | 58.90 |
| Mixed | 2 / 14 | 2 / 33286 | 29662 / 33272 (89.15%) | 36 / 220 | 58.60 |
| Structured | 0 / 30 | 0 / 131586 | 127524 / 131556 (96.94%) | 62 / 242 | 57.06 |

For varied input, checked appends total42,280, of which28,162 request zero bits. The unchecked writer receives28,184 calls, including13,416 zero-bit FSE groups. Nearly all nonzero extra fields have width7. No sequence in the three traced inputs reaches the reference's31-extra-bit threshold for an additional pre-extra flush. Reference `ZSTD_encodeSequences_body` on64-bit systems flushes after every reverse-loop sequence; C++ already accumulates roughly eight varied sequences per flush. This is a source-derived cadence comparison, not a separately instrumented reference count. There is no evidence that C++ flushes too frequently on this workload.

Two isolated optimized GCC13 level2 candidates tested the empty-emission opportunity against current production, using six alternating10,000-frame pairs per input onCPU2:

| Candidate | Varied paired change | Varied wins | Mixed paired change | Mixed wins |
| --- | ---: | ---: | ---: | ---: |
| Return early from wide `append_unchecked` for zero width | -0.07% | 3/6 | -0.86% | 2/6 |
| Skip empty groups before constructing their combined value | -1.32% | 1/6 | -0.71% | 2/6 |

The second variant checks the total FSE width before the shifts/ORs in the adaptive helper, and checks the ML-only width in its shortcut; it does not include the first variant's writer-level guard. Both preserve state transitions, ordering, final flushes and capacity logic. For valid FSE outputs, zero width implies zero value, so omitting the write preserves payload bytes.

Three additional varied counter pairs for the first candidate measure production/candidate median instructions46.333428/45.496635B (-1.81%), cycles13.176160/13.059405B (-0.89%), and branches6.773951/6.710115B. Reduced work in these counter runs does not establish a throughput gain in the unprofiled pairs. Both candidates are rejected. All48 ordinary timed candidate/control workers and6 counter workers match reference frames and pass allocation guards. Broad sanitizer/unit qualification was not run for rejected candidates. Production and README measurements remain unchanged. Artifacts: `out/wide-zero-write-20261107/`, `out/empty-fse-group-20261107/`.

The symbol audit identifies a more specific structural question:

| Input | Dominant LL code and frequency | Dominant ML code and frequency | Dominant OF code and frequency |
| --- | ---: | ---: | ---: |
| Varied | 3 (99.05%) | 43 (99.83%) | 0 (99.99%) |
| Mixed | 1 (97.55%) | 12 (96.87%) | 0 (99.32%) |
| Structured | 1 (99.38%) | 12 (99.20%) | 0 (99.83%) |

Next evaluate a block-scoped dominant-symbol FSE transition specialization: cache that symbol's transform and exploit zero-bit transition ranges while preserving exact state updates and a general fallback. This is distinct from skipping the writer after all transitions are already computed. First inspect actual table logs and reachable state patterns and account for setup cost; varied has only about880 sequences per block, so blindly expanding all512 possible states for both length tables could cost more than it saves. Do not introduce cross-frame memoization that benefits only identical benchmark frames, or repeat flush/empty-write variants without new evidence. The all-level goal remains incomplete.

## Level-2 FSE residue and local wide-writer controls, 2026-11-07

Two isolated GCC13 `-O3 -DNDEBUG` candidates target the current 1-MiB level-2 varied-input entropy path. Each uses six alternating 10,000-frame production/candidate pairs per input on CPU2, followed by three 10,000-frame hardware-counter pairs on varied input. No production headers were modified.

The first candidate reuses `quotient = value >> number_bits` for both the next-state index and `emitted = value - (quotient << number_bits)`, replacing the original mask expression in `encode_bits`. RLE checks and final-state behavior remain unchanged. Assembly confirms the shared shift result, followed by a shift/subtract for emitted bits. Valid sequence-table widths are at most nine, so the unsigned residue equals the original low-bit mask, including zero width. This introduces no tables or workspace.

| Candidate | Input | Median paired throughput change | Wins | Production MiB/s | Candidate MiB/s |
| --- | --- | ---: | ---: | ---: | ---: |
| Shared FSE quotient | Varied | +0.31% | 3/6 | 3424.64 | 3406.84 |
| Shared FSE quotient | Mixed | +0.32% | 4/6 | 2968.99 | 2985.04 |
| Local adaptive wide writer | Varied, initial | +2.09% | 5/6 | 3325.37 | 3402.30 |
| Local adaptive wide writer | Mixed | -0.26% | 2/6 | 2927.50 | 2901.16 |
| Local adaptive wide writer | Varied, independent confirmation | -0.18% | 2/6 | 3420.59 | 3418.34 |

Paired changes and ratios of independently computed speed medians are different statistics. FSE residue counter medians: production/candidate cycles13.184961/13.140531B, instructions46.333428/46.122148B, branches6.773951/6.773952B. The roughly0.46% instruction reduction and0.34% cycle reduction do not establish a reliable throughput gain. Reject the arithmetic candidate; artifacts: `out/fse-state-residue-20261107/`.

The second candidate makes the allocation-free `wide_sequence_bit_writer` local to `write_sequence_bitstream`, preserving its output buffer, capacity checks, flush rules, and narrow fallback. This differs from historical narrow level-3 local-writer experiments. An initial edit to the main encoder affected an inactive compile-time path: its executable was byte-identical to production (SHA-256 `f2de000b88bdbfee3f6a167c714e078c1013bf5bba859017d6b618e989ce0ae1`). Its timing variation is an identical-binary control, not a gain; saved under `out/local-wide-writer-20261107/`.

Moving the writer in the actual adaptive helper changes machine code (candidate SHA-256 `60087d74cdde76eed5e11b4fa61798b7be347f6be8c27b00e4d497f5c7d45905`). The wide workspace state accesses disappear, but counter medians do not improve: production/candidate cycles13.196515/13.243605B and instructions46.333428/46.455891B. Branches fall6.773951→6.694874B. Less workspace traffic is not sufficient when total work and scheduling also change.

Six independent rotating production/candidate/reference triples confirm no gain. Reference reaches3686.71MiB/s versus candidate3418.34 (92.72% ratio of medians; median paired reference ratio92.18%,0/6 wins). The initial2.09% old/new result did not reproduce. Reject the local-writer candidate. Both executables are127.9453125KiB. Complete throughput/memory/code measurements and identities are saved in `out/local-adaptive-writer-20261107/final/`; assembly, paired trials and counters are in its parent directory.

All78 active-candidate compression runs pass exact reference-frame comparisons and allocation guards (24 pairs-workers+6 counter-workers per candidate, plus18 confirmation workers); both confirmation decoders recover the input. The additional24 identical-binary control workers also pass. Broad unit/sanitizer qualification was not run for these rejected candidates. Production headers and published README performance numbers remain unchanged.

Next: trace the actual 1-MiB level-2 adaptive writer's per-block branch selection, emitted-bit widths and flush frequency, then compare reference flush cadence. Use that evidence before changing reservoir scheduling; do not repeat local-state or mask-expression variants merely because their source looks cheaper. The all-level performance goal remains open.

## Level-1 regression attribution and loop-placement control, 2026-11-07

The completed repeated-input audit compares the saved pre-fixed-length-writer executable with current production at 100,000 frames per run, pinned to CPU2. Three alternating counter pairs give median cycles 17.084806B versus 19.147477B (+12.07%), but instructions are 99.790088B versus 99.804788B (+0.0147%). All eight counter/profile frames match the saved reference frame (SHA-256 `8f0ecce439313ba75fb187a6cc0ddd519b240a24a15a7136dc34e7467f0b51f0`). The profiling process is complete.

Cycle profiles place 80.84% of old samples and 84.44% of current samples in the compact fast parser, rather than the changed entropy loop. Both reports have zero lost samples; throttle records have not been audited, so use these shares for localization rather than precise stage-cycle subtraction. Annotated assembly shows the same long-match loop operations shifted from address `0x144e0` to `0x144d0`. This supports investigating instruction placement, not assuming new per-sequence algorithmic work caused the regression.

An isolated GCC13 candidate adds `__attribute__((optimize("align-loops=32")))` to fast `parse()`. Initial six-pair repeated/structured controls improve by 13.95%/1.05%. However, its hottest loop begins at `0x144f0`, still only 16-byte aligned: the option changes placement within the function but does not establish that 32-byte alignment itself explains the gain. No portable alignment guarantee or general CPU conclusion follows.

Six rotating current/candidate/reference triples across six 1-MiB shapes give:

| Input | Frames/trial | Candidate change vs current C++ | Paired wins | Current C++ MiB/s | Candidate MiB/s | Reference MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| structured | 2000 | 0.85% | 4/6 | 730.78 | 732.84 | 681.51 |
| mixed | 6000 | -2.78% | 1/6 | 2578.09 | 2530.50 | 2204.28 |
| varied | 6000 | -2.67% | 0/6 | 3420.08 | 3327.57 | 3441.07 |
| zero | 10000 | -0.54% | 1/6 | 41079.09 | 40598.93 | 8586.87 |
| repeated | 10000 | 11.32% | 6/6 | 23316.10 | 25921.78 | 9809.22 |
| random | 4000 | -0.26% | 3/6 | 3827.96 | 3794.92 | 2943.41 |
All 108 timed compression workers pass exact reference-frame comparison and allocation guards; all 12 decoder checks recover the original input. The initial 24-worker pair screen also passes. Both C++ executables are 127.7890625 KiB. Per-trial memory, binary/input hashes, and all nine comparison metrics are saved in `out/parser-loop-alignment-20261107/final/`; the completed attribution audit is in `out/l1-repeat-audit-20261107/`.

The candidate is rejected: recovering an already-leading repeated workload does not justify widening the varied-input deficit. Production headers and published README measurements are unchanged. No broad malformed-input or sanitizer qualification was performed for this rejected compiler-option candidate. Next: prioritize the confirmed varied level-2 entropy deficit; retain the placement evidence when interpreting future unrelated-source regressions, and do not repeat single-sequence guards or generic alignment sweeps without a distinct hypothesis.

## README matrix measurement provenance

These notes record which measurements populate the current README tables. Keep future refresh history here, with implementation decisions and next steps in [PERFORMANCE_PLAN.md](PERFORMANCE_PLAN.md). The README presents the library and the latest published comparisons.

Measured 2026-11-06 with optimized GCC 13 against the bundled reference on five deterministic 1 MiB inputs at levels 1–22. Each cell gives **speed / peak memory / executable size**, in that order, as C++ percentages of reference. Above 100% means faster for speed; below 100% means less memory or smaller code for the other two values. Compression frames matched byte for byte, and both decoders recovered the input. Short trials are a screening comparison; near-parity rows remain timing-sensitive. Expand the absolute tables below for all nine C++ and reference metrics at every level and input. The [full benchmark report](BENCHMARKS_INPUTS.md) gives input definitions and longer confirmations.

Level-3 compression was refreshed on 2026-11-07. Levels 3 and 4 use six alternating 3,000-frame compression trials per input.

Decompression was measured on 2026-11-07 using three alternating 1,000-frame trials per row, with six longer trials for structured level 1 (3,000 frames), mixed level 3 (20,000), and structured levels 18, 20, and 21 (100,000). Measurement details and optimization tradeoffs are recorded in the [benchmark report](#bounded-short-overlap-copy-retained-2026-11-07).

Memory is whole-process peak resident set size (RSS), including the benchmark worker. On Linux, this is the current executable's `VmHWM`, excluding peaks inherited from its launcher. Code size is the complete worker executable, not just library instructions. Decompression rows use frames produced at the indicated compression level.

The main matrix covers zero-filled, repeated, structured, pseudo-random, and mixed data. Additional tables below cover varied data and shorter inputs. Results depend on the compiler, input, and level; the C++ implementation does not yet outperform reference in every case.

Six-trial compression confirmations on 2026-11-07 refresh structured levels 2, 5, and 17, mixed level 17, and random level 14. See the [confirmation report](#current-compression-gap-confirmations-2026-11-07) for iteration counts and variability. These update the measurements without a source change.

Level-1 compression was refreshed on 2026-11-07 after specializing blocks with fixed length codes. Six rotating old/current/reference trials per input verified exact frames and both decoders; see the [measurement report](#fixed-length-fse-writer-retained-2026-11-07).

## Fixed-length FSE writer retained, 2026-11-07

Fresh optimized level-1 structured profiles put 51.89% of C++ cycles in the enclosing compression/entropy function and 39.89% in its collected-literal fast parser. Reference spends 45.97% in sequence writing, 30.55% in parsing, and 14.78% in sequence-code conversion. A separate 500-frame phase trace plus warmup shows 7,014 of 7,515 blocks have both literal-length and match-length FSE tables in run-length mode; offset tables remain compressed. These fixed length states emit zero bits. This differs from the existing double-RLE shortcut for fixed literal-length and offset tables.

The GCC13 compact collected-literal path now dispatches once per block to advance only the offset FSE state when both length tables are run-length encoded. It preserves each sequence's length extra bits and offset extra bits, initial/final state handling, and the general fallback. No workspace or allocation is added. The worker grows by 192 bytes (127.60 to 127.79 KiB).

Six rotating saved-production/current/reference triples per input, pinned to CPU2, give these old/new paired changes:

| Input | Frames/trial | Median paired change vs old C++ | Old/new wins |
| --- | ---: | ---: | ---: |
| structured | 2000 | 10.94% | 6/6 |
| mixed | 6000 | 8.96% | 6/6 |
| varied | 6000 | 1.79% | 6/6 |
| zero | 10000 | -1.16% | 0/6 |
| repeated | 10000 | -11.24% | 0/6 |
| random | 4000 | 1.08% | 5/6 |

Structured and mixed improvements hold in all six pairs; the independent initial screen also won 6/6 by 10.66% and 9.12%. Repeated input is an explicit 11.24% paired regression, but remains 238.44% of reference throughput by the ratio of medians. Zero input loses 1.16% paired and remains 475.12% of reference. These costs are retained in exchange for removing the structured deficit and improving mixed throughput. A separate single-sequence guard did not recover repeated throughput (-0.71%, 3/6 versus the retained candidate) and lost structured -0.88% (2/6); it is not integrated. Varied improves relative to saved C++ but remains near, slightly below, reference. This does not achieve the all-level objective.

Current all-nine-metric results (ratios of medians, distinct from paired changes above):

| Input | C++ MiB/s | Reference MiB/s | Speed % | C++ MiB | Reference MiB | Memory % | C++ KiB | Reference KiB | Code % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| structured | 741.18 | 684.54 | 108.28% | 5.32 | 6.14 | 86.61% | 127.79 | 632.52 | 20.20% |
| mixed | 2584.80 | 2204.58 | 117.25% | 5.50 | 6.14 | 89.57% | 127.79 | 632.52 | 20.20% |
| varied | 3444.87 | 3482.34 | 98.92% | 5.23 | 5.90 | 88.52% | 127.79 | 632.52 | 20.20% |
| zero | 40606.25 | 8546.58 | 475.12% | 5.20 | 5.90 | 88.11% | 127.79 | 632.52 | 20.20% |
| repeated | 23712.82 | 9944.85 | 238.44% | 5.21 | 5.92 | 87.99% | 127.79 | 632.52 | 20.20% |
| random | 3829.86 | 2889.39 | 132.55% | 6.16 | 6.00 | 102.70% | 127.79 | 632.52 | 20.20% |

All 108 timed workers produced reference-identical frames and passed allocation guards; both production decoders recovered each of the six input shapes. Optimized GCC13 unit, compact-sequence, and all-level compression-parity tests passed (17.78 seconds combined). The new differential control covers 96 fixed-length-table cases against the ordinary-record/general writer, including varying extra-bit values within the same symbols, zero and nonzero literal lengths, varying offsets, small sequence counts, and narrow/wide bit writers. Its final strict rerun and ASan/UBSan suite pass, alongside the existing 152 parser/entropy blocks. Rebuilt neighboring level-2, level-3, and level-18 worker SHA-256s are unchanged. Other compilers retain their prior path.

README and BENCHMARKS_INPUTS now contain the five updated level-1 rows and all nine metrics; README also refreshes the varied level-1 row. Source profiles: `out/structured1-compress-profile-20261107/`. Candidate, final rotating trials, identities, tests, and reporting scripts: `out/length-rle-writer-20261107/`. Rejected guard: `out/length-rle-single-guard-20261107/`.

## Current compression gap confirmations, 2026-11-07

Before the retained writer change, rechecked six screened deficits with six alternating current/reference pairs per case. All 72 timed compression workers matched reference bytes and passed allocation guards; 12 decoder checks recovered the inputs in both implementations. Structured1 used 2,000 frames/trial, structured2 2,500, structured5 3,000, structured17 1,500, mixed17 300, and random14 500. Input/binary identities and raw data: `out/compression-gap-confirm-20261107/`.

| Input/level | Median paired C++/reference speed | C++ pair wins |
| --- | ---: | ---: |
| Structured 1, before retained change | 94.94% | 0/6 |
| Structured 2 | 101.23% | 6/6 |
| Structured 5 | 100.25% | 3/6 |
| Structured 17 | 101.21% | 5/6 |
| Mixed 17 | 113.93% | 6/6 |
| Random 14 | 115.25% | 6/6 |

These longer measurements supersede the corresponding short-screen README compression rows. Structured1's confirmation motivated the retained change above and is superseded there. Structured5 is near parity; its ratio of independent speed medians remains 99.44%, despite its paired median being slightly over 100%. Do not infer a stable win from either near-parity summary. These baseline confirmations are not source improvements.

NEXT: the remaining varied level-2 deficit is still material. Keep near-parity varied1/9/18, structured5, and high-level structured decoding under confirmation; the goal is not complete. Repeated-input overhead from the new writer dispatch is a measured follow-up opportunity, but the tested single-sequence guard is not a solution.

## Level-18 varied confirmation and child-link prefetch, 2026-11-07

Replaced the two-frame level-18 varied screening observation with six longer rounds of 30 frames, alternating both implementation order and plain/perf-stat mode order on CPU2. All 24 current/reference workers match compressed bytes and pass allocation guards. Plain medians are C++ 6.19027 and reference 6.19913 MiB/s (99.86%); median paired ratio is 99.87%, with 3/6 C++ wins. Counter-run medians are 6.21194/6.13323 MiB/s, paired ratio 100.58%, with 4/6 wins. These results establish near parity, not a repeatable C++ win or a significant deficit.

Current plain measurements, also copied to the README varied-input row:

| Level | Trials × frames | C++ MiB/s | Reference MiB/s | Speed % | C++ MiB | Reference MiB | Memory % | C++ KiB | Reference KiB | Code % |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 18 | 6 × 30 | 6.19 | 6.20 | 99.86% | 21.82 | 22.21 | 98.23% | 145.41 | 632.52 | 22.99% |

Counter medians C++/reference: cycles 22.873/22.794B, instructions 58.976/67.085B, branches 10.683/11.914B, branch misses 78.302/78.240M. C++ already retires fewer instructions while spending approximately the same cycles. Separate 30-frame cycle profiles have zero lost samples and matching frames. C++ spends 96.94% in the fused binary-tree collector; reference spends 91.47% in `ZSTD_insertBt1` plus 6.48% in all-match collection. C++ optimal-statistics update is 1.02%, so price bookkeeping is not the dominant target on this input. Artifacts, identities, scripts and all raw measurements: `out/l18-varied-confirm-20261107/`.

Tested a distinct memory-latency candidate: prefetch the current candidate node's child links immediately after calculating their masked slot, before comparing the current strings. The address stays inside the existing child table. This affects GCC13 x86-64 forward insertion without the adjacent-comparison cache; traversal order, candidate bounds, output, and allocation policy remain unchanged. Disassembly confirms `prefetcht0` at the intended insertion loop.

| Input | Frames per trial | Median paired throughput change | Candidate pair wins | Production median MiB/s | Candidate median MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| Varied, 1 MiB | 30 | -2.35% | 1/6 | 6.1323 | 5.9911 |
| Mixed, 1 MiB | 300 | -0.41% | 2/6 | 74.4793 | 72.9085 |

Each uses six alternating current/candidate pairs on CPU2. All 24 timed workers match separately generated reference frames and pass allocation guards. Percent changes are medians of pair ratios, not ratios of independent throughput medians. The prefetch is rejected before broader correctness gates; production headers were never changed. Candidate source, executables, assembly and scripts remain under `out/tree-child-prefetch-20261107/`. The README update reflects stronger baseline measurement, not this rejected candidate.

NEXT: avoid targeting an assumed level-18 instruction deficit or adding more child prefetches. Current varied levels 9 and 18 are near parity; level 2 remains the clearer compression deficit. Reassess remaining structured/mixed deficits with longer current-worker controls before choosing another architectural change, and preserve input-dependent tradeoffs rather than relying on one corpus.

## Level-9 plain/counter measurement control, 2026-11-07

The earlier varied level-9 plain confirmation reported 91.73% of reference throughput, but a subsequent counter pair and cycle profile reversed that ordering. Rechecked the same current workers and 1-MiB corpus with six rounds, alternating both implementation order and plain/counter mode order. Each worker processes 500 frames on CPU2. All 24 workers emit the same frame and pass allocation guards; identities and full results are archived in `out/l9-measurement-control-20261107/`.

| Measurement mode | C++ median MiB/s | Reference median MiB/s | Ratio of medians | Median paired ratio | C++ pair wins |
| --- | ---: | ---: | ---: | ---: | ---: |
| Plain | 180.64 | 183.18 | 98.61% | 101.01% | 4/6 |
| perf stat counters | 182.80 | 181.64 | 100.64% | 100.21% | 3/6 |

Plain ranges overlap: C++ 178.76–189.06 and reference 178.46–184.73 MiB/s. Counter-run ranges also overlap: C++ 177.26–188.38 and reference 180.35–184.05. Pair-ratio medians and ratios of independent throughput medians differ materially; neither should be substituted for the other.

Counter medians over 500 frames are C++/reference cycles 12.399/12.472B, instructions 43.265/40.143B, branches 4.923/5.547B, and branch misses 14.449/26.745M. Thus C++ retires about 7.78% more instructions but does not show a cycle penalty in this run. The earlier roughly 8% throughput gap is not reproducible under these controls. This is measurement variability, not a source speedup or proof of an enduring C++ win. Preserve the dated earlier results; do not target a supposed 8% row-matcher cycle deficit without renewed evidence. README performance values are unchanged because no code improvement occurred.

## Grouped Huffman validation rejected, 2026-11-07

Audited reference BMI2 dispatch and the existing experiment history before changing code. The previous narrow writer dispatch, whole-writer cloning, and target-attributed wrapper trials already produced real BMI2 instructions without reliable gains; this turn did not repeat them.

A distinct isolated trial loads all four Huffman codes in the current four-symbol loop, combines their width validity predicates, then emits the codes in the existing order. The scalar tail, code validation, output layout, flush schedule, and workspace stay unchanged. Unlike the rejected once-per-table validation policy, this adds no table scan and still checks every used symbol. GCC lowers the combined condition to two validation branches, not the single branch hoped for, and spills the carried bit container to stack at `0x8(%rsp)`.

| Input | Median paired throughput change | Candidate pair wins | Production median MiB/s | Candidate median MiB/s |
| --- | ---: | ---: | ---: | ---: |
| Varied, 1 MiB | -3.07% | 1/6 | 3466.45 | 3371.32 |
| Mixed, 1 MiB | -0.57% | 2/6 | 2957.10 | 2916.67 |

Six alternating CPU2 optimized GCC13 level-2 pairs per input, each 10,000 frames, pass exact reference-frame and allocation guards (24 workers). Changes are median pair ratios. Rejected before broader invalid-code/sanitizer qualification; no production change or README refresh. Artifacts: `out/huffman-group-validation-20261107/`, including scripts, isolated source, disassembly, reference frames and raw measurements. Avoid further validation batching based only on fewer source-level branches; the code lifetime and generated memory traffic matter.

NEXT: prioritize gaps that survive repeated unprofiled controls. Level-2 varied remains a demonstrated deficit, whereas level-9 is now near parity. The varied level-18 screen used only two frames per worker and still needs a longer confirmation before selecting a high-level parser target.

## Level-2 instruction sampling and encoder mask lookup, 2026-11-07

Collected matched optimized GCC13 level-2 instruction profiles on 10,000 varied 1-MiB frames. Initial period-100,003 captures reported zero lost samples but contained PERF_RECORD_THROTTLE events: sampled periods represented only 20.95B C++ and 15.41B reference instructions, versus approximately 46.33B and 39.68B from counters. Zero lost samples alone is not sufficient evidence of complete sampling coverage.

Repeated at period 1,000,003. Both captures have zero lost samples and zero throttle records; their sampled-period totals are 46,333,138,999 and 39,684,119,052, consistent with earlier counter runs. Every profiled output matches reference. Use these lower-rate captures for instruction attribution:

| Function or scope | C++ instruction sample share | Reference instruction sample share |
| --- | ---: | ---: |
| Fast parser | 49.20% | 58.15% |
| Broad C++ sequence encoder / reference sequence bit writer | 27.35% | 17.52% |
| Reference sequence-code conversion (separate function) | Included above | 4.07% |
| Huffman stream writer | 6.76% | 4.03% |
| Huffman tree construction | 3.47% | 5.40% |

Scopes differ: C++ gathers literals in its encoder, while reference stores literals during parsing; the broad encoder includes several inlined operations. These percentages therefore do not establish matched per-stage speed ratios. Scaling shares by their separate instruction totals suggests approximately 22.80B/23.08B parser instructions and 3.13B/1.60B Huffman stream instructions (C++/reference), useful for prioritization rather than cycle attribution. Sample skid also prevents identifying one stack store as the cause of a hotspot. Captures, annotations, metadata audit, executable identities, and scripts: `out/l2-instruction-profile-20261107/`, especially `low-rate/`.

The FSE encoder annotation shows repeated variable-mask construction during grouped state emission. An isolated candidate replaces that mask expression in `encode_bits` with a 33-entry constexpr uint32 mask table, leaving RLE validation, state transitions, and bit-writer policy unchanged. Current built FSE tables cap tableLog at 9, so valid emission widths fit the lookup. Disassembly confirms indexed mask-table use. This is an encoder trial, distinct from the rejected decoder state-mask lookup.

| Input | Median paired throughput change | Candidate pair wins | Production median MiB/s | Candidate median MiB/s |
| --- | ---: | ---: | ---: | ---: |
| Varied, 1 MiB | -0.47% | 3/6 | 3451.42 | 3406.56 |
| Mixed, 1 MiB | -0.08% | 3/6 | 2940.58 | 2952.30 |

Six alternating CPU-2-pinned 10,000-frame pairs per input pass exact reference frames and worker allocation guards (24 timed workers). Percentage changes are medians of pair ratios, not ratios of the displayed medians. Neither input demonstrates a repeatable gain. Rejected before broader correctness gates; production and README metrics are unchanged. Artifacts: `out/fse-encoder-masks-20261107/`.

NEXT: use the unthrottled profiles for the broader entropy-dataflow investigation. Audit reference BMI2 dispatch and C++ writer code generation against the previously tested native-CPU and Huffman writer variants before selecting another change; do not repeat mask substitutions, rejected loop duplication, or source-level state scheduling solely because a sampled instruction appears hot.

## Fast-parser whole-record copy rejected, 2026-11-07

Disassembly of the production level-2 fast parser and the preceding direct-offBase candidate identifies packed-field construction work at the normal vector append. Production around `0x10624` masks match length, loads the destination packed word, preserves its upper two bits, stores the merged word, and then stores the byte containing the repeat code. The rejected direct-offset candidate still performs this read/modify/write work around `0x10567`, even though its repeat code is zero. This is direct assembly evidence, not proof that these stores dominate total runtime.

An isolated candidate keeps production's logical offsets, 12-byte `parsed_sequence`, bitfields, and encoder unchanged. Only fast-parser append changes: construct the sequence value, default-emplace its destination, then `memcpy` the complete trivially copyable record. Other block record types retain ordinary push_back. This tests complete-record transfer without enlarging storage or repeating the previously rejected 16-byte ordinary-field layout.

GCC13 does emit full-width destination copies, but it also retains zero initialization and stack temporary stores/loads. The candidate normal append around `0x106e5–0x10742` materializes the record on the stack, zeros the destination with eight- and four-byte stores, then copies eight and four bytes from that temporary. Thus the source-level memcpy does not produce the intended minimal record-store sequence.

| Input | Median paired throughput change | Candidate pair wins | Production median MiB/s | Candidate median MiB/s |
| --- | ---: | ---: | ---: | ---: |
| Varied, 1 MiB | -1.98% | 2/6 | 3470.60 | 3389.41 |
| Mixed, 1 MiB | -5.47% | 0/6 | 3004.32 | 2827.56 |

Both cases use six alternating CPU-2-pinned optimized GCC13 pairs of 10,000 frames. All 24 workers pass byte-for-byte comparison against independently generated reference frames and worker allocation guards. Percent changes are medians of pair ratios. The candidate is rejected before broader parser-history/sanitizer gates; production headers and README metrics are unchanged. Scripts, isolated headers, binaries, disassembly, and all measurements are in `out/sequence-record-copy-20261107/`; comparative original/direct-offset disassembly is in `out/fast-offbase-20261107/`.

NEXT: avoid another emplace/copy spelling or the already tested larger plain record. Any proposed sequence-storage redesign must demonstrate fewer actual stores and loads in optimized assembly and preserve the measured mixed control. The main level-2 entropy cost still warrants a matched instruction profile to distinguish code gathering, FSE table work, and the reverse writer before selecting another optimization.

## Fast level-2 direct offBase trial rejected, 2026-11-07

Reference fast parsing stores encoded offBase in its sequence records. C++ level-2 fast parsing instead stores a logical offset and repeat code, then reconstructs offBase in both code gathering and reverse sequence writing. An isolated candidate added an explicit parser policy and selected the existing DirectOffBase encoder policy only for GCC13 level 2 with a pledged 1 MiB source. All three fast-parser sequence emission sites were adapted, including initial RLE and immediate repeat matches; record size, allocation policy, and bit-writer selection stayed unchanged. This is distinct from the previously rejected 256-KiB double-fast trial.

Six alternating CPU-2-pinned, optimized GCC13 old/candidate pairs of 10,000 frames per input measured:

| Input | Median paired throughput change | Candidate pair wins | Old median MiB/s | Candidate median MiB/s |
| --- | ---: | ---: | ---: | ---: |
| Varied, 1 MiB | -1.13% | 2/6 | 3423.53 | 3384.40 |
| Mixed, 1 MiB | -0.30% | 3/6 | 2998.76 | 2953.19 |

The paired percentage is the median of individual pair ratios, not the ratio of the two throughput medians. All 24 timed workers matched separately generated reference frames and passed worker allocation guards. Three further alternating 10,000-frame varied counter pairs also matched reference:

| Counter median | Production | Candidate |
| --- | ---: | ---: |
| User cycles | 13,233,188,238 | 13,417,232,380 |
| Instructions | 46,333,428,909 | 46,822,687,315 |
| Branches | 6,773,951,833 | 6,703,254,248 |
| Branch misses | 5,802,976 | 5,850,767 |

Despite fewer branches, retired instructions rise 1.06% and cycles rise 1.39%. Moving conversion to the parser does not establish an overall work reduction. The candidate remains isolated under `out/fast-offbase-20261107/` with preparation/build/run scripts, headers, executables, reference frames, all trial CSVs, and counters. Production headers were never changed. Rejected before full parser-history/malformed-input qualification; existing README measurements remain authoritative.

NEXT: inspect the candidate and production parser/encoder assembly before proposing another representation change. In particular, distinguish actual conversion savings from stores and masks caused by retaining the packed match-length/repeat-code field. Do not assume that the retained double-fast direct-offset benefit transfers to fast parsing, or repeat the rejected offset-RLE loop specialization.

## Level-2 1-MiB profile and Huffman-description reuse audit, 2026-11-07

The generic GCC13 optimized 1-MiB level-2 worker resolves to `fast_match_state<20,16,6,0>`, unlike the 256-KiB collected-literal double-fast path. Three alternating, CPU-2-pinned 10,000-frame counter pairs on the varied corpus measured these medians:

| Counter | C++ | Reference |
| --- | ---: | ---: |
| User cycles | 13,307,382,185 | 12,254,917,235 |
| Instructions | 46,333,428,003 | 39,684,406,097 |
| Branches | 6,773,951,186 | 5,156,446,955 |
| Branch misses | 5,909,903 | 31,815,900 |

The ratio of median cycle counts is 108.59%. Fewer branch misses do not offset the C++ execution cost. Separate cycle profiles lost zero samples: C++ fast parsing accounts for 50.49%, the broad sequence encoder 20.54%, Huffman literal encoding 6.70%, tree construction 4.91%, and Huffman stream writing 4.77%. Reference fast parsing accounts for 57.54%, sequence writing 13.77%, tree construction 6.89%, and sequence-code conversion 5.37%. These function boundaries differ, so percentages must not be interpreted as matched stage timings.

The C++ phase trace over 1,000 frames plus warmup reports 142.44 ms parsing, 149.32 ms entropy encoding, 58.08 ms literal encoding, and 14.43 ms Huffman weight work. It builds 7,008 weight FSE tables and reuses 1,000. These instrumented nested timings are diagnostic, not production throughput measurements. All nine counter/profile/trace frames were verified byte-identical (SHA-256 `94ac0296385fa966b5b7579854fe80c64792fb514b98e7c0814beee7366bda85`). Artifacts: `out/l2-1m-profile-20261107/`.

An isolated header copy then logged exact Huffman serializer keys (explicit weight count and every explicit weight). The first 128 descriptions contain eight distinct keys in a repeating eight-block order. Simulated LRU capacities 1, 2, and 4 yield zero hits; capacity 8 yields 120/128 hits, entirely from subsequent identical frames. Its output matches reference. This is a reuse diagnostic, not a speed experiment: no cache or production change was retained. Artifacts and reproduction script: `out/weight-description-reuse-20261107/`.

A serialized-description cache therefore has no demonstrated within-frame benefit on this target. Any larger cross-frame cache must first show value on changing-frame workloads and account for key comparison, copying, and workspace cost; repeated identical benchmark frames alone are insufficient. Next inspect sequence-encoding instruction/dataflow costs against reference before adding state. Existing early-repeat selection and Huffman writer experiments remain rejected unless new evidence distinguishes a proposed change. Published throughput tables are unchanged.

## Current all-level varied compression screen and confirmations, 2026-11-07

Rebuilt the optimized GCC13 evaluation workers from the current checkout (`Release`, `-O3 -DNDEBUG`, pledged size 1 MiB), then compared all 22 levels on the existing varied corpus, pinned to CPU2. Each level used one calibration frame per implementation followed by three alternating pairs with equal iteration counts, targeting roughly 350 ms per timed worker. The three lowest screen ratios (levels2/9/8) then received six longer pairs targeting roughly two seconds per worker. Slow high-level screen rows contain very few frames and must not be treated as confirmed wins.

All **212 compression workers** emitted byte-identical reference frames and passed allocation guards; **44 decoder checks** recovered the corpus in both directions at every level. The table uses the longer confirmations where available and screening results elsewhere. Speeds use median elapsed time, RSS uses median current-process peak memory, and code size is the complete executable. This is a new measurement checkpoint, not a source optimization. Existing README five-pattern measurements are unchanged.

| Level | Trials × frames | C++ MiB/s | Reference MiB/s | Speed % | C++ MiB | Reference MiB | Memory % | C++ KiB | Reference KiB | Code % |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 3 × 1164 | 3197.90 | 3193.74 | 100.13% | 5.22 | 5.95 | 87.73% | 127.60 | 632.52 | 20.17% |
| 2 | 6 × 6743 | 3387.19 | 3739.03 | 90.59% | 5.34 | 6.15 | 86.82% | 127.95 | 632.52 | 20.23% |
| 3 | 3 × 827 | 2606.64 | 2533.02 | 102.91% | 6.02 | 6.64 | 90.64% | 127.74 | 632.52 | 20.20% |
| 4 | 3 × 485 | 2712.80 | 2489.31 | 108.98% | 7.58 | 7.86 | 96.52% | 127.74 | 632.52 | 20.20% |
| 5 | 3 × 62 | 319.12 | 238.12 | 134.02% | 7.58 | 8.41 | 90.15% | 127.63 | 632.52 | 20.18% |
| 6 | 3 × 83 | 320.46 | 307.06 | 104.37% | 7.57 | 8.38 | 90.39% | 119.20 | 632.52 | 18.85% |
| 7 | 3 × 72 | 249.23 | 248.32 | 100.37% | 10.10 | 10.89 | 92.79% | 119.20 | 632.52 | 18.85% |
| 8 | 6 × 390 | 192.79 | 194.91 | 98.91% | 10.07 | 10.89 | 92.50% | 119.20 | 632.52 | 18.85% |
| 9 | 6 × 342 | 164.99 | 179.87 | 91.73% | 15.08 | 15.86 | 95.12% | 119.20 | 632.52 | 18.85% |
| 10 | 3 × 44 | 136.30 | 126.40 | 107.83% | 15.10 | 15.91 | 94.87% | 119.21 | 632.52 | 18.85% |
| 11 | 3 × 25 | 96.55 | 80.89 | 119.36% | 15.10 | 15.85 | 95.24% | 119.21 | 632.52 | 18.85% |
| 12 | 3 × 29 | 99.79 | 82.06 | 121.61% | 15.10 | 15.85 | 95.24% | 119.21 | 632.52 | 18.85% |
| 13 | 3 × 39 | 154.63 | 114.28 | 135.30% | 21.10 | 21.86 | 96.52% | 123.23 | 632.52 | 19.48% |
| 14 | 3 × 24 | 103.29 | 72.46 | 142.55% | 21.07 | 21.88 | 96.32% | 123.23 | 632.52 | 19.48% |
| 15 | 3 × 17 | 61.47 | 46.00 | 133.63% | 21.09 | 21.85 | 96.55% | 123.23 | 632.52 | 19.48% |
| 16 | 3 × 3 | 9.48 | 8.45 | 112.22% | 21.32 | 21.75 | 98.04% | 149.02 | 632.52 | 23.56% |
| 17 | 3 × 4 | 9.51 | 8.55 | 111.26% | 21.29 | 21.81 | 97.60% | 149.02 | 632.52 | 23.56% |
| 18 | 3 × 2 | 5.96 | 6.05 | 98.48% | 21.78 | 22.32 | 97.60% | 145.41 | 632.52 | 22.99% |
| 19 | 3 × 1 | 2.95 | 1.83 | 160.76% | 21.81 | 22.32 | 97.72% | 145.41 | 632.52 | 22.99% |
| 20 | 3 × 1 | 3.03 | 1.83 | 165.63% | 21.80 | 22.25 | 97.98% | 145.41 | 632.52 | 22.99% |
| 21 | 3 × 1 | 2.94 | 1.81 | 162.39% | 21.78 | 22.29 | 97.70% | 145.41 | 632.52 | 22.99% |
| 22 | 3 × 1 | 2.96 | 1.82 | 162.67% | 21.78 | 22.23 | 98.00% | 145.41 | 632.52 | 22.99% |

The longer ratios of median speeds confirm level2 at **90.59%**, level9 at **91.73%**, and level8 at **98.91%** of reference. Level18's 98.48% screen also remains an open confirmation target; it used only two frames per worker. Near-parity levels1/7 are not established wins either.

Matched 1,000-frame level9 profiling produced exact frames with zero lost samples: C++ `find_best_match` accounts for88.58% of cycle samples, reference row matching85.47%. A separate counter pair measured C++/reference25.664/25.597B cycles,86.443/80.205B instructions,9.835/11.082B branches, and28.923/53.813M branch misses. Thus this pair has nearly equal cycle costs despite more C++ instructions and fewer branches/misses. The profiled worker times also reverse the unprofiled confirmation ordering (C++6.987s, reference7.338s). Do not equate the confirmed unprofiled throughput deficit with an8% excess in algorithmic cycle cost or infer a particular row-matcher instruction is responsible.

Next prioritize the larger current **1-MiB level2** deficit, which is a different specialization from the repeatedly tested256-KiB collected-literal path. Inspect resolved parser/writer policies and obtain matched phase/counter evidence before reusing rejected short-input experiments. Keep varied levels8/9 and slow-screen18 on the work list; level9 needs repeated counter/unprofiled agreement before another row-layout microvariant.

Corpus path: `out/zen3-sixlevel-20261025/pair-l9-varied/input.bin`; SHA-256 `34626ecb899a906ec37ad04087a6261c2471ccbf66fd461a7a5f8ae3914d0a07`. Reproduction scripts, iteration counts, all raw CSVs/frames, binary identities, summary, profiles and counters are under `out/varied-all-levels-20261107/`.

## Combined histogram metadata scans rejected, 2026-11-07

Tested three isolated alternatives to `select_sequence_table`'s maximum-symbol scan followed by `max_element`: (1) accumulate both results in one loop, (2) express the highest nonzero symbol as an explicit maximum-index reduction, and (3) implement the combined scan with SSE2. These do not change histogram collection, table-selection rules, normalized counts, or allocation. Fresh optimized GCC13 level-2/256-KiB baselines were used with six alternating CPU2 20,000-frame pairs per input and variant.

| Variant | Varied median paired change | Wins | Mixed median paired change | Wins |
| --- | ---: | ---: | ---: | ---: |
| Combined scalar scan | +0.14% | 4/6 | -0.61% | 3/6 |
| Explicit maximum-index reduction | -0.22% | 2/6 | -3.97% | 1/6 |
| Explicit SSE2 scan | +0.40% | 4/6 | -0.06% | 2/6 |

All 72 timed workers matched optimized reference compressed bytes and passed allocation guards. GCC's vectorization reports reject both scalar forms with an unsupported-use diagnostic; the maximum-index rewrite did not deliver automatic SIMD. The explicit SSE2 version maintains four maximum-count lanes and four last-nonzero-index lanes, then reduces those lanes and handles the alphabet tail. Unsigned comparison uses sign-bit bias, preserving full uint32 histogram values. An independent scalar oracle passed 40,000 ASan/UBSan cases over alphabet sizes32/36/53/256, including zero/tied, sparse, and full-width random counts.

None demonstrates a reliable end-to-end improvement worth retaining. Production, README measurements, and normal build artifacts remain unchanged; the isolated candidates did not advance to all-level acceptance or broader performance qualification. Artifacts: `out/histogram-metadata-20261107/`, `out/histogram-metadata-vector-20261107/`, and `out/histogram-metadata-sse2-20261107/` contain source snapshots/generators, compiler reports, oracle and raw trials.

Next: refresh the all-level compression comparison on the existing 1-MiB varied corpus, then confirm the largest remaining deficits with longer runs. Earlier varied results exposed lazy-parser gaps beyond the short level-2 case; current five-pattern README tables and old varied checkpoints should not be treated as proof that those paths now beat reference. Prioritize a repeated substantial deficit over further metadata-loop microvariants.

## Grouped FSE state-mask lookup rejected, 2026-11-07

An isolated GCC13 medium-block decoder replaced only the two shift/subtract masks used to split the grouped FSE state value with a ten-entry constexpr mask table. The reverse bit reader, reloads, checks, table representation, and allocation behavior were unchanged. Source audit confirms sequence tables are validated at maximum logs 9 (literal/match) and 8 (offset); generated state widths cannot exceed those logs, and RLE uses zero. Thus the proposed mask indices are 0–9. Disassembly verifies two indexed memory-AND instructions against the new table, so the source change reached the hot code.

All 110 reference-frame checks and all 48 timed workers passed exact-output and allocation guards. Six alternating CPU2 current/candidate pairs gave:

| Input / frame level | Frames per trial | Median paired throughput change | Candidate wins |
| --- | ---: | ---: | ---: |
| Structured / 18 | 30,000 | -2.22% | 1/6 |
| Structured / 20 | 30,000 | +0.05% | 3/6 |
| Mixed / 3 | 20,000 | +0.68% | 3/6 |
| Structured / 1 | 3,000 | -2.61% | 0/6 |

The remaining target gap worsens and the retained level-1 gain regresses, so this candidate is rejected without further malformed-input qualification. Production code, binaries, and README metrics remain unchanged. Artifacts and annotated instruction locations: `out/state-mask-20261107/` (the memory-AND pair is at candidate addresses `a411`/`a414`). Do not infer that fewer source expressions or replacing a variable shift with a lookup makes the loop faster.

Next rotate back to compression: audit a combined maximum-symbol/most-frequent scan in `select_sequence_table`. The current code scans a bounded histogram for the last nonzero symbol and then scans its prefix again with `max_element`. This proposal concerns only already-built histogram metadata, not the previously rejected per-sequence histogram fusion or offset-code rescan removal. Preserve table-selection ties and compressed bytes; inspect compiler vectorization and benchmark level-2 varied/mixed controls before retaining anything.

## Fixed four-sequence decode batch rejected, 2026-11-07

Implemented an isolated GCC13 medium-block decoder that decodes four sequence records into a fixed stack array, then executes their existing validated copies. The prototype preserves FSE/repeat-offset order and final-sequence handling; length validation uses a pending logical output size while offset validation remains in copy execution. Records retain literal pointers and three lengths/offsets, requiring 128 bytes of stack scratch on this target and no heap allocation. Other block paths retain the ordinary loop.

All 110 current reference frames pass exact decoded-output and worker allocation checks. Six alternating current/candidate pairs per control yield:

| Input / frame level | Frames per trial | Median paired throughput change | Candidate wins |
| --- | ---: | ---: | ---: |
| Structured / 18 | 30,000 | -15.79% | 0/6 |
| Structured / 20 | 30,000 | -16.09% | 0/6 |
| Mixed / 3 | 20,000 | -0.18% | 2/6 |
| Structured / 1 | 3,000 | -3.71% | 0/6 |

All 48 timed workers passed exact-output/allocation guards. One additional matched 100,000-frame structured18 counter pair measured current/candidate 24.975/28.259 billion cycles, 112.040/127.530 billion instructions, 17.575/18.847 billion branches, and 10.965/32.380 million branch misses. Instructions increased about 13.8%, cycles about 13.1%, with nearly unchanged IPC; this is added work, not evidence of a latency-only regression. A single pair cannot apportion costs among scratch writes, loop control, validation, and code generation.

The prototype is rejected before full malformed-input/sanitizer qualification. Valid-frame checks do not establish malformed-input correctness. No production headers, binaries, or README metrics changed. Artifacts: `out/decode-batch4-20261107/` contains source generation, isolated headers, all valid-frame checks, controls and counters. Do not repeat the same batch with arbitrary batch-size tuning without evidence that the extra work can be removed.

Next bounded idea: audit the two variable mask constructions used to split grouped FSE states. A small constexpr mask table for validated state widths could replace shift/subtract dependencies without sequence scratch or extra allocations. This is distinct from the rejected reverse-reader mask change: keep the bit reader and its bounds/reload behavior untouched. Verify the table-width invariant, inspect generated instructions, and test medium-block controls before any retention; an extra table load can still be slower.

## Longer structured decoder confirmations, 2026-11-07

After retaining bounded short-overlap copying, six alternating CPU2 current/reference pairs of 100,000 frames each confirmed the three remaining short-screen deficits. All 36 workers passed exact output and allocation guards. No library change was made in this follow-up.

| Structured frame level | C++ MiB/s | Reference MiB/s | Median paired throughput | C++ wins |
| ---: | ---: | ---: | ---: | ---: |
| 18 | 19132.04 | 19414.80 | 98.33% | 1/6 |
| 20 | 19158.82 | 19357.15 | 98.55% | 2/6 |
| 21 | 19100.61 | 19114.55 | 99.66% | 2/6 |

Speeds use median elapsed time; paired percentages use per-pair ratios and therefore differ from the README's ratio of median speeds. Replaced only these three README/detailed-matrix decoder rows, including measured RSS and executable sizes, and corrected their trial-count note. This is stronger measurement evidence, not a new optimization.

The current four-artifact identity audit (C++ binary, reference binary, encoded frame, expected output) groups all 110 cases into 30 distinct workloads. Structured19–22 are one identical workload; structured18 is separate. Thus level20/21 results are repeated observations of the same algorithm and data, not independent level-specific bottlenecks. Executable paths and runtime placement can still affect timing, so their measurements remain separately reported.

Three additional alternating 100,000-frame level18 hardware-counter pairs all show higher C++ cycles; median paired cycle cost is 100.71% of reference. Median C++/reference totals: 23.280/23.116 billion cycles, 112.040/95.574 billion instructions, 17.575/13.647 billion branches, and 10.572/5.237 million branch misses. The instruction excess is much larger than the cycle excess; avoid treating every extra instruction as an equivalent latency penalty.

Next architectural experiment: audit and prototype a bounded stack batch of sequence metadata, separating entropy decoding from existing validated copy execution. Start with a small fixed batch, no heap allocation, unchanged repeat-offset/FSE order, and exact final-sequence handling. This differs from the rejected mere movement of FSE updates before each copy: the intended separation must actually reduce live decoder state during copying. Extra scratch writes and bounds bookkeeping may outweigh any gain; test structured18 and the shared structured19–22 workload, plus mixed3 and the retained structured1 improvement. Preserve malformed/truncated-frame and output-capacity checks. Do not repeat whole-decoder BMI2, standalone register-mask, or broad tiny-copy changes already rejected.

Artifacts: `out/structured-high-confirm-20261107/` contains raw timings/counters, all four identity hashes per group, current summary, and report-update script. Remaining compression and decoder gaps keep the goal open.

## Bounded short-overlap copy retained, 2026-11-07

The earlier broad overlap-seed helper extension regressed mixed input. This narrower GCC13 change applies only to the non-medium decoder after nonoverlapping matches have already returned: for overlapping matches of at most eight bytes and offsets at least four, perform two ordered exact four-byte copies. Overlap guarantees `length > offset >= 4`, so the first four bytes are available, the second copy can consume bytes written by the first, and neither copy crosses the requested match end. Offsets below four, longer matches, medium blocks, other compilers, and all validation remain on their existing paths.

An independent bytewise-expansion oracle passed ASan/UBSan for all ten supported length/offset pairs, 64 alignments, and 64 data seeds (40,960 cases) using exact-end allocations. Added a permanent structured level-1 direct-decoder test for an undersized destination, exact capacity, guards, and reuse after error. Optimized GCC13 unit tests and all-level compression/interoperability acceptance passed in 17.89 seconds. The integrated decoder exactly matches the measured candidate binary; all 22 rebuilt decoder workers are byte-identical.

Six alternating old/new pairs per case measured:

| Input / compression level | Frames per trial | Median paired throughput change | Candidate wins |
| --- | ---: | ---: | ---: |
| Structured / 1 | 3,000 | +27.42% | 6/6 |
| Mixed / 3 | 20,000 | -2.70% | 1/6 |
| Structured / 18 | 30,000 | +2.13% | 5/6 |

The mixed regression is an accepted, explicit tradeoff for closing the structured level-1 deficit. Independent six-trial current/reference confirmations measured structured1 **1592.38/1273.70 MiB/s (125.02%)**, mixed3 **9857.15/7372.57 (133.70%)**, and structured18 **19248.87/19619.18 (98.11%)**. Thus mixed retains a substantial reference lead, while structured18 remains unresolved. Current C++ worker size is **126.34 KiB**, versus reference **211.24 KiB**; memory and every comparison ratio are in the README and detailed matrix.

Refreshed all 110 GCC decoder cases using three alternating 1,000-frame trials per case, then the three longer confirmations above: 696 current/reference worker runs, all exact-output and allocation-guard checks passed. The README's compression, short-input compression, and native Windows measurements are preserved. Broad short-screen improvements are not all attributed to this source change; only the controlled old/new comparisons establish its measured effect. Short-screen deficits also remain at structured20/21 and require longer confirmation.

Artifacts: `out/short-overlap4-20261107/` contains the candidate and previous header, oracle, source-generation and benchmark scripts, build/test logs, all raw trials, the 110-row summary, worker hash, and report renderer. Next: confirm current structured20/21 with longer runs, then investigate the remaining structured18 sequence-decoding cost without sacrificing mixed3's reference lead. Compression gaps remain on the overall task list.

## Level-1 structured decoder profile and overlap-seed control, 2026-11-07

Six alternating CPU2 comparisons of the current optimized GCC13 decoders on the 1-MiB structured level-1 reference frame (3,000 frames each) confirm a small deficit: C++/reference median speeds 1127.55/1154.24 MiB/s; median paired throughput 98.54%, with C++ winning 2/6. These are different aggregations; the ratio of medians is not the median paired ratio. Exact decoded-output checks passed throughout.

One matched 10,000-frame counter pair measured C++/reference 37.889/37.039 billion cycles, 177.028/166.118 billion instructions, 37.277/24.391 billion branches, and 10.272/0.977 million branch misses. Separate zero-loss cycle profiles place 74.17% of C++ self samples in the padded non-medium sequence loop, 9.91% in the padded medium loop, 12.82% in libc copying, and 1.22% in the tiny-copy helper. Reference samples were 98.18% in sequence decoding and 1.43% in FSE table construction. Different function boundaries and inlining prevent direct matched-phase attribution.

A separate untimed diagnostic identifies one medium block with 16,128 sequences, followed by fourteen non-medium blocks with 8,192 sequences each. Every match is 8–15 bytes; half of the later blocks' matches overlap. Later blocks have no sequence literals, and their first eight traced sequences alternate eight-byte matches at offsets 4,088 and 7. The first block has 256 short-literal sequences and 15,872 zero-literal sequences. These observations identify short overlap copying as a concrete lead rather than a generic allocation hypothesis.

An isolated candidate extended the existing exact 1–7-byte overlap-seed helper from medium blocks to all block paths. Six alternating current/candidate pairs per case produced:

| Input / compression level | Frames per trial | Median paired change | Candidate wins |
| --- | ---: | ---: | ---: |
| Structured / 1 | 3,000 | +8.33% | 6/6 |
| Mixed / 3 | 20,000 | -6.67% | 0/6 |
| Structured / 18 | 30,000 | +3.56% | 5/6 |

All 36 workers passed exact-output and allocation guards. The mixed regression rejects the broad extension; production and README measurements remain unchanged. The candidate and trace headers are isolated under `out/l1-structured-decode-20261107/`, alongside scripts, profiles, counts, binary/frame hashes, and raw metrics.

Next: audit and test a narrower exact-copy algorithm for overlapping 4–8-byte matches at offsets at least four, using two ordered four-byte copies with the second ending at the match boundary. Prove overlap and bounds behavior with a bytewise oracle before benchmarking; retain mixed3 and structured18 controls. Do not adopt a special case solely because it wins this structured corpus, and do not infer that all libc samples arise from the observed offset-7 matches.

## Reference-style double-fast catch-up guard rejected, 2026-11-07

The branch-profile follow-up first audited earlier safe-address, branch-hint, repeat-guard, long-match-loop, PGO, and in-record offset-base experiments; those already failed controls. A distinct isolated trial changed backward match extension from two short-circuit boundary checks to the reference's bitwise combination of the two Boolean checks, still short-circuiting before reading either byte. Match insertion and repeat-offset behavior were unchanged.

Built baseline/candidate with GCC13 `-O3 -g -DNDEBUG`, level 2 and pledged size 256 KiB. Six alternating CPU2 pairs of 20,000 frames per input all matched optimized reference bytes and passed the worker allocation guards. Varied median paired throughput change was **-0.23% (3/6 wins)**; genuine mixed was **+0.17% (3/6 wins)**. There is no reliable speedup, so the candidate remains isolated. The optimized `.text` did change (baseline `2ac9abe466022b3c694e30d75afb44df92a6f236375fd176e6aadbfa3bad24a6`, candidate `f8a58b20f26b4460fe78a582269059976a633948b2d8020afea3e20af4933f57`); this was not a source-only no-op. No production changes or README measurement refresh.

Artifacts: `out/l2-catchup-20261107/` contains the source overlay, build/run scripts, 24 timed worker outputs, reference frames, raw metrics, and candidate disassembly. The repeated level-2 branch-control microvariants have not established a gain. Next rotate to the README's level-1 structured decompression deficit: confirm it with longer alternating current/reference runs before selecting a decoder change. Keep level-2's remaining gap open rather than relabeling neutral trials as success.

## Current level-2 varied-input cycle and branch profiles, 2026-11-07

Rebuilt the current level-2/256-KiB C++ worker with GCC13 `-O3 -g -DNDEBUG`; compared it with the optimized reference worker on the same varied input, pinned to CPU2. Three alternating 40,000-frame counter pairs confirm a remaining deficit:

| Metric (whole-process counters, median) | C++ | Reference |
| --- | ---: | ---: |
| User cycles (billions) | 13.044 | 12.490 |
| User instructions (billions) | 47.115 | 47.138 |
| User branches (billions) | 5.932 | 5.692 |
| User branch misses (millions) | 21.713 | 5.534 |

Worker timings were 3.097/2.899/2.909 seconds for C++ and 2.823/2.751/2.794 for reference. The median paired cycle excess is 4.81%; instruction counts are almost equal. These are counter-enabled diagnostic timings, not replacement README benchmark rows. All counter/profile frames matched reference bytes, and both optimized decoders recovered the original input.

Separate cycle profiles had zero lost samples. C++ self samples: collecting double-fast parser 38.73%, separately outlined match counter 7.50%, enclosing compression function 20.08%, Huffman literal selector 6.61%, Huffman stream writer 5.04%, Huffman tree builder 4.19%. Reference: double-fast parser 50.87%, sequence writer 12.66%, block splitting 6.78%, Huffman tree builder 6.33%, sequence-to-code conversion 5.53%. Different inlining/function boundaries prevent treating these as matched phase times.

A separate 3,000-frame instrumented run measured parse 100.01 ms and encode 132.14 ms; nested encoding buckets included gather 16.60 ms, literals 44.97 ms, tables 40.44 ms, and bits 29.49 ms. Histogram/max-symbol work was 27.52 ms, dominated by offset-table selection. Offset codes selected RLE in 3,001 of 6,002 blocks; LL and ML selected compressed tables throughout. Instrumentation perturbs timing, and these clocks overlap; do not add all buckets or compare their absolutes directly to uninstrumented profiles. Prior RLE-prescan and bounded-histogram experiments must be audited before proposing another scan.

Matched branch-miss profiles isolate a stronger next lead: 59.81% of C++ misses sampled in the collecting parser and 13.26% in match counting, versus 11.51% in the reference's broader parser. Reference misses instead concentrate in block splitting (34.05%) and Huffman tree construction (22.34%). Source/assembly annotation has clusters around candidate handling and post-match hash updates, including nonbranch instructions; sampling skid means these are regions to investigate, not proof of a particular bad branch. Both implementations already perform the same post-match insertion positions (+2, end-2 long, end-1 short), so deleting those updates is not justified.

Next: audit the collecting parser's candidate/repeat/catch-up and match-count control flow against reference, then test a scoped reduction in branches or dependent work with counter evidence and mixed/random/repeated controls. Preserve exact insertion order and bounds; do not repeat pointer-only, tag-removal, or Huffman batching variants already rejected. Raw profiles, annotated assembly, trace, counters, binary/input hashes and reciprocal checks: `out/l2-current-profile-20261107/`. No production changes or new speedup claimed.

## Aligned sequence-code slab diagnostic rejected, 2026-11-07

Following the tag-placement audit, an isolated GCC 13 level-2/256-KiB prototype replaced three LL/OF/ML byte vectors with one 64-byte-aligned owning vector. Each region's stride rounds up to 64 bytes; views are computed from the current owner, with no stored spans. A second variant also omits the unused fixed-minimum-match-5 tag allocations. Production headers were unchanged. The reference reserves its three code arrays from its shared workspace (`reference_zstd/lib/compress/zstd_compress.c`, `ZSTD_cwksp_reserve_buffer` calls); this prototype consolidates only the code arrays, not that entire workspace design.

Three rotating baseline/slab/slab-without-tags trials per input and placement used 8,000 frames on CPU 2. All 72 workers matched reference compressed bytes and passed allocation guards. Median paired throughput changes relative to the current layout:

| Pre-construction padding (bytes) | Mixed: slab | Mixed: slab without tags | Varied: slab | Varied: slab without tags |
| ---: | ---: | ---: | ---: | ---: |
| 0 | +1.68% | -6.37% | -2.49% | -3.68% |
| 64 | +0.36% | -9.05% | -4.71% | -5.68% |
| 256 | -2.26% | -7.00% | -4.91% | -1.80% |
| 2048 | +7.57% | +7.10% | -3.48% | -1.45% |

The slab's LL/OF/ML page offsets were 64/2816/1472 in the recorded trials, with or without tags. Thus stabilizing these offsets did not eliminate sensitivity to removing tags; other allocation placement and generated-code differences remain uncontrolled. This does not identify a hardware cause. Padding remains diagnostic only. Aligned-allocation counts were 4 for baseline, 5 for slab, and 3 for slab without tags: the slab adds one aligned allocation while replacing three ordinary allocations. These counts are not total allocation counts.

A separate assertion-enabled check passed alignment, growth at capacities 1/65/43691, independent copying, copy/move assignment and construction, and reuse after moving. The prototype remains rejected on performance grounds; no production or README measurement changes. Artifacts, source overlays, per-worker address logs and measurements: `out/code-slab-20261107/`.

Next: obtain fresh paired phase/cycle evidence for current level-2/256-KiB varied input before expanding the arena design. In particular, separate parser and entropy work from placement effects; fewer construction allocations alone have not demonstrated lower steady-state cost. Any full-workspace arena proposal must account for reference buffer ordering, growth, and ownership rather than transplanting this failed slab.

## Tag allocation and buffer-placement diagnostic, 2026-11-07

The audit confirms that fixed minimum-match-5 GCC13 ordinary, collecting and direct-offBase parser entry points do not use byte tags. Runtime minimum-match selection, other compilers and the ordinary minimum-match-4 parser still require them. History already rejected both empty tag members and merely skipping their resizes after cross-input regressions; this turn therefore investigated placement rather than repeating an uninstrumented removal claim.

Built isolated level-2 256-KiB diagnostic workers with identical pre-timing address logging, retaining member layout in both. One skips the two unused tag-vector resizes; production remains unchanged. A retained padding allocation of 0, 64, 256 or 2048 bytes before compressor construction varies placement. Three rounds shuffle the eight input/padding cases and alternate worker order, using 8,000 frames each. All 48 timed workers matched reference frames and passed post-warmup allocation guards.

The two removed tag arrays contain 32 KiB total and reduce aligned construction allocations from four to two. Long/short position-table page offsets remain equal between the two workers at each padding setting, but LL/OF/ML code arrays move backward by 128 bytes modulo 4096. With zero padding, table offsets are 1472/1536 and old code-array offsets are 1680/336/3088 versus 1552/208/2960 without tags. Input, output, parsed records and literals have page offset 16. Each configuration's observed offsets were stable across the three rounds despite different full addresses.

| Pre-construction padding (bytes) | Mixed paired throughput change | Wins | Varied paired throughput change | Wins |
| ---: | ---: | ---: | ---: | ---: |
| 0 | -4.76% | 0/3 | -2.25% | 1/3 |
| 64 | -3.48% | 0/3 | -2.22% | 1/3 |
| 256 | -4.96% | 0/3 | -1.72% | 0/3 |
| 2048 | +2.57% | 3/3 | +0.90% | 2/3 |

This demonstrates placement sensitivity in the diagnostic and identifies an actual address change. It does not isolate a cache or aliasing mechanism, nor establish production speedup: logging alters constructor code, only three pairs were run per case, and the artificial padding is not a proposed production fix. RSS averages were modestly lower without tags, but savings varied with allocation behavior. No source or README performance rows changed.

NEXT: prototype an owning, aligned slab for the three sequence-code arrays, with fixed relative offsets and a single allocation. Compare it both with and without unused tag storage under the same placement controls before considering production. Preserve scratch growth and copy/move semantics; avoid stored spans that would alias the wrong owner after a workspace copy. Audit prior work for equivalent slab designs first. The purpose is to reduce allocation count and make code-buffer placement intentional, not tune a magic padding value. Raw addresses, allocation counts, snapshots, metrics and exact frames: `out/tag-placement-20261107/`. Remaining compression/decompression speed gaps keep the overall goal open.

## GCC minimum-match-4 direct comparisons retained, 2026-11-07

The history audit ruled out repeating canonical-code caching at commit and a two-container Huffman rewrite: both had already passed byte checks but failed throughput confirmation. Instead, the current profile identified the tagged double-fast parser as about half of 256-KiB level-3 samples. Its 18/16/16/4 window/hash/chain/minMatch specialization differed from the already direct minimum-match-5 path.

The GCC13 direct-offBase parser now uses full candidate comparisons for that measured specialization, eliminating its byte-tag reads, writes and rejection checks while retaining candidate-index, window and full-match validation. A single policy applies throughout the frame. Other configurations retain their existing selection. Six alternating CPU2 old/new pairs of 12,000 frames each gained mixed +20.82% (6/6 wins), varied +18.30% (6/6), structured +25.01% (6/6) and random +9.31% (6/6). Zero was near neutral at -0.24% (2/6), and repeated lost -1.22% (1/6); the repeated regression is an explicit retained tradeoff. All 72 worker frames matched reference and passed worker guards.

Extended the sequence oracle to compare tagged logical records against direct encoded-offset records with the actual 18/16/16/4 parameters over four input shapes, eight blocks, window expiry and reset epochs. Optimized unit tests and all-level acceptance passed in 18.07 seconds. Rebuilt 64-KiB and 1-MiB level-3 binaries exactly match saved production executables; the integrated 256-KiB binary exactly matches the measured isolated candidate.

Independent six alternating current/reference pairs on all six 256-KiB input types passed byte parity and both decoders. Current/reference throughput: mixed 111.31%, varied 102.29%, structured 102.95%, random 126.66%, repeated 206.42%, zero 284.31%. README now publishes all nine metrics for these six rows, preserving the 64 KiB and main 1 MiB matrices and native Windows results. The narrow varied/structured leads are measurement snapshots, not universal completion. Artifacts and source snapshots: `out/l3-min4-direct-20261107/`.

NEXT: audit unused tag-buffer construction in compile-time direct parser paths. The minimum-match-5 GCC path already selects direct parsing for ordinary, collected and encoded-offset views, but still allocates tag storage; validate every entry point before eliding buffers, especially runtime-parameter and other compiler paths. Treat the newly direct minimum-match-4 view separately because its ordinary logical parser still uses tags. Measure memory and throughput on all affected inputs rather than assuming allocation removal improves steady-state speed. The 64 KiB worker resolves to 16/16/15/5 and is already direct, so another tag-removal trial there would be redundant. Remaining all-level compression/decompression gaps still require work.

## GCC level-3 256-KiB double-RLE shortcut retained, 2026-11-07

An isolated 3,000-frame phase trace on current 256-KiB inputs remained byte-identical. Mixed parse/encode took 228.36/186.65 ms; encoder gathering/literals/tables/bits were 42.96/62.20/26.58/53.87 ms. LL and OF each selected run-length mode in 3,001 of 9,003 sequence blocks (including warmup). Varied parse/encode took 167.99/122.45 ms, split 18.64/48.59/20.65/33.91 ms across the same encoder stages, with no LL/OF run-length tables. These instrumented times are diagnostic and include timer overhead, not headline throughput.

Extended the existing GCC13 level-3 double-RLE writer policy to the 262,144-byte pledged size in addition to 1 MiB. Table-mode checks, extra-bit writes and validation remain unchanged; no allocation is added. Six alternating CPU2 old/new pairs of 12,000 frames per worker improved mixed +3.07% (5/6 wins), varied +1.43% (6/6), structured +2.30% (5/6), repeated +5.86% (5/6), and zero +21.43% (6/6); random was near neutral at -0.29% (2/6). All 72 worker frames matched reference bytes and passed worker guards. Varied never takes the shortcut, so its gain may be code-generation/layout related; do not attribute every gain to omitted FSE operations.

Added permanent 256-KiB level-3 mixed/varied compression acceptance cases. Optimized units and expanded all-level acceptance passed in 18.50 seconds. The rebuilt 1 MiB level-3 executable is byte-identical to its saved production version; the newly compiled production 256 KiB worker exactly matches the isolated candidate. Independent six alternating current/reference pairs per case passed exact frames and both decoders. README updates all nine affected metrics: mixed 1818.66/1995.64 MiB/s (91.13%), varied 2468.76/2868.97 (86.05%). C++/reference worker sizes remain 127.73/632.52 KiB. Thus this is a retained gain, not completion of either remaining gap.

The varied trace spends about 40% of encoder time on Huffman literals; weight serialization is 8.10 ms, stream work 20.30 ms, and repeat-code preparation 2.52 ms (nested timing categories must not be summed indiscriminately). History already rejects an early-repeat proof that added a duplicate 256-symbol scan. NEXT: audit the canonical-code lifetime at successful new-table commit. Current code saves repeat lengths/history but reconstructs the repeat codes when first needed; test whether transferring the already computed canonical codes avoids that work without a new scan, allocation or history change on rejected candidates. Verify existing cache experiments before implementation, and require varied/mixed controls. Artifacts: `out/l3-short-phases-20261107/`. Overall goal remains incomplete.

## Level-3 short-input controls and remaining gaps, 2026-11-07

Built isolated optimized GCC 13 `-O3 -DNDEBUG` workers for the pre-direct-offset source and retained source at 64 and 256 KiB. Baseline headers exactly match the Git blobs in `a8b9b94`; production files and binaries were not modified. Mixed inputs contain zero, repeated, structured and pseudo-random quarters generated at each size, rather than a prefix of the 1 MiB mixed input. Varied text uses the established varied corpus prefixes.

Six rotating old/current/reference triples per case used 50,000 frames at 64 KiB and 12,000 at 256 KiB. All 72 timed workers matched reference frames; both decoders recovered every case. Median paired current/old changes: 64-KiB mixed +2.05% (5/6 wins), varied -0.29% (3/6); 256-KiB mixed +1.73% (5/6), varied -0.29% (3/6). Thus the retained direct-offset policy improves mixed inputs with no consistent varied regression in this run. It does not solve the larger 256-KiB deficits: current/reference throughput is 89.88% mixed and 84.15% varied. README now includes these additional nine-metric measurements so the 1 MiB matrix does not conceal them. Table throughput uses median elapsed time; paired changes use median per-round speed ratios.

| Input | Size (KiB) | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| mixed | 64 | 1648.60 | 1603.12 | 102.84% | 4.66 | 4.38 | 106.33% | 123.64 | 632.52 | 19.55% |
| varied | 64 | 3291.77 | 3214.24 | 102.41% | 4.60 | 4.32 | 106.33% | 123.64 | 632.52 | 19.55% |
| mixed | 256 | 1807.45 | 2010.89 | 89.88% | 5.17 | 4.88 | 106.00% | 127.73 | 632.52 | 20.19% |
| varied | 256 | 2444.44 | 2904.91 | 84.15% | 4.97 | 4.83 | 102.99% | 127.73 | 632.52 | 20.19% |

Matched CPU2 `perf record` captures on 30,000 frames per worker used user cycles with period 100,003 and had zero lost samples. Mixed reference/current times were 5.037/5.454 s; sampled cycle totals were approximately 16.701/16.633 billion, so elapsed differences there must not be explained solely by excess cycle counts. Varied times were 3.391/4.026 s and sampled cycles approximately 10.346/12.284 billion, reproducing its deficit in both measures. Profiled frames matched exactly.

Varied C++ self samples: double-fast parser 49.13%, match counter 6.27%, broad sequence encoder 16.64%, enclosing frame 6.00%, Huffman literal selection 5.28%, Huffman stream 3.66%, Huffman tree 3.29%. Reference: parser 60.81%, sequence writer 8.98%, sequence-to-codes 4.29%, splitter 5.41%, Huffman table builder 5.12%. These functions include different work; shares are leads, not like-for-like phase ratios. The 256-KiB C++ parser resolves to window/hash/chain/minMatch 18/16/16/4 and uses tags, unlike the 1 MiB minMatch-5 direct-candidate specialization. The broad encoder needs phase attribution before choosing between parser, gathering, table selection or bit writing.

NEXT: collect 256-KiB level-3 encoder subphase timings and LL/OF/ML table modes on mixed and varied input. The double-RLE shortcut is currently selected only for 1 MiB level 3; verify mode frequency before extending it, and keep the varied-input deficit as a required control. Review prior phase/dispatch experiments before changing generic policies. All artifacts, source snapshots, hashes, per-worker metrics and profiles are under `out/l3-short-offbase-20261107/`. No library change in this step; overall goal remains incomplete.

## GCC level-3 direct encoded offsets retained, 2026-11-07

Extended the measured direct-offBase parser policy to GCC 13 level 3. Its double-RLE and adaptive sequence writers now receive the same representation policy as the main encoder, including last-sequence extra bits and both reverse loops. Logical-offset callers retain the existing behavior. Levels 2 and 4 were rebuilt and their complete executable hashes exactly match saved production workers.

Six alternating CPU2 old/new pairs measured structured +5.79% (6/6 wins), mixed +5.49% (6/6), varied text +1.53% (6/6), random -0.09% (3/6), repeated -1.52% (0/6), and zero +2.15% (5/6). Structured/mixed/varied/random used 3,000 frames per worker; repeated/zero used 10,000. All 72 workers produced exact reference frames and passed worker guards. Retained with the explicit repeated-input regression: it remains 184.99% of reference throughput in the independent comparison, while the structured bottleneck gains substantially. No claim that every input improved versus the prior C++ build.

Optimized units and all-level compression acceptance passed in 17.85 seconds combined. Acceptance also covers short 64 KiB level-3 mixed and varied inputs; those checks establish byte correctness, not performance at that size. Independent six-trial, 3,000-frame reference comparisons on all five table inputs passed compressed-byte parity and both encodings with both decoders. Structured is 781.37/741.73 MiB/s (105.34%), mixed 2105.95/1731.30 (121.64%). All five current measured level-3 rows exceed reference. C++ worker size is 127.74 KiB versus 632.52 KiB reference. README and BENCHMARKS_INPUTS update all nine metrics for those five rows and preserve other results.

Artifacts, snapshots, raw controls, validation, hashes and the table update script: `out/l3-direct-offbase-20261106/` (run began before midnight). NEXT: measure the newly selected direct-offset policy on short pledged level-3 inputs against the pre-change source; do not infer short-input performance from 1 MiB gains. Then return to remaining structured compression/decompression deficits. Overall all-level performance completion remains unproven.

## GCC level-4 direct encoded offsets retained, 2026-11-06

The double-fast parser now offers an explicit direct-offBase view: ordinary matches store offset+3, repeat matches store the repeat code, and the record's separate repeat field is cleared. GCC 13 level 4 selects that view and the corresponding entropy-writer policy, avoiding reconstruction in both code gathering and reverse bit writing. Records stay the same size, buffers are reused, and ordinary parser callers retain logical offsets. All three emission sites (ordinary match, immediate repeat and RLE shortcut) are covered. Partition repair is confined to optimal strategies and does not consume these records.

Six alternating CPU2 old/new pairs gave median paired throughput changes of structured +4.22% (6/6 wins, 3,000 frames), mixed +2.11% (4/6, 3,000), random +0.23% (3/6, 3,000), repeated +10.32% (6/6, 10,000), zero +25.12% (6/6, 10,000), and varied text +4.29% (6/6, 3,000). All 72 workers produced exact reference frames and passed worker guards. The large gains on simple inputs may include generated-code layout effects; the experiment does not attribute every gain solely to removed conversions.

Independent current/reference comparisons used six alternating 3,000-frame trials for each of the five table inputs, with byte parity and both decoders checking both encodings. Structured measures 710.16/701.63 MiB/s (101.21% of reference); mixed 2013.67/1645.96 (122.34%). All five measured rows exceed reference, but the structured lead remains narrow. README and BENCHMARKS_INPUTS contain all nine current throughput, whole-worker peak RSS and executable-size metrics for those rows. C++ executable size is 127.74 KiB versus reference 632.52 KiB. Other rows and the Windows section are unchanged.

Added record-by-record equivalence tests for minimum-match 5 and 6, four input shapes, eight blocks per frame and two reset epochs, checking block metadata, every LL/ML/offBase and cleared repeat fields. Optimized units passed in 0.35 seconds, and all-level compression acceptance passed in 17.36 seconds. Rebuilt neighboring level-3 and level-5 executables exactly match their saved hashes. No added allocation or changed frame format. Artifacts, baseline/candidate controls, reference reports, hashes and table-update script: `out/double-fast-offbase-20261106/`.

NEXT: evaluate the same representation for the level-3 double-fast specialization using separate baseline controls; it has an existing double-RLE writer policy, so its result cannot be inferred from level 4. Keep level 4's new implementation and its cross-input controls intact. The broader compression/decompression goal remains incomplete.

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
