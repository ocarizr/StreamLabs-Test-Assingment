"""Audit immutable FetchContent sources and optionally query OSV for known advisories."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import sys
from urllib.error import URLError
from urllib.request import Request, urlopen


def inventory(source: str) -> list[dict]:
    """Reject unpinned inputs and preserve the exact acquisition identity for review."""
    dependencies = []

    for name, body in re.findall(r"\bFetchContent_Declare\s*\(\s*(\w+)(.*?)\)", source, re.S | re.I):
        archive = re.search(r"\bURL\s+(https://codeload.github.com/\S+)", body)
        digest = re.search(r"\bURL_HASH SHA256=([a-f0-9]{64})(?=\s|$)", body)
        git = re.search(r"GIT_REPOSITORY\s+(https://github.com/\S+)", body)
        revision = re.search(r"GIT_TAG\s+([a-f0-9]{40})(?=\s|$)", body)

        if archive and digest:
            url = archive[1]
            commit = url.rsplit("/", 1)[-1]

            if re.fullmatch(r"[a-f0-9]{40}", commit):
                query = {"commit": commit}
            elif name == "absl" and "/refs/tags/" in url:
                # OSV's OSS-Fuzz package identity supports the hash-locked Abseil tag.
                query = {
                    "package": {"name": "abseil-cpp", "ecosystem": "OSS-Fuzz"},
                    "version": commit,
                }
            else:
                raise ValueError(f"No reviewed advisory identity for {name}: {url}")

            entry = {"name": name, "url": url, "sha256": digest[1], "query": query}
        elif git and revision:
            entry = {"name": name, "url": git[1], "query": {"commit": revision[1]}}
        else:
            raise ValueError(f"Dependency {name} needs an immutable revision or SHA256 archive")

        dependencies.append(entry)

    if {entry["name"] for entry in dependencies} != {
        "absl",
        "protobuf",
        "spdlog",
        "nlohmann_json",
        "googletest",
    } or len(dependencies) != 5:
        raise ValueError("Review new/removed dependencies and update the expected inventory")

    return dependencies


def query_advisories(dependencies: list[dict]) -> list[dict]:
    """Fail on unavailable/malformed responses; absence of findings is not a safety proof."""
    request = Request(
        "https://api.osv.dev/v1/querybatch",
        data=json.dumps({"queries": [entry["query"] for entry in dependencies]}).encode(),
        headers={"Content-Type": "application/json", "User-Agent": "PipeForge-dependency-audit"},
        method="POST",
    )

    with urlopen(request, timeout=30) as response:
        result = json.load(response)

    if not isinstance(result, dict):
        raise ValueError("OSV response must be an object")

    results = result.get("results")

    if not isinstance(results, list) or len(results) != len(dependencies):
        raise ValueError("OSV returned an incomplete dependency result set")

    for entry in results:
        if not isinstance(entry, dict) or set(entry) - {"vulns", "next_page_token"}:
            raise ValueError("OSV returned an unexpected advisory result")

        if entry.get("next_page_token"):
            raise ValueError("OSV results are paginated; review the complete finding set")

        if "vulns" in entry and (
            not isinstance(entry["vulns"], list)
            or any(not isinstance(item, dict) or not item.get("id") for item in entry["vulns"])
        ):
            raise ValueError("OSV returned malformed advisory identities")

    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--online", action="store_true", help="query OSV, failing on findings/errors"
    )
    parser.add_argument("--report", type=Path, help="write the acquisition/advisory inventory JSON")
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[1]

    try:
        dependencies = inventory((root / "cmake/Dependencies.cmake").read_text(encoding="utf-8"))
        results = query_advisories(dependencies) if arguments.online else None
        report = {
            "checked_at": datetime.now(timezone.utc).isoformat(),
            "advisory_source": "https://osv.dev" if arguments.online else None,
            "dependencies": dependencies,
            "results": results,
        }

        if arguments.report:
            arguments.report.parent.mkdir(parents=True, exist_ok=True)
            arguments.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

        findings = False

        for dependency, result in zip(dependencies, results or [{}] * len(dependencies)):
            ids = [item["id"] for item in result.get("vulns", [])]
            findings |= bool(ids)
            print(
                f"{dependency['name']}: {', '.join(ids) if ids else 'no advisory returned' if results is not None else 'immutable source verified'}"
            )

        return 1 if findings else 0
    except (OSError, URLError, ValueError) as error:
        # File/network boundaries may fail; never silently turn an unavailable audit green.
        print(f"Dependency audit failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
