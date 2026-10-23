"""Prevent accidental omissions or third-party analysis in the database selector."""

import importlib.util
import io
import json
from pathlib import Path
import re
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    "check_tidy", Path(__file__).resolve().parents[2] / "scripts/check_tidy.py"
)
check_tidy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(check_tidy)


class TranslationUnitSelectionTests(unittest.TestCase):
    def test_warning_totals_are_separated_from_actionable_diagnostics(self):
        output = (
            "163773 warnings generated.\n"
            "src/main.cpp:12:3: error: unsafe access [cppcoreguidelines-pro-bounds-pointer-arithmetic]\n"
            "2 warnings and 1 error generated.\n"
            "note: inspect this expression\n"
        )
        visible = check_tidy.actionable_output(output)
        self.assertNotIn("163773 warnings generated.", visible)
        self.assertIn("src/main.cpp:12:3: error: unsafe access", visible)
        self.assertIn("2 warnings and 1 error generated.", visible)
        self.assertIn("note: inspect this expression", visible)

    def test_compiler_warning_text_is_never_hidden(self):
        output = "src/main.cpp:1:1: warning: 12 warnings generated. [check]\n"
        self.assertEqual(check_tidy.actionable_output(output), output)

    def test_report_preserves_raw_counts_and_failing_analyzer_status(self):
        root = Path(__file__).resolve().parents[2]
        output = "163773 warnings generated.\nsrc/main.cpp:1: error: defect [check]\n"
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "compile_commands.json").write_text(json.dumps([
                {"file": str(root / "src/cli/application.cpp"), "directory": str(root)}
            ]), encoding="utf-8")
            responses = [
                mock.Mock(stdout="LLVM version 19.1.5"),
                mock.Mock(returncode=0),
                mock.Mock(returncode=1, stdout=output),
            ]
            with mock.patch("sys.argv", ["check_tidy", "--build", str(build)]), \
                    mock.patch.object(check_tidy.subprocess, "run", side_effect=responses), \
                    mock.patch("builtins.print") as printed, \
                    mock.patch("sys.stderr", new_callable=io.StringIO) as errors:
                with self.assertRaises(SystemExit) as failure:
                    check_tidy.main()
            self.assertEqual(failure.exception.code, 1)
            self.assertIn("clang-tidy failed", errors.getvalue())
            self.assertIn(output, (build / "clang-tidy.log").read_text(encoding="utf-8"))
            self.assertTrue(any("error: defect" in str(call) for call in printed.call_args_list))

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
