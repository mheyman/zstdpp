"""Compare optimized decoder workers with alternating Linux perf measurements.

Worker timing excludes setup; perf counters cover the complete worker process.
Use enough iterations to amortize setup, and keep elapsed-time and cycle ratios
separate: cycles help diagnose throughput, but do not replace throughput.
"""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess


EVENTS = ("cycles", "instructions", "branches", "branch-misses")


def positive(value):
    result = int(value)
    if result <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return result


def counters(path):
    result = {}
    with path.open(newline="") as stream:
        for row in csv.reader(stream):
            if len(row) < 3:
                continue
            event = row[2].strip().split(":")[0]
            if event not in EVENTS:
                continue
            try:
                result[event] = float(row[0])
            except ValueError as error:
                raise ValueError(f"unavailable perf event {event}: {path}") from error
            # perf's CSV percentage column is the event's scheduled fraction.
            if len(row) > 4 and row[4].strip():
                if float(row[4].strip().rstrip("%")) < 99:
                    raise ValueError(f"multiplexed perf event {event}: {path}")
    if set(result) != set(EVENTS) or result["cycles"] <= 0:
        raise ValueError(f"missing or invalid perf counters: {path}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--encoded", type=Path, required=True)
    parser.add_argument("--expected", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--iterations", type=positive, default=100000)
    parser.add_argument("--trials", type=positive, default=6)
    parser.add_argument("--cpu", type=int, default=2)
    parser.add_argument("--perf", default="perf")
    args = parser.parse_args()
    paths = {name: getattr(args, name).resolve()
             for name in ("cpp", "reference", "encoded", "expected")}
    for path in paths.values():
        if not path.is_file():
            parser.error(f"not a file: {path}")
    if args.expected.stat().st_size == 0:
        parser.error("expected output must be nonempty")
    if args.cpu < 0:
        parser.error("CPU must be nonnegative")
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("output directory must be empty")
    args.output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, LC_ALL="C")
    metadata = {
        "files": {name: {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                  for name, path in paths.items()},
        "iterations": args.iterations, "trials": args.trials, "cpu": args.cpu,
        "perf": subprocess.check_output([args.perf, "--version"], text=True, env=env).strip(),
        "counter_scope": "whole process; worker elapsed time excludes setup",
    }
    (args.output / "provenance.json").write_text(json.dumps(metadata, indent=2) + "\n")
    measurements = []
    for trial in range(1, args.trials + 1):
        for name in (("cpp", "reference") if trial % 2 else ("reference", "cpp")):
            stem = args.output / f"{name}-{trial}"
            stat_path, metrics_path = stem.with_suffix(".stat"), stem.with_suffix(".csv")
            command = ["taskset", "-c", str(args.cpu), args.perf, "stat", "-x,",
                       "-e", ",".join(EVENTS), "-o", str(stat_path), "--",
                       str(paths[name]), str(paths["encoded"]), str(paths["expected"]),
                       str(args.iterations), str(metrics_path)]
            subprocess.run(command, check=True, env=env)
            with metrics_path.open(newline="") as stream:
                rows = list(csv.reader(stream))
            if len(rows) != 1 or len(rows[0]) != 5:
                raise ValueError(f"invalid worker metrics: {metrics_path}")
            elapsed, peak, encoded, decoded, iterations = map(int, rows[0])
            if (elapsed <= 0 or peak <= 0 or encoded != paths["encoded"].stat().st_size
                    or decoded != paths["expected"].stat().st_size or iterations != args.iterations):
                raise ValueError(f"unexpected worker metrics: {metrics_path}")
            measurements.append(dict(trial=trial, implementation=name, elapsed_ns=elapsed,
                                     peak_bytes=peak, **counters(stat_path)))
        pair = {row["implementation"]: row for row in measurements[-2:]}
        cpp, reference = pair["cpp"], pair["reference"]
        print(f"trial {trial}: C++/reference cycles {cpp['cycles']/reference['cycles']:.4f}; "
              f"throughput {reference['elapsed_ns']/cpp['elapsed_ns']:.4f}", flush=True)
    with (args.output / "trials.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(measurements[0]))
        writer.writeheader()
        writer.writerows(measurements)
    pairs = [{row["implementation"]: row for row in measurements if row["trial"] == trial}
             for trial in range(1, args.trials + 1)]
    cycle_ratio = statistics.median(pair["cpp"]["cycles"] / pair["reference"]["cycles"] for pair in pairs)
    speed_ratio = statistics.median(pair["reference"]["elapsed_ns"] / pair["cpp"]["elapsed_ns"] for pair in pairs)
    lines = ["# Paired decoder cycle comparison", "",
             f"{args.trials} alternating pairs, {args.iterations} frames per worker, CPU {args.cpu}.", "",
             f"Median paired C++/reference cycle cost: **{100*cycle_ratio:.2f}%** (lower is better).",
             f"Median paired C++/reference throughput: **{100*speed_ratio:.2f}%** (higher is better).", "",
             "Counters include process startup, warmup and validation; worker timing excludes them.",
             "Workers validate decoded output at warmup; their nonzero exits fail this comparison.",
             "These diagnostic ratios do not replace the full throughput/RSS/executable-size report.", "",
             "| Implementation | Median cycles/frame | Median instructions/frame | Median branch misses/frame |",
             "| --- | ---: | ---: | ---: |"]
    for name in ("cpp", "reference"):
        values = [row for row in measurements if row["implementation"] == name]
        metrics = [statistics.median(row[event] for row in values) / args.iterations
                   for event in ("cycles", "instructions", "branch-misses")]
        lines.append(f"| {name} | " + " | ".join(f"{value:.2f}" for value in metrics) + " |")
    report = "\n".join(lines) + "\n"
    (args.output / "SUMMARY.md").write_text(report)
    print(report)


if __name__ == "__main__":
    main()
