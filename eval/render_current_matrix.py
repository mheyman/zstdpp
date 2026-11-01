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


def confirmed_pair(path):
    report = path.read_text(encoding="utf-8")
    rows = [line for line in report.splitlines()
            if line.startswith("| ") and line[2:3].isdigit()]
    if len(rows) != 1:
        raise ValueError(f"expected one numeric benchmark row: {path}")
    values = [float(value.strip()) for value in rows[0].strip("|").split("|")]
    if len(values) != 9:
        raise ValueError(f"expected nine benchmark metrics: {path}")
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
            values[6], values[7])


def confirmed_decoder_pairs(directory, level, code_sizes):
    samples = {"cpp": [], "reference": []}
    for trial in range(1, 7):
        for name in samples:
            path = directory / f"l{level}-{name}-{trial}.csv"
            with path.open(newline="") as stream:
                rows = list(csv.reader(stream))
            if len(rows) != 1 or len(rows[0]) != 5:
                raise ValueError(f"invalid decoder trial: {path}")
            elapsed, peak, input_bytes, output_bytes, iterations = map(int, rows[0])
            if elapsed <= 0 or peak <= 0 or input_bytes <= 0 or output_bytes != 1048576 or iterations != 10000:
                raise ValueError(f"unexpected decoder trial values: {path}")
            samples[name].append((elapsed, peak))
    cpp_elapsed = median(row[0] for row in samples["cpp"])
    reference_elapsed = median(row[0] for row in samples["reference"])
    return (10000 * 1e9 / cpp_elapsed, 10000 * 1e9 / reference_elapsed,
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
    parser.add_argument("--confirmed-structured-decode", type=Path,
                        help="six longer paired structured decoder trials for levels 16–22")
    args = parser.parse_args()
    data = load_eval(args.mixed, "mixed")
    for name in KINDS[:-1]:
        data |= load_eval(args.shapes / name / "metrics.csv", name)
    expected = {(kind, level, operation) for kind in KINDS
                for level in range(1, 23) for operation in OPERATIONS}
    if set(data) != expected:
        raise ValueError(f"incomplete matrix: missing {expected - set(data)}; extra {set(data) - expected}")
    if args.confirmed_l13:
        data["mixed", 13, "compress"] = confirmed_pair(args.confirmed_l13)
    if args.confirmed_l16:
        data["mixed", 16, "compress"] = confirmed_pair(args.confirmed_l16)
    if args.confirmed_structured_decode:
        for level in range(16, 23):
            key = "structured", level, "decompress"
            data[key] = confirmed_decoder_pairs(args.confirmed_structured_decode, level, data[key][4:])

    confirmation_note = ""
    if args.confirmed_l13:
        confirmation_note += ("The mixed-input level 13 compression cell uses a longer six-trial "
                              "1,000-frame [confirmation](BENCHMARKS.md#confirmed-mixed-input-level-13-compression). ")
    if args.confirmed_l16:
        confirmation_note += ("The mixed-input level 16 compression cell uses a longer six-trial "
                              "2,000-frame [confirmation](BENCHMARKS.md#confirmed-mixed-input-level-16-compression). ")
    if confirmation_note:
        confirmation_note += "The short-screen rows remain in the detailed evaluation.\n\n"

    performance = f"""## Performance

Measured {args.date} from the current optimized GCC 13 build against the bundled reference on five deterministic 1 MiB inputs at every level. Each cell shows **speed / peak memory / executable size**, as C++ percentages of reference. Higher speed and lower memory or code size are better. Compression frames matched byte for byte, and both decoders recovered the input. The mixed-input rows used three alternating trials of 200 iterations; other inputs used two alternating trials of 100 iterations. Short runs, especially for zero-filled data, are a screening comparison rather than a precise speedup claim. Expand the full measurements below for C++ and reference values in MiB/s, MiB, and KiB for every level and input; they are also maintained in the [benchmark matrix](BENCHMARKS_INPUTS.md).

### Compression (% of reference)

""" + confirmation_note + grid(data, "compress") + """

### Decompression (% of reference)

""" + ("Structured levels 16–22 use six alternating 10,000-frame exact-output trials; "
       "the other cells use the screening trials described above.\n\n"
       if args.confirmed_structured_decode else "") + grid(data, "decompress") + """

<details>
<summary>Full measurements: speed, memory, and executable size</summary>

#### Compression

""" + detailed_table(data, "compress", KINDS) + """

#### Decompression

""" + detailed_table(data, "decompress", KINDS) + """

</details>

[Experiment history](PERFORMANCE_HISTORY.md) and the [performance work list](PERFORMANCE_PLAN.md) track progress separately.

To benchmark your own 1 MiB input, build the optimized `msvc-pair` or `gcc-pair` target and run `zstdpp_pair --level 3 --input path/to/file --iterations 1000 --trials 6`. The pair tool checks exact compressed bytes and both decoders.

"""
    readme = args.readme.read_text(encoding="utf-8")
    start = readme.index("## Performance\n")
    end = readme.index("## Project notes\n", start)
    updated = readme[:start] + performance + readme[end:]
    if "- [Full current benchmark matrix](BENCHMARKS_INPUTS.md)" not in updated:
        updated = updated.replace("- [Benchmark tables and methodology](BENCHMARKS.md)",
                                  "- [Benchmark tables and methodology](BENCHMARKS.md)\n- [Full current benchmark matrix](BENCHMARKS_INPUTS.md)")
    args.readme.write_text(updated, encoding="utf-8", newline="\n")

    structured_note = (" Structured decompression levels 16–22 below use six alternating "
                       "10,000-frame exact-output trials."
                       if args.confirmed_structured_decode else "")
    details = f"""# Current benchmark matrix: all input shapes

Measured {args.date} with optimized GCC 13 Release `-O3` workers from the current checkout on 1 MiB inputs. Mixed input used three alternating trials of 200 frames per row; the other inputs used two alternating trials of 100 frames. Each result uses the median elapsed time and peak resident set size. Code size is the complete worker executable. Percentages are C++ / reference × 100; higher throughput and lower RSS or code size are better. These short runs are a broad screening pass and should be repeated for close results. The mixed-input level 13 and level 16 compression rows use longer confirmed measurements, as in the README; the [short-screen rows](BENCHMARKS.md#current-evaluation) remain in the evaluation report.{structured_note}

Inputs: `zero` is all zero bytes; `repeated` repeats the benchmark phrase; `structured` uses `(index / 16 + index % 7) & 255`; `random` is Python's seeded pseudo-random byte stream (`0x5A17C0DE`); and `mixed` combines those patterns. Every compression trial produced the same bytes as reference, and both production decoders recovered the input from that common frame. Workers checked their own output and ordinary allocation counts after warmup. Generate inputs with `eval/make_matrix_inputs.py` and measure with `zstdpp_eval --input`.

[README overview](README.md#performance) · [Current mixed-input measurements](BENCHMARKS.md#current-evaluation) · [Experiment history](PERFORMANCE_HISTORY.md)

## Compression

""" + detailed_table(data, "compress", KINDS) + """

## Decompression

""" + detailed_table(data, "decompress", KINDS) + "\n"
    args.details.write_text(details, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
