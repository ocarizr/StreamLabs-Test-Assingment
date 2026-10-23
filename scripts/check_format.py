"""Check maintained C++ and Protobuf sources with the pinned clang-format."""

import argparse
from pathlib import Path
import subprocess

from control_spacing import missing_control_gaps, with_control_gaps
from proto_spacing import missing_proto_gaps, with_proto_gaps


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang-format", default="clang-format")
    parser.add_argument("--fix", action="store_true", help="apply clang-format and required block gaps")
    arguments = parser.parse_args()
    result = subprocess.run(
        [arguments.clang_format, "--version"], check=True, capture_output=True, text=True
    )
    if "clang-format version 19.1.5" not in result.stdout:
        parser.error("clang-format 19.1.5 is required for reproducible formatting")
    root = Path(__file__).resolve().parents[1]
    sources = sorted(
        path
        for directory in ("apps", "include", "src", "tests", "benchmarks")
        for path in (root / directory).rglob("*")
        if path.suffix in (".cpp", ".hpp", ".h")
    )
    if not sources:
        parser.error("no maintained C++ sources found")
    sources.append(root / "cmake/version.hpp.in")
    sources.extend(sorted((root / "proto").rglob("*.proto")))
    if arguments.fix:
        subprocess.run([arguments.clang_format, "-i", *map(str, sources)], check=True)
        for source in sources:
            text = source.read_text(encoding="utf-8")
            with_gaps = with_proto_gaps if source.suffix == ".proto" else with_control_gaps
            source.write_text(with_gaps(text), encoding="utf-8", newline="\n")
        subprocess.run([arguments.clang_format, "-i", *map(str, sources)], check=True)

    subprocess.run([arguments.clang_format, "--dry-run", "--Werror", *map(str, sources)], check=True)
    failures = []
    for source in sources:
        proto = source.suffix == ".proto"
        missing_gaps = missing_proto_gaps if proto else missing_control_gaps
        block = "message/enum" if proto else "control block"
        for line in missing_gaps(source.read_text(encoding="utf-8")):
            failures.append(f"{source.relative_to(root)}:{line}: expected an empty line after {block}")
    if failures:
        parser.exit(1, "\n".join(failures) + "\n")


if __name__ == "__main__":
    main()
