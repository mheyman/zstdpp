# Performance work list

This list orders the remaining work by measurement value. Each item must preserve byte-for-byte output and be kept only when paired measurements show a repeatable improvement.

- [x] Build a paired benchmark harness with output validation and median reporting.
- [ ] Batch the Huffman literal writer so several symbols share an accumulator.
- [x] Batch FSE sequence encoding and schedule flushes like the reference writer.
- [x] Improve entropy-reader refills and interleave the four Huffman decode streams.
- [ ] Fuse sequence literal gathering with parsing while retaining a reusable workspace.
- [ ] Reduce FSE table construction clears, scratch storage, and repeated table work.
- [ ] Profile level-15 binary-tree traversal and reduce its dependent pointer and index work.
- [ ] Expand the corpus matrix and allocation checks before recording final benchmark claims.

## Measurement rule

Run alternating candidate/reference trials with the same corpus, level, and iteration count. Validate the emitted frame after every pair. Report the per-trial gain, median gain, and range. Repeat a promising result before changing another hot path.

## Recorded experiments

- A first four-symbol Huffman batching attempt preserved all level 1, 3, 5, 9, and 15 frames, but was 9.19% slower than the reference in nine paired level-1 trials. A scalar four-code version was statistically neutral, and a scalar two-code version was 1.11% slower. All were reverted; a lower-overhead batching design is required.
- An FSE sequence grouping attempt (three state transitions plus LL/ML/OF extras in one bounded reservoir update) preserved exact frames and improved level 15 by 31% in a three-trial smoke run, but regressed level 1 by 20% versus the scalar baseline. Reverted; a cheaper unchecked append or level-aware dispatch is needed.
- A cheaper FSE-only append path moved the required mask into the state transition and removed generic writer checks. It preserved exact frames, improved the paired level-1 median from -11.1% to -10.0%, and measured +10.4% at level 15 (7 trials, 500 iterations). The broader six-field grouping remains reverted.
- A four-stream Huffman decode interleave matched the reference output, but level 1 was statistically flat and level 15 improved by only about 1% versus the sequential baseline. Reverted; future decompression work should target reverse-reader refill and peek/copy overhead directly.
- Replacing the per-symbol `peek()` reader copy with a temporary-position save/restore preserved exact output and reduced decompression time by roughly 2–3% in longer 7-trial runs at levels 1 and 15. The four-stream interleave itself remains reverted.
- The sequence literal gather loop now caches the literal-storage and input end pointers, removing repeated vector-end arithmetic while retaining the existing short-copy fast path. Exact output and acceptance tests remain clean; throughput was statistically neutral, so the larger parser/gather fusion remains open.
