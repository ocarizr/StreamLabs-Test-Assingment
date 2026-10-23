"""Materialize reviewed hexadecimal regression seeds and their framed variants."""

import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    arguments.output.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[1]

    # Emit both bare envelopes and length-prefixed frames from each reviewed seed.
    for seed in sorted((root / "tests/fuzz/corpus").glob("*.hex")):
        payload = bytes.fromhex(seed.read_text(encoding="utf-8"))
        (arguments.output / seed.stem).write_bytes(payload)
        (arguments.output / (seed.stem + "-framed")).write_bytes(
            len(payload).to_bytes(4, "little") + payload
        )


if __name__ == "__main__":
    main()
