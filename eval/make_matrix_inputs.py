"""Write deterministic 1 MiB inputs for zstdpp_eval --input matrix runs."""

import argparse
import random
from pathlib import Path


SIZE = 1 << 20


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    phrase = b"The quick brown fox jumps over the lazy dog. Zstandard C++ evaluation.\n"
    rng = random.Random(0x5A17C0DE)
    samples = {
        "zero": bytes(SIZE),
        "repeated": (phrase * (SIZE // len(phrase) + 1))[:SIZE],
        "structured": bytes(((index // 16 + index % 7) & 255) for index in range(SIZE)),
        "random": rng.randbytes(SIZE),
    }
    for name, content in samples.items():
        path = args.output / f"{name}.bin"
        path.write_bytes(content)
        print(path)


if __name__ == "__main__":
    main()
