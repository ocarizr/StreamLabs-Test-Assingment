"""Verify protoc identity and the generated-code guard against runtime mismatches."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--protoc", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--include", action="append", required=True)
    arguments = parser.parse_args()

    # Verify the compiler pin before exercising generated-header compatibility.
    version = subprocess.run(
        [arguments.protoc, "--version"], check=True, capture_output=True, text=True
    )

    if version.stdout.strip() != "libprotoc 36.2":
        parser.exit(1, "Unexpected protoc; generated code and runtime must share the pin\n")

    compiler = Path(arguments.compiler).stem.lower()
    msvc = compiler in ("cl", "clang-cl")
    flags = ["/nologo", "/std:c++20", "/EHsc", "/Zs"] if msvc else ["-std=c++20", "-fsyntax-only"]
    includes = [("/I" if msvc else "-I") + directory for directory in arguments.include]

    # Accept matching headers, then require an intentional runtime mismatch to fail.
    with tempfile.TemporaryDirectory(prefix="pipeforge-version-") as directory:
        source = Path(directory) / "version.cpp"
        normal = '#include "pipeforge/v1/protocol.pb.h"\n'
        source.write_text(normal, encoding="utf-8")
        command = [arguments.compiler, *flags, *includes, str(source)]
        matching = subprocess.run(command, capture_output=True, text=True, timeout=60)

        if matching.returncode:
            parser.exit(1, "Matching generated code failed:\n" + matching.stdout + matching.stderr)

        # Change only the reported runtime version for the negative compilation probe.
        source.write_text(
            "#include <google/protobuf/runtime_version.h>\n"
            "#undef PROTOBUF_VERSION\n#define PROTOBUF_VERSION 0\n" + normal,
            encoding="utf-8",
        )
        mismatched = subprocess.run(command, capture_output=True, text=True, timeout=60)

        if (
            mismatched.returncode == 0
            or "incompatible version" not in mismatched.stdout + mismatched.stderr
        ):
            parser.exit(1, "Incompatible runtime was not rejected by the generated guard\n")

    print("Pinned protoc and matching headers accepted; incompatible runtime rejected")


if __name__ == "__main__":
    main()
