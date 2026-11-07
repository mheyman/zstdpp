"""Render native decoder worker trials without replacing the GCC matrix."""

import argparse
import csv
import json
from pathlib import Path
from statistics import median

from render_current_matrix import KINDS, detailed_table, grid


def load_trials(path):
    grouped = {}
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            key = row["Input"], int(row["Level"]), row["Implementation"]
            grouped.setdefault(key, []).append(row)
    expected = {(kind, level, name) for kind in KINDS for level in range(1, 23)
                for name in ("cpp", "reference")}
    if set(grouped) != expected:
        raise ValueError("native decoder matrix is incomplete or contains unexpected rows")
    result = {}
    for kind in KINDS:
        for level in range(1, 23):
            metrics = {}
            signatures = []
            for name in ("cpp", "reference"):
                rows = grouped[kind, level, name]
                trials = sorted(int(row["Trial"]) for row in rows)
                if len(rows) < 3 or trials != list(range(1, len(rows) + 1)):
                    raise ValueError(f"missing or duplicate trials: {kind}/{level}/{name}")
                sizes = {int(row["DecodedBytes"]) for row in rows}
                iterations = {int(row["Iterations"]) for row in rows}
                code = {int(row["CodeBytes"]) for row in rows}
                if len(sizes) != 1 or len(iterations) != 1 or len(code) != 1:
                    raise ValueError("worker parameters changed during native comparison")
                size, count, code_bytes = sizes.pop(), iterations.pop(), code.pop()
                elapsed = [int(row["ElapsedNs"]) for row in rows]
                peaks = [int(row["PeakBytes"]) for row in rows]
                if min(size, count, code_bytes, *elapsed, *peaks) <= 0:
                    raise ValueError("native worker metrics must be positive")
                metrics[name] = (size * count * 1e9 / median(elapsed) / 1048576,
                                 median(peaks) / 1048576, code_bytes / 1024)
                signatures.append((size, count, trials))
            if signatures[0] != signatures[1]:
                raise ValueError("native workers did not measure equal work")
            longer = (kind == "structured" and level in (16, 18)) or (kind == "mixed" and level == 3)
            expected_signature = (1048576, 30000 if longer else 1000,
                                  list(range(1, 7 if longer else 4)))
            if signatures[0] != expected_signature:
                raise ValueError("native trials differ from the documented measurement protocol")
            result[kind, level, "decompress"] = tuple(
                metrics[name][index] for index in range(3) for name in ("cpp", "reference"))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trials", type=Path)
    parser.add_argument("--date", required=True)
    parser.add_argument("--readme", type=Path, default=Path("README.md"))
    parser.add_argument("--details", type=Path, default=Path("BENCHMARKS_NATIVE.md"))
    args = parser.parse_args()
    data = load_trials(args.trials)
    provenance = json.loads(args.trials.with_name("matrix-provenance.json").read_text(encoding="utf-8"))
    cpu = provenance["cpu"]
    if not isinstance(cpu, int) or cpu not in range(64):
        raise ValueError("invalid native worker CPU in provenance")
    description = (
        f"Measured {args.date} using MSVC Release `/O2 /Ob3` decoder workers on Windows logical CPU {cpu}, "
        "on the same five 1 MiB inputs and reference frames as the GCC matrix. Levels refer to "
        "the compression settings that produced each frame. Most rows use three alternating "
        "1,000-frame trials; structured levels 16 and 18 and mixed level 3 use six 30,000-frame "
        "trials. Throughput uses median worker elapsed time. Memory is median process peak "
        "working set, and code size is the complete worker executable. Both workers check "
        "exact decoded bytes at warmup; the C++ worker also rejects ordinary allocations "
        "during timing. Short-screen and near-parity results remain timing-sensitive. "
        "These are native decompression measurements; the "
        "[GCC compression/decompression matrix](BENCHMARKS_INPUTS.md) remains a separate comparison. "
        "Native speed comparisons are provisional: an "
        "[identical-binary control](BENCHMARKS.md#native-identical-binary-control-2026-11-06) "
        "showed substantial timing differences between executable copies."
    )
    table = detailed_table(data, "decompress")
    section = ("## Native Windows decompression\n\n" + description +
               "\n\nEach cell gives **speed / peak memory / executable size** as C++ percentages "
               "of reference. Higher speed and lower memory/code percentages are better.\n\n" +
               grid(data, "decompress") +
               "\n\n<details>\n<summary>Native absolute measurements for all levels and inputs</summary>\n\n" +
               table + "\n\n</details>\n\n")
    readme = args.readme.read_text(encoding="utf-8")
    end = readme.index("## More information\n")
    start = readme.find("## Native Windows decompression\n", 0, end)
    if start == -1:
        start = end
    args.readme.write_text(readme[:start] + section + readme[end:], encoding="utf-8", newline="\n")
    args.details.write_text("# Native Windows decoder benchmark matrix\n\n" + description +
                           "\n\n[README](README.md#native-windows-decompression) · "
                           "[Benchmark report](BENCHMARKS.md)\n\n" + table + "\n",
                           encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
