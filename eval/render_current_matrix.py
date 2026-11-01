"""Render the current all-level benchmark CSVs into project documentation."""

import argparse
import csv
from datetime import date
from pathlib import Path


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


def detailed_table(data, operation):
    lines = ["| Input | Level | C++ speed (MiB/s) | Ref speed (MiB/s) | Speed % | "
             "C++ RSS (MiB) | Ref RSS (MiB) | RSS % | C++ code (KiB) | Ref code (KiB) | Code % |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for kind in KINDS[:-1]:
        for level in range(1, 23):
            values = data[kind, level, operation]
            lines.append(f"| {kind} | {level} | {values[0]:.2f} | {values[1]:.2f} | "
                         f"{percent(values[0], values[1]):.2f}% | {values[2]:.2f} | "
                         f"{values[3]:.2f} | {percent(values[2], values[3]):.2f}% | "
                         f"{values[4]:.2f} | {values[5]:.2f} | "
                         f"{percent(values[4], values[5]):.2f}% |")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mixed", type=Path, required=True)
    parser.add_argument("--shapes", type=Path, required=True,
                        help="directory containing zero/repeated/structured/random/metrics.csv")
    parser.add_argument("--readme", type=Path, default=Path("README.md"))
    parser.add_argument("--details", type=Path, default=Path("BENCHMARKS_INPUTS.md"))
    parser.add_argument("--date", default=date.today().isoformat(),
                        help="date on which the measurements were collected")
    args = parser.parse_args()
    data = load_eval(args.mixed, "mixed")
    for name in KINDS[:-1]:
        data |= load_eval(args.shapes / name / "metrics.csv", name)
    expected = {(kind, level, operation) for kind in KINDS
                for level in range(1, 23) for operation in OPERATIONS}
    if set(data) != expected:
        raise ValueError(f"incomplete matrix: missing {expected - set(data)}; extra {set(data) - expected}")

    performance = f"""## Performance

Measured {args.date} from the current optimized GCC 13 build against the bundled reference on five deterministic 1 MiB inputs at every level. Each cell shows **throughput / peak memory / executable size**, as C++ percentages of reference. Higher throughput and lower memory or code size are better. Compression frames matched byte for byte, and both decoders recovered the input. The mixed-input rows used three alternating trials of 200 iterations; other inputs used two alternating trials of 100 iterations. Short runs, especially for zero-filled data, are a screening comparison rather than a precise speedup claim.

### Compression (% of reference)

""" + grid(data, "compress") + """

### Decompression (% of reference)

""" + grid(data, "decompress") + """

[Full measurements and method](BENCHMARKS.md#current-evaluation) include the C++ and reference values for mixed input. [Other input measurements](BENCHMARKS_INPUTS.md) contain the full values for zero, repeated, structured, and random data. [Experiment history](PERFORMANCE_HISTORY.md) and the [performance work list](PERFORMANCE_PLAN.md) track progress separately.

To benchmark your own 1 MiB input, build the optimized `msvc-pair` or `gcc-pair` target and run `zstdpp_pair --level 3 --input path/to/file --iterations 1000 --trials 6`. The pair tool checks exact compressed bytes and both decoders.

"""
    readme = args.readme.read_text(encoding="utf-8")
    start = readme.index("## Performance\n")
    end = readme.index("## Project notes\n", start)
    updated = readme[:start] + performance + readme[end:]
    if "- [Other input benchmark matrix](BENCHMARKS_INPUTS.md)" not in updated:
        updated = updated.replace("- [Benchmark tables and methodology](BENCHMARKS.md)",
                                  "- [Benchmark tables and methodology](BENCHMARKS.md)\n- [Other input benchmark matrix](BENCHMARKS_INPUTS.md)")
    args.readme.write_text(updated, encoding="utf-8", newline="\n")

    details = f"""# Current benchmark matrix: additional input shapes

Measured {args.date} with optimized GCC 13 Release `-O3` workers from the current checkout, 1 MiB inputs, two alternating trials of 100 frames per row for each operation. Each result uses the median elapsed time and peak resident set size. Code size is the complete worker executable. Percentages are C++ / reference × 100; higher throughput and lower RSS or code size are better. These short runs are a broad screening pass and should be repeated for close results.

Inputs: `zero` is all zero bytes; `repeated` repeats the benchmark phrase; `structured` uses `(index / 16 + index % 7) & 255`; `random` is Python's seeded pseudo-random byte stream (`0x5A17C0DE`). Every compression trial produced the same bytes as reference, and both production decoders recovered the input from that common frame. Workers checked their own output and ordinary allocation counts after warmup. Generate inputs with `eval/make_matrix_inputs.py` and measure with `zstdpp_eval --input`.

[README overview](README.md#performance) · [Current mixed-input measurements](BENCHMARKS.md#current-evaluation) · [Experiment history](PERFORMANCE_HISTORY.md)

## Compression

""" + detailed_table(data, "compress") + """

## Decompression

""" + detailed_table(data, "decompress") + "\n"
    args.details.write_text(details, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
