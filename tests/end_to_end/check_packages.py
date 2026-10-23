"""Exercise the installed Release distribution independently of unit test state."""

import argparse
import importlib.util
from pathlib import Path
import tempfile

SPEC = importlib.util.spec_from_file_location(
    "release", Path(__file__).resolve().parents[2] / "scripts/release.py"
)
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--cmake", required=True)
    arguments = parser.parse_args()
    tag = f"V{arguments.version}"
    with tempfile.TemporaryDirectory(prefix="pipeforge-e2e-") as temporary:
        output = Path(temporary)
        release.package_runtime(arguments.build.resolve(), output, tag, arguments.cmake)
        release.smoke_packages(output, tag)


if __name__ == "__main__":
    main()
