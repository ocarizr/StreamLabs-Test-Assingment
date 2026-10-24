"""Exercise the installed Release distribution independently of unit test state."""

import argparse
import importlib.util
import os
from pathlib import Path
import tempfile
import zipfile

SPEC = importlib.util.spec_from_file_location(
    "release", Path(__file__).resolve().parents[2] / "scripts/release.py"
)
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)

IPC_SPEC = importlib.util.spec_from_file_location(
    "check_ipc", Path(__file__).with_name("check_ipc.py")
)
ipc = importlib.util.module_from_spec(IPC_SPEC)
IPC_SPEC.loader.exec_module(ipc)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--cmake", required=True)
    arguments = parser.parse_args()
    tag = f"V{arguments.version}"

    # Install and validate all three distributions in a fresh directory.
    with tempfile.TemporaryDirectory(prefix="pipeforge-e2e-") as temporary:
        output = Path(temporary)
        release.package_runtime(arguments.build.resolve(), output, tag, arguments.cmake)
        release.smoke_packages(output, tag)

        # Run IPC using only extracted runtime assets and a system-only PATH.
        # The split client/server packages must work just as the combined ZIP does.
        environment = dict(os.environ)
        environment["PATH"] = str(Path(os.environ["SystemRoot"]) / "System32")
        extracted = {}

        for kind, roles in release.PACKAGE_ROLES.items():
            archive = output / release.archive_name(kind, tag)
            release.validate_archive(archive, roles)
            destination = output / kind
            destination.mkdir()

            with zipfile.ZipFile(archive) as package:
                package.extractall(destination)

            extracted[kind] = destination

        for name, client, server in (
            (
                "split",
                extracted["client"] / "pipeforge-client.exe",
                extracted["server"] / "pipeforge-server.exe",
            ),
            (
                "bundle",
                extracted["bundle"] / "pipeforge-client.exe",
                extracted["bundle"] / "pipeforge-server.exe",
            ),
        ):
            working = output / f"{name}-session"
            working.mkdir()
            ipc.exercise(client, server, working, environment)


if __name__ == "__main__":
    main()
