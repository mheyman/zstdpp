"""Render the current all-level benchmark CSVs into project documentation."""

import argparse
import csv
from datetime import date
from pathlib import Path
import re
from statistics import median


KINDS = ("zero", "repeated", "structured", "random", "mixed")
OPERATIONS = ("compress", "decompress")


def percent(cpp, reference):
    return cpp / reference * 100


def load_eval(path, name):
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    grouped = {}
    for row in rows:
        key = name, int(row["level"]), row["operation"]
        grouped.setdefault(key, {})[row["implementation"]] = row
    result = {}
    for key, pair in grouped.items():
        cpp, reference = pair["sph-zstd++"], pair["reference-zstd"]
        def speed(row):
            return int(row["iterations"]) * 1e9 / int(row["elapsed_ns"])
        result[key] = (speed(cpp), speed(reference),
                       int(cpp["peak_bytes"]) / 1048576, int(reference["peak_bytes"]) / 1048576,
                       int(cpp["code_bytes"]) / 1024, int(reference["code_bytes"]) / 1024)
    return result


def format_ratios(values):
    return " / ".join(f"{percent(values[index], values[index + 1]):.1f}%"
                      for index in (0, 2, 4))


def grid(data, operation):
    lines = ["| Level | Zero | Repeated | Structured | Random | Mixed |",
             "| ---: | ---: | ---: | ---: | ---: | ---: |"]
    for level in range(1, 23):
        lines.append("| " + str(level) + " | " + " | ".join(
            format_ratios(data[kind, level, operation]) for kind in KINDS) + " |")
    return "\n".join(lines)


