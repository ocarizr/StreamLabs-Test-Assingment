"""Verify that the committed analyzer configuration rejects representative defects.

These snippets are analyzed only, never compiled or executed. A green result
requires the expected diagnostic and a nonzero clang-tidy exit for every probe.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile

PROBES = {
    "leak": (
        "int leak() { int* value = new int{1}; return *value; }",
        "clang-analyzer-cplusplus.NewDeleteLeaks",
    ),
    "use_after_free": (
        "int use_after_free() { int* value = new int{1}; delete value; return *value; }",
        "clang-analyzer-cplusplus.NewDelete",
    ),
    "division_by_zero": (
        "int divide(int numerator) { int divisor = 0; return numerator / divisor; }",
        "clang-analyzer-core.DivideZero",
    ),
    "uninitialized": (
        "int uninitialized() { int value; return value; }",
        "clang-analyzer-core.uninitialized.UndefReturn",
    ),
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang-tidy", default="clang-tidy")
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[1]

    # Keep intentionally defective probes outside the maintained source tree.
    with tempfile.TemporaryDirectory(prefix="pipeforge-analysis-") as temporary:
        for name, (source, diagnostic) in PROBES.items():
            path = Path(temporary) / f"{name}.cpp"
            path.write_text(source, encoding="utf-8")
            result = subprocess.run(
                [
                    arguments.clang_tidy,
                    str(path),
                    f"--config-file={root / '.clang-tidy'}",
                    "--quiet",
                    "--",
                    "-std=c++20",
                ],
                capture_output=True,
                text=True,
                timeout=60,
            )

            # A failed invocation alone is insufficient: require the expected defect diagnostic.
            if result.returncode == 0 or diagnostic not in result.stdout + result.stderr:
                parser.exit(
                    1,
                    f"analyzer did not reject {name} with {diagnostic}\n{result.stdout}{result.stderr}",
                )

            print(f"Confirmed diagnostic: {diagnostic}")


if __name__ == "__main__":
    main()
