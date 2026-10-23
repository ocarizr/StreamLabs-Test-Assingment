"""Run clang-tidy on every maintained translation unit in a real CMake database."""

import argparse
import json
from pathlib import Path
import re
import subprocess


def translation_units(database: list[dict], root: Path) -> list[Path]:
    """Select/deduplicate project sources, excluding legacy and generated trees."""
    selected = set()
    for entry in database:
        source = Path(entry["file"])
        if not source.is_absolute():
            source = Path(entry["directory"]) / source
        source = source.resolve()
        try:
            relative = source.relative_to(root.resolve())
        except ValueError:
            continue
        if relative.parts[0] in ("apps", "src", "tests", "benchmarks") and source.suffix in (".cpp", ".cc", ".cxx"):
            selected.add(source)
    return sorted(selected)


def header_filter(root: Path) -> str:
    """Match project headers with either Clang's slash or Windows separator."""
    prefix = r"[/\\]".join(re.escape(part) for part in root.resolve().as_posix().split("/"))
    return "^" + prefix + r"[/\\](include[/\\]pipeforge|apps|src|tests|benchmarks)[/\\]"


def actionable_output(output: str) -> str:
    """Omit only Clang's pre-filter totals; preserve every emitted diagnostic.

    HeaderFilterRegex/SystemHeaders control emitted findings, but Clang's warning
    totals still include excluded SDK/STL/dependency headers. Full output is kept
    in the build log, including these totals, for audit and troubleshooting.
    """
    return "".join(line for line in output.splitlines(keepends=True)
                   if not re.fullmatch(r"\d+ warnings? generated\.\s*", line))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--clang-tidy", default="clang-tidy")
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = arguments.build.resolve()
    version = subprocess.run(
        [arguments.clang_tidy, "--version"], check=True, capture_output=True, text=True
    ).stdout
    match = re.search(r"LLVM version (\d+)\.", version)
    if not match or int(match[1]) < 19:
        parser.error("clang-tidy 19 or newer is required")
    print(version.strip(), flush=True)
    config = f"--config-file={root / '.clang-tidy'}"
    subprocess.run([arguments.clang_tidy, config, "--verify-config"], check=True)
    database = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    sources = translation_units(database, root)
    if not sources:
        parser.error("compilation database contains no maintained C++ translation units")
    # Restrict header diagnostics to this checkout, not similarly named upstream paths.
    project_headers = header_filter(root)
    failures = 0
    log_path = build / "clang-tidy.log"
    print(f"Full clang-tidy output: {log_path} (includes pre-filter header totals)", flush=True)
    with log_path.open("w", encoding="utf-8") as log:
        for source in sources:
            label = f"Analyzing {source.relative_to(root)}"
            print(label, flush=True)
            log.write(label + "\n")
            result = subprocess.run(
                [arguments.clang_tidy, str(source), "-p", str(build), config,
                 f"--header-filter={project_headers}", "--quiet"], timeout=300,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                encoding="utf-8", errors="replace"
            )
            log.write(result.stdout)
            log.flush()
            print(actionable_output(result.stdout), end="", flush=True)
            failures += result.returncode != 0
    if failures:
        parser.exit(1, f"clang-tidy failed for {failures} translation unit(s)\n")
    print(f"clang-tidy passed for {len(sources)} translation units")


if __name__ == "__main__":
    main()
