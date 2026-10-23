"""Check maintained C++ sources with the same clang-format version used in CI."""

import argparse
from pathlib import Path
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang-format", default="clang-format")
    arguments = parser.parse_args()
    result = subprocess.run(
        [arguments.clang_format, "--version"], check=True, capture_output=True, text=True
    )
    if "clang-format version 19.1.5" not in result.stdout:
        parser.error("clang-format 19.1.5 is required for reproducible formatting")
    root = Path(__file__).resolve().parents[1]
    sources = sorted(
        path
        for directory in ("apps", "include", "src", "tests/unit")
        for path in (root / directory).rglob("*")
        if path.suffix in (".cpp", ".hpp", ".h")
    )
    if not sources:
        parser.error("no maintained C++ sources found")
    sources.append(root / "cmake/version.hpp.in")
    subprocess.run(
        [arguments.clang_format, "--dry-run", "--Werror", *map(str, sources)], check=True
    )


if __name__ == "__main__":
    main()
