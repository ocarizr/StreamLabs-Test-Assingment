"""Prevent accidental omissions or third-party analysis in the database selector."""

import importlib.util
from pathlib import Path
import re
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "check_tidy", Path(__file__).resolve().parents[2] / "scripts/check_tidy.py"
)
check_tidy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(check_tidy)


class TranslationUnitSelectionTests(unittest.TestCase):
    def test_header_filter_accepts_both_separator_styles_and_excludes_dependencies(self):
        root = Path(__file__).resolve().parents[2]
        pattern = check_tidy.header_filter(root)
        for separator in ("/", "\\"):
            with self.subTest(separator=separator):
                header = (root / "include/pipeforge/application.hpp").as_posix().replace("/", separator)
                dependency = (root / "build/_deps/other/include/pipeforge/other.hpp").as_posix().replace("/", separator)
                self.assertIsNotNone(re.search(pattern, header))
                self.assertIsNone(re.search(pattern, dependency))

    def test_maintained_sources_are_selected_and_duplicates_removed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            source = root / "src/cli/application.cpp"
            database = [
                {"file": str(source), "directory": str(root)},
                {"file": "src/cli/application.cpp", "directory": str(root)},
                {"file": "apps/client/main.cpp", "directory": str(root)},
                {"file": "tests/unit/application_tests.cpp", "directory": str(root)},
            ]
            self.assertEqual(
                check_tidy.translation_units(database, root),
                sorted((source, root / "apps/client/main.cpp", root / "tests/unit/application_tests.cpp")),
            )

    def test_upstream_legacy_generated_and_external_sources_are_excluded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            database = [
                {"file": source, "directory": str(root)} for source in (
                    "build/_deps/googletest-src/src/gtest-all.cc", "build/generated/message.pb.cc",
                    "Client/Client.cpp", "Server/Server.cpp", "include/pipeforge/application.hpp",
                    "../external/src/other.cpp",
                )
            ]
            self.assertEqual(check_tidy.translation_units(database, root), [])


if __name__ == "__main__":
    unittest.main()
