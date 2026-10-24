"""Keep broken links and stale requirement evidence from silently passing CI."""

import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "documentation", ROOT / "scripts/check_documentation.py"
)
docs = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(docs)


class DocumentationChecks(unittest.TestCase):
    def test_generated_artifact_rejects_missing_pages_and_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertTrue(docs.broken_html_links(root))
            (root / "index.html").write_text(
                '<a href="page.html">Page</a><img src="image.png">', encoding="utf-8"
            )
            self.assertEqual(len(docs.broken_html_links(root)), 2)
            (root / "page.html").write_text("Page", encoding="utf-8")
            (root / "image.png").touch()
            self.assertEqual(docs.broken_html_links(root), [])

    def test_missing_relative_links_fail_but_external_links_are_not_fetched(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "docs").mkdir()
            (root / "README.md").write_text(
                "[missing](docs/missing.md) [external](https://example.com)", encoding="utf-8"
            )
            self.assertEqual(len(docs.broken_links(root)), 1)
            (root / "docs/missing.md").write_text("Available", encoding="utf-8")
            self.assertEqual(docs.broken_links(root), [])

    def test_removed_test_and_missing_requirement_invalidate_the_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "docs").mkdir()
            (root / "tests").mkdir()
            (root / "tests/CMakeLists.txt").write_text(
                "add_test(NAME ipc-process-acceptance COMMAND executable)", encoding="utf-8"
            )
            (root / "tests/cases.cpp").write_text("TEST(Client, Connects) {}", encoding="utf-8")
            table = "\n".join(
                f"| R{number} | `ipc-process-acceptance` `Client.Connects` |"
                for number in range(1, 9)
            )
            page = root / "docs/testing.md"
            page.write_text(table, encoding="utf-8")
            self.assertEqual(docs.traceability_errors(root), [])
            page.write_text(
                table.replace("R8", "R7").replace("Client.Connects", "Client.Removed"),
                encoding="utf-8",
            )
            self.assertGreater(len(docs.traceability_errors(root)), 1)


if __name__ == "__main__":
    unittest.main()
