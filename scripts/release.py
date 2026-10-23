"""Validate release provenance and package installed Windows runtime components.

This module does not publish, create tags, or modify Git history. Only the
tag-push workflow is allowed to publish its validated and tested ZIP artifacts.
"""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile


def normalized_version(tag: str) -> str:
    """Return major.minor.patch for supported V/v-prefixed release tags."""
    number = r"(0|[1-9][0-9]*)"
    match = re.fullmatch(rf"[vV]{number}\.{number}(?:\.{number})?", tag)
    if not match:
        raise ValueError("release tags must be Vmajor.minor or Vmajor.minor.patch (v also accepted)")
    return ".".join((match[1], match[2], match[3] or "0"))


def git(repo: Path, *arguments: str) -> str:
    """Read repository state, converting missing refs and Git failures to errors."""
    result = subprocess.run(
        ["git", "-C", str(repo), *arguments], capture_output=True, text=True, timeout=30
    )
    if result.returncode != 0:
        raise ValueError(f"Git validation failed: {result.stderr.strip()}")
    return result.stdout.strip()


def verify_release(repo: Path, tag: str, expected_commit: str) -> str:
    """Verify tag, checkout, and membership in fetched origin/master history."""
    version = normalized_version(tag)
    if not re.fullmatch(r"[0-9a-fA-F]{40}|[0-9a-fA-F]{64}", expected_commit):
        raise ValueError("expected commit must be a full Git object ID")
    commit = git(repo, "rev-parse", "--verify", f"refs/tags/{tag}^{{commit}}")
    expected = git(repo, "rev-parse", "--verify", f"{expected_commit}^{{commit}}")
    if commit != expected or git(repo, "rev-parse", "HEAD") != commit:
        raise ValueError("tag, event commit, and checked-out commit must match")
    git(repo, "merge-base", "--is-ancestor", commit, "refs/remotes/origin/master")
    return version


PACKAGE_ROLES = {"client": ("client",), "server": ("server",), "bundle": ("client", "server")}


def archive_name(kind: str, tag: str) -> str:
    """Return a safe, versioned asset name after validating the tag."""
    normalized_version(tag)
    return f"pipeforge-{kind}-{tag}-windows-x64.zip"


def validate_archive(archive: Path, roles: tuple[str, ...]) -> None:
    """Require exactly the standalone M1 runtimes; disallow paths and extra files."""
    expected = sorted([*(f"pipeforge-{role}.exe" for role in roles), "THIRD-PARTY-NOTICES.txt"])
    with zipfile.ZipFile(archive) as package:
        if sorted(package.namelist()) != expected:
            raise ValueError(f"unexpected runtime contents in {archive.name}")
        if package.testzip() is not None:
            raise ValueError(f"corrupt ZIP: {archive.name}")


def package_runtime(build: Path, output: Path, tag: str, cmake: str = "cmake") -> None:
    """Install components into fresh staging directories and write three ZIPs.

    Only Release Windows x64 builds are supported by the publishing workflow.
    Runtime manifests are intentionally explicit: extend them with installation
    rules and tests if future milestones add distributable DLL dependencies.
    """
    normalized_version(tag)
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    if "CMAKE_BUILD_TYPE:STRING=Release" not in cache:
        raise ValueError("packages require a single-configuration Release build")
    output.mkdir(parents=True, exist_ok=True)
    # Cleanup is confined to the fresh TemporaryDirectory owned by this call.
    with tempfile.TemporaryDirectory(prefix=".stage-", dir=output) as temporary:
        for kind, roles in PACKAGE_ROLES.items():
            staging = Path(temporary) / kind
            for role in roles:
                subprocess.run(
                    [cmake, "--install", str(build), "--prefix", str(staging),
                     "--config", "Release", "--component", role], check=True, timeout=60
                )
            files = sorted(path for path in staging.rglob("*") if path.is_file())
            expected = sorted([*(f"pipeforge-{role}.exe" for role in roles), "THIRD-PARTY-NOTICES.txt"])
            if sorted(path.relative_to(staging).as_posix() for path in files) != expected:
                raise ValueError(f"unexpected installed runtime files for {kind}")
            archive = output / archive_name(kind, tag)
            with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as package:
                for path in files:
                    package.write(path, path.name)
            validate_archive(archive, roles)


