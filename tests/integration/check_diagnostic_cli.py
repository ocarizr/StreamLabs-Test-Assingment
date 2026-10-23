"""Validate explicit diagnostic outputs and I/O failure handling in real processes."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--server", type=Path, required=True)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="pipeforge-cli-integration-") as temporary:
        directory = Path(temporary)
        for executable in (arguments.client, arguments.server):
            def run(*options):
                return subprocess.run([str(executable), *map(str, options)], cwd=directory,
                                      capture_output=True, text=True, timeout=10)

            assert run("--version").returncode == 0
            assert not list(directory.iterdir()), "default CLI invocation created diagnostic files"
            for options in (("--log-file",), ("--trace-file", "", "--version"),
                            ("--log-file", "one", "--log-file", "two", "--version")):
                result = run(*options)
                assert result.returncode == 2 and result.stderr and not result.stdout, result
            assert not list(directory.iterdir()), "invalid invocation created files"

            failed = run("--log-file", directory, "--version")
            assert failed.returncode == 1 and "Diagnostics I/O" in failed.stderr, failed
            log = directory / "application.log"
            trace = directory / "profile.json"
            if "Profiling: enabled" in run("--help").stdout:
                alias = run("--log-file", "same.log", "--trace-file", directory / "same.log", "--version")
                assert alias.returncode == 1 and not (directory / "same.log").exists(), alias
                if sys.platform.startswith("linux"):
                    full = run("--trace-file", "/dev/full", "--version")
                    assert full.returncode == 1 and "Diagnostics I/O" in full.stderr, full
                result = run("--log-file", log, "--trace-file", trace, "--version")
                assert result.returncode == 0 and not result.stderr, result
                data = json.loads(trace.read_text(encoding="utf-8"))
                events = [event for event in data["traceEvents"] if event["ph"] == "X"]
                assert {event["name"] for event in events} == {"cli.command", "print_command"}
                trace.unlink()
            else:
                assert run("--trace-file", trace, "--version").returncode == 2
                assert not trace.exists()
                assert run("--log-file", log, "--version").returncode == 0
            assert "Command completed" in log.read_text(encoding="utf-8")
            log.unlink()


if __name__ == "__main__":
    main()
