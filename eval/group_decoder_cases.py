"""Audit identical decoder workloads without merging or changing measurements.

Matching groups require identical C++ and reference executables, encoded frames,
and expected output. Timing still depends on placement and run conditions; a
matching group is not evidence that one implementation is faster.
"""

import argparse
import hashlib
import json
from pathlib import Path

KINDS = ("zero", "repeated", "structured", "random", "mixed")
ARTIFACTS = ("cpp", "reference", "encoded", "expected")


def fingerprint(path):
    path = Path(path)
    before = path.stat()
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    after = path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError(f"artifact changed while hashing: {path}")
    return {"sha256": digest.hexdigest(), "bytes": after.st_size}


def group_cases(cases):
    groups = {}
    seen = set()
    for case in cases:
        identity = case["input"], case["level"]
        if identity in seen:
            raise ValueError(f"duplicate decoder case: {identity}")
        seen.add(identity)
        artifacts = {name: fingerprint(case[name]) for name in ARTIFACTS}
        key = tuple((artifacts[name]["sha256"], artifacts[name]["bytes"]) for name in ARTIFACTS)
        if key not in groups:
            groups[key] = {"artifacts": artifacts, "members": []}
        groups[key]["members"].append({
            "input": case["input"], "level": case["level"],
            "paths": {name: str(Path(case[name]).resolve()) for name in ARTIFACTS},
        })
    return list(groups.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--workers", type=Path, required=True)
    parser.add_argument("--suffix", default="", help="worker executable suffix, e.g. .exe")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    cases = [dict(input=kind, level=level,
                  cpp=args.workers / f"sph_decompress_l{level}{args.suffix}",
                  reference=args.workers / f"reference_decompress_l{level}{args.suffix}",
                  encoded=args.matrix / kind / f"reference_l{level}.zst",
                  expected=args.matrix / kind / "corpus.bin")
             for kind in KINDS for level in range(1, 23)]
    groups = group_cases(cases)
    report = {"schema": 1, "case_count": len(cases), "group_count": len(groups),
              "note": "Content identity only; no timings are merged or speed claims inferred.",
              "groups": groups}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{len(cases)} decoder cases form {len(groups)} distinct artifact groups")
    for group in groups:
        if len(group["members"]) > 1:
            print(", ".join(f"{m['input']}/{m['level']}" for m in group["members"]))


if __name__ == "__main__":
    main()