def detailed_table(data, operation, kinds=KINDS):
    lines = ["| Input | Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | "
             "C++ memory (MiB) | Reference memory (MiB) | % of Reference | "
             "C++ code (KiB) | Reference code (KiB) | % of Reference |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for kind in kinds:
        for level in range(1, 23):
            values = data[kind, level, operation]
            lines.append(f"| {kind} | {level} | {values[0]:.2f} | {values[1]:.2f} | "
                         f"{percent(values[0], values[1]):.2f}% | {values[2]:.2f} | "
                         f"{values[3]:.2f} | {percent(values[2], values[3]):.2f}% | "
                         f"{values[4]:.2f} | {values[5]:.2f} | "
                         f"{percent(values[4], values[5]):.2f}% |")
    return "\n".join(lines)


def confirmed_pair(path, code_sizes, allow_cpp_code_size_change=False):
    report = path.read_text(encoding="utf-8")
    rows = [line for line in report.splitlines()
            if line.startswith("| ") and line[2:3].isdigit()]
    if len(rows) != 1:
        raise ValueError(f"expected one numeric benchmark row: {path}")
    values = [float(value.strip()) for value in rows[0].strip("|").split("|")]
    if len(values) != 9:
        raise ValueError(f"expected nine benchmark metrics: {path}")
    if abs(values[7] - code_sizes[1]) > 0.01 or (
            not allow_cpp_code_size_change and abs(values[6] - code_sizes[0]) > 0.01):
        raise ValueError(f"confirmed worker code sizes differ from the matrix: {path}")
    if allow_cpp_code_size_change:
        code_sizes = values[6], code_sizes[1]
    with (path.parent / "trials.csv").open(newline="") as stream:
        trials = list(csv.DictReader(stream))
    if len(trials) < 2 or any(row["exact_bytes"] != "true" for row in trials):
        raise ValueError(f"missing exact paired trials: {path.parent / 'trials.csv'}")

    def median_uint(field):
        ordered = sorted(int(row[field]) for row in trials)
        middle = len(ordered) // 2
        return (ordered[middle] if len(ordered) % 2 else
                ordered[middle - 1] + (ordered[middle] - ordered[middle - 1]) // 2)

    match = re.search(r"trials of (\d+) iterations", report)
    if not match:
        raise ValueError(f"missing iteration count: {path}")
    iterations = int(match.group(1))
    return (iterations * 1e9 / median_uint("cpp_elapsed_ns"),
            iterations * 1e9 / median_uint("reference_elapsed_ns"),
            median_uint("cpp_peak_bytes") / 1048576,
            median_uint("reference_peak_bytes") / 1048576,
            *code_sizes)


def confirmed_decoder_pairs(directory, level, code_sizes, expected_iterations=10000):
    samples = {"cpp": [], "reference": []}
    for trial in range(1, 7):
        for name in samples:
            path = directory / f"l{level}-{name}-{trial}.csv"
            with path.open(newline="") as stream:
                rows = list(csv.reader(stream))
            if len(rows) != 1 or len(rows[0]) != 5:
                raise ValueError(f"invalid decoder trial: {path}")
            elapsed, peak, input_bytes, output_bytes, iterations = map(int, rows[0])
            if elapsed <= 0 or peak <= 0 or input_bytes <= 0 or output_bytes != 1048576 or iterations != expected_iterations:
                raise ValueError(f"unexpected decoder trial values: {path}")
            samples[name].append((elapsed, peak))
    cpp_elapsed = median(row[0] for row in samples["cpp"])
    reference_elapsed = median(row[0] for row in samples["reference"])
    return (expected_iterations * 1e9 / cpp_elapsed,
            expected_iterations * 1e9 / reference_elapsed,
            median(row[1] for row in samples["cpp"]) / 1048576,
            median(row[1] for row in samples["reference"]) / 1048576,
            *code_sizes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mixed", type=Path, required=True)
    parser.add_argument("--shapes", type=Path, required=True,
                        help="directory containing zero/repeated/structured/random/metrics.csv")
    parser.add_argument("--readme", type=Path, default=Path("README.md"))
    parser.add_argument("--details", type=Path, default=Path("BENCHMARKS_INPUTS.md"))
    parser.add_argument("--date", default=date.today().isoformat(),
                        help="date on which the measurements were collected")
    parser.add_argument("--confirmed-l13", type=Path,
                        help="longer paired level-13 mixed-input SUMMARY.md to supersede the short screen")
    parser.add_argument("--confirmed-l16", type=Path,
                        help="longer paired level-16 mixed-input SUMMARY.md to supersede the short screen")
    parser.add_argument("--confirmed-random-l15", type=Path,
                        help="longer paired level-15 random-input SUMMARY.md to supersede the short screen")
    parser.add_argument("--confirmed-structured", action="append", nargs=2,
                        metavar=("LEVEL", "SUMMARY"), default=[],
                        help="longer paired structured compression result to supersede a short-screen row")
    parser.add_argument("--confirmed-compression", action="append", nargs=3,
                        metavar=("INPUT", "LEVEL", "SUMMARY"), default=[],
                        help="longer paired compression result for one input and level")
    parser.add_argument("--confirmed-structured-decode", type=Path,
                        help="six longer paired structured decoder trials for levels 16–22")
    parser.add_argument("--confirmed-random-decode", type=Path,
                        help="six paired 100,000-frame random decoder trials for levels 3 and 4")
    args = parser.parse_args()
    data = load_eval(args.mixed, "mixed")
    for name in KINDS[:-1]:
        data |= load_eval(args.shapes / name / "metrics.csv", name)
    expected = {(kind, level, operation) for kind in KINDS
                for level in range(1, 23) for operation in OPERATIONS}
    if set(data) != expected:
        raise ValueError(f"incomplete matrix: missing {expected - set(data)}; extra {set(data) - expected}")
    if args.confirmed_l13:
        key = "mixed", 13, "compress"
        data[key] = confirmed_pair(args.confirmed_l13, data[key][4:])
    if args.confirmed_l16:
        key = "mixed", 16, "compress"
        data[key] = confirmed_pair(args.confirmed_l16, data[key][4:])
    if args.confirmed_random_l15:
        key = "random", 15, "compress"
        data[key] = confirmed_pair(args.confirmed_random_l15, data[key][4:])
    structured_levels = []
    confirmed_keys = set()
    for level_text, path_text in args.confirmed_structured:
        level = int(level_text)
        if level not in range(1, 23) or level in structured_levels:
            raise ValueError(f"invalid or repeated structured confirmation level: {level}")
        key = "structured", level, "compress"
        data[key] = confirmed_pair(Path(path_text), data[key][4:])
        structured_levels.append(level)
        confirmed_keys.add(key)
    confirmed_double_fast_levels = set()
    confirmed_greedy_inputs = set()
    other_confirmed_compression = set()
    for kind, level_text, path_text in args.confirmed_compression:
        level = int(level_text)
        key = kind, level, "compress"
        if kind not in KINDS or level not in range(1, 23) or key in confirmed_keys:
            raise ValueError(f"invalid or repeated compression confirmation: {kind}, {level}")
        data[key] = confirmed_pair(Path(path_text), data[key][4:], level in (2, 5))
        confirmed_keys.add(key)
        if level in (3, 4):
            confirmed_double_fast_levels.add(level)
        elif level == 5:
            confirmed_greedy_inputs.add(kind)
        else:
            other_confirmed_compression.add((kind, level))
    if args.confirmed_structured_decode:
        for level in range(16, 23):
            key = "structured", level, "decompress"
            data[key] = confirmed_decoder_pairs(args.confirmed_structured_decode, level, data[key][4:])
    if args.confirmed_random_decode:
        for level in (3, 4):
            key = "random", level, "decompress"
            data[key] = confirmed_decoder_pairs(args.confirmed_random_decode, level,
                                                data[key][4:], 100000)

    performance = f"""## Performance

Measured {args.date} with optimized GCC 13 against the bundled reference on five deterministic 1 MiB inputs at levels 1–22. Each cell gives **speed / peak memory / executable size**, in that order, as C++ percentages of reference. Above 100% means faster for speed; below 100% means less memory or smaller code for the other two values. Compression frames matched byte for byte, and both decoders recovered the input. Short trials are a screening comparison. Expand the absolute tables below for all nine C++ and reference metrics at every level and input. The [full benchmark report](BENCHMARKS_INPUTS.md) gives input definitions and longer confirmations.

### Compression (% of reference)

""" + grid(data, "compress") + """

### Decompression (% of reference)

""" + grid(data, "decompress") + """

<details>
<summary>Absolute C++ and reference measurements for every level and input</summary>

#### Compression

""" + detailed_table(data, "compress") + """

#### Decompression

""" + detailed_table(data, "decompress") + """

</details>

"""
    readme = args.readme.read_text(encoding="utf-8")
    start = readme.index("## Performance\n")
    end = readme.index("## More information\n", start)
    updated = readme[:start] + performance + readme[end:]
    args.readme.write_text(updated, encoding="utf-8", newline="\n")

    structured_note = (" Structured decompression levels 16–22 below use six alternating "
                       "10,000-frame exact-output trials."
                       if args.confirmed_structured_decode else "")
    random_decode_note = (" Random decompression levels 3 and 4 use six alternating "
                          "100,000-frame exact-output trials in "
                          "[BENCHMARKS.md](BENCHMARKS.md#confirmed-random-input-decompression)."
                          if args.confirmed_random_decode else "")
    confirmed_levels = [str(level) for level, present in ((13, args.confirmed_l13),
                        (16, args.confirmed_l16)) if present]
    confirmed_note = (" The mixed-input level " + " and ".join(confirmed_levels) +
                      " compression rows use longer confirmed measurements; the "
                      "[short-screen rows](BENCHMARKS.md#mixed-input-screening-evaluation) remain in the evaluation report."
                      if confirmed_levels else "")
    if structured_levels:
        confirmed_note += (" Structured compression levels " +
                           ", ".join(map(str, sorted(structured_levels))) +
                           " use longer six-trial confirmations in "
                           "[BENCHMARKS.md](BENCHMARKS.md#confirmed-structured-input-compression).")
    if 3 in confirmed_double_fast_levels:
        confirmed_note += (" Compression level 3 uses six-trial confirmations on all five inputs in "
                           "[BENCHMARKS.md](BENCHMARKS.md#confirmed-level-3-double-rle-sequence-writer).")
    guard_levels = sorted(level for level in confirmed_double_fast_levels if level != 3)
    if guard_levels:
        confirmed_note += (" Compression level" + ("s " if len(guard_levels) != 1 else " ") +
                           ", ".join(map(str, guard_levels)) +
                           (" use " if len(guard_levels) != 1 else " uses ") +
                           "six-trial current-candidate confirmations on all five inputs in "
                           "[BENCHMARKS.md](BENCHMARKS.md#confirmed-double-fast-guard-compression).")
    if confirmed_greedy_inputs:
        confirmed_note += (" Greedy compression level 5 uses six-trial confirmations on " +
                           ", ".join(kind for kind in KINDS if kind in confirmed_greedy_inputs) +
                           " inputs in [BENCHMARKS.md]"
                           "(BENCHMARKS.md#confirmed-direct-offbase-level-5-compression).")
    if other_confirmed_compression:
        confirmed_note += (" Additional longer paired compression confirmations apply to " +
                           ", ".join(f"{kind} level {level}"
                                     for kind, level in sorted(other_confirmed_compression)) + ".")
    if args.confirmed_random_l15:
        confirmed_note += (" Random compression level 15 uses a longer six-trial confirmation in "
                           "[BENCHMARKS.md](BENCHMARKS.md#confirmed-random-input-level-15-compression).")
    details = f"""# Current benchmark matrix: all input shapes

Measured {args.date} with optimized GCC 13 Release `-O3` workers from the current checkout on 1 MiB inputs. Mixed input used three alternating trials of 200 frames per row; the other inputs used two alternating trials of 100 frames. Each result uses the median elapsed time and peak resident set size. Code size is the complete worker executable. Percentages are C++ / reference × 100; higher throughput and lower RSS or code size are better. These short runs are a broad screening pass and should be repeated for close results.{confirmed_note}{structured_note}{random_decode_note}

Inputs: `zero` is all zero bytes; `repeated` repeats the benchmark phrase; `structured` uses `(index / 16 + index % 7) & 255`; `random` is Python's seeded pseudo-random byte stream (`0x5A17C0DE`); and `mixed` combines those patterns. Every compression trial produced the same bytes as reference, and both production decoders recovered the input from that common frame. Workers checked their own output and ordinary allocation counts after warmup. Generate inputs with `eval/make_matrix_inputs.py` and measure with `zstdpp_eval --input`.

[README overview](README.md#performance) · [Mixed-input screening measurements](BENCHMARKS.md#mixed-input-screening-evaluation) · [Experiment history](PERFORMANCE_HISTORY.md)

## Compression

""" + detailed_table(data, "compress", KINDS) + """

## Decompression

""" + detailed_table(data, "decompress", KINDS) + "\n"
    args.details.write_text(details, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