def smoke_packages(output: Path, tag: str) -> None:
    """Extract each package alone and run both CLI paths with a system-only PATH."""
    version = normalized_version(tag)
    if os.name != "nt":
        raise ValueError("Windows runtime packages must be smoke-tested on Windows")
    environment = dict(os.environ)
    environment["PATH"] = str(Path(os.environ["SystemRoot"]) / "System32")
    for kind, roles in PACKAGE_ROLES.items():
        archive = output / archive_name(kind, tag)
        validate_archive(archive, roles)
        with tempfile.TemporaryDirectory(prefix="pipeforge-smoke-") as temporary:
            extracted = Path(temporary)
            with zipfile.ZipFile(archive) as package:
                package.extractall(extracted)  # Names were checked against exact basenames.
            for role in roles:
                executable = extracted / f"pipeforge-{role}.exe"
                for option in ("--version", "--help"):
                    result = subprocess.run(
                        [str(executable), option], cwd=extracted, env=environment,
                        capture_output=True, text=True, timeout=10, check=True
                    )
                    expected = f"pipeforge-{role} {version}\n" if option == "--version" else "Usage:"
                    if expected not in result.stdout or result.stderr:
                        raise ValueError(f"invalid packaged {role} {option} output")

                profiling = "Profiling: enabled" in result.stdout
                log_file = extracted / f"{role}.log"
                trace_file = extracted / f"{role}.trace.json"
                command = [str(executable), "--log-file", str(log_file)]
                if profiling:
                    command += ["--trace-file", str(trace_file)]
                command += ["--version"]
                result = subprocess.run(command, cwd=extracted, env=environment,
                                        capture_output=True, text=True, timeout=10, check=True)
                if result.stderr or result.stdout != f"pipeforge-{role} {version}\n":
                    raise ValueError(f"invalid packaged {role} diagnostic invocation")
                log = log_file.read_text(encoding="utf-8")
                if "Command started" not in log or "Command completed" not in log:
                    raise ValueError(f"missing packaged {role} logs")
                if profiling:
                    trace = json.loads(trace_file.read_text(encoding="utf-8"))
                    events = [event for event in trace["traceEvents"] if event["ph"] == "X"]
                    if {event["name"] for event in events} != {"cli.command", "print_command"}:
                        raise ValueError(f"missing packaged {role} profiling scopes")
                    if any(event["dur"] < 0 or event["ts"] < 0 or event["pid"] <= 0 or event["tid"] <= 0 for event in events):
                        raise ValueError(f"invalid packaged {role} profiling fields")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    validate = commands.add_parser("validate")
    validate.add_argument("--tag", required=True)
    validate.add_argument("--commit", required=True)
    validate.add_argument("--repo", type=Path, default=Path.cwd())
    package = commands.add_parser("package")
    package.add_argument("--tag", required=True)
    package.add_argument("--build", type=Path, required=True)
    package.add_argument("--output", type=Path, default=Path("dist"))
    package.add_argument("--cmake", default="cmake")
    smoke = commands.add_parser("smoke")
    smoke.add_argument("--tag", required=True)
    smoke.add_argument("--output", type=Path, default=Path("dist"))
    arguments = parser.parse_args()
    try:
        if arguments.command == "validate":
            version = verify_release(arguments.repo, arguments.tag, arguments.commit)
            print(version)
            if output := os.environ.get("GITHUB_OUTPUT"):
                with open(output, "a", encoding="utf-8") as stream:
                    stream.write(f"version={version}\n")
                    stream.write(f"commit={git(arguments.repo, 'rev-parse', 'HEAD')}\n")
        elif arguments.command == "package":
            package_runtime(arguments.build.resolve(), arguments.output.resolve(), arguments.tag, arguments.cmake)
        else:
            smoke_packages(arguments.output.resolve(), arguments.tag)
    except (ValueError, OSError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        parser.exit(1, f"release: {error}\n")


if __name__ == "__main__":
    main()
