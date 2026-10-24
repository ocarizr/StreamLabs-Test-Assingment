"""Validate local guide links and the requirement-to-test traceability table."""

import argparse
from html.parser import HTMLParser
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit


class Destinations(HTMLParser):
    """Collect actual rendered links/assets, excluding JavaScript string templates."""

    def __init__(self):
        super().__init__()
        self.paths = []

    def handle_starttag(self, tag, attrs):
        self.paths.extend(value for key, value in attrs if key in ("href", "src") and value)


def broken_html_links(directory: Path) -> list[str]:
    """Check that an extracted documentation artifact has its local pages/assets."""
    if not (directory / "index.html").is_file():
        return ["Generated API index.html is missing"]

    failures = []

    for page in directory.rglob("*.html"):
        parser = Destinations()
        parser.feed(page.read_text(encoding="utf-8"))

        for path in parser.paths:
            link = urlsplit(path)

            if link.scheme or link.netloc or not link.path:
                continue

            target = (page.parent / unquote(link.path)).resolve()

            if not target.is_relative_to(directory.resolve()) or not target.is_file():
                failures.append(f"{page.name}: missing HTML target {path}")

    return failures


def broken_links(root: Path) -> list[str]:
    """Check repository-relative Markdown destinations without fetching external sites."""
    failures = []

    for page in [root / "README.md", *sorted((root / "docs").glob("*.md"))]:
        text = page.read_text(encoding="utf-8")

        for match in re.finditer(r"\]\(([^\s)]+)\)", text):
            link = urlsplit(match[1])

            if link.scheme or link.netloc or not link.path:
                continue

            target = (page.parent / unquote(link.path)).resolve()

            if not target.is_relative_to(root.resolve()) or not target.is_file():
                failures.append(f"{page.relative_to(root)}: missing local target {match[1]}")

    return failures


def traceability_errors(root: Path) -> list[str]:
    """Every requirement must cite existing CTest cases, rather than obsolete names."""
    tests = set(
        re.findall(
            r"add_test\(\s*NAME\s+([\w-]+)",
            (root / "tests/CMakeLists.txt").read_text(encoding="utf-8"),
        )
    )

    for source in (root / "tests").rglob("*.cpp"):
        tests.update(
            f"{suite}.{case}"
            for suite, case in re.findall(
                r"\bTEST(?:_F)?\(\s*(\w+)\s*,\s*(\w+)\s*\)", source.read_text(encoding="utf-8")
            )
        )

    guide = (root / "docs/testing.md").read_text(encoding="utf-8")
    rows = re.findall(r"^\| (R[1-8]) \| (.+)$", guide, re.M)
    failures = []

    if sorted(key for key, _ in rows) != [f"R{number}" for number in range(1, 9)]:
        failures.append("testing.md must map each of R1-R8 exactly once")

    for requirement, row in rows:
        names = re.findall(
            r"`([A-Za-z_]\w*\.[A-Za-z_]\w*|ipc-process-acceptance|runtime-packages)`", row
        )

        if not names or "ipc-process-acceptance" not in names:
            failures.append(f"{requirement}: missing process acceptance evidence")

        failures.extend(
            f"{requirement}: unknown CTest case {name}" for name in names if name not in tests
        )

    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--html", type=Path, help="also validate an extracted generated HTML tree")
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    failures = broken_links(root) + traceability_errors(root)

    if arguments.html:
        failures += broken_html_links(arguments.html)

    for failure in failures:
        print(failure, file=sys.stderr)

    if not failures:
        print("Local documentation links and R1-R8 CTest references passed")

    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
