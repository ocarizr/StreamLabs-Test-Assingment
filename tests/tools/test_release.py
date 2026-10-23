"""Unit tests for release version parsing and runtime archive validation."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

MODULE_PATH = Path(__file__).resolve().parents[2] / "scripts/release.py"
SPEC = importlib.util.spec_from_file_location("release", MODULE_PATH)
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class ReleaseVersionTests(unittest.TestCase):
    def test_supported_tags_are_normalized(self):
        for tag, expected in (("V1.0", "1.0.0"), ("V1.0.0", "1.0.0"), ("V1.0.1", "1.0.1"), ("v2.4.9", "2.4.9")):
            with self.subTest(tag=tag):
                self.assertEqual(release.normalized_version(tag), expected)

    def test_unsupported_tags_fail_before_building(self):
        for tag in ("1.0.0", "V1", "V01.0", "V1.0.01", "V1.0.0-rc1", "V1.0.0/other", "V1.0\n", "V1.0;exit", ""):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.normalized_version(tag)


class ReleaseArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.archive = self.directory / "package.zip"

    def write_archive(self, *names):
        with zipfile.ZipFile(self.archive, "w") as package:
            for name in (*names, "THIRD-PARTY-NOTICES.txt"):
                package.writestr(name, b"runtime fixture")

    def test_client_and_bundle_have_exact_runtime_manifests(self):
        self.write_archive("pipeforge-client.exe")
        release.validate_archive(self.archive, ("client",))
        self.write_archive("pipeforge-client.exe", "pipeforge-server.exe")
        release.validate_archive(self.archive, ("client", "server"))

    def test_source_debug_symbols_tests_and_paths_are_rejected(self):
        for extra in ("source.cpp", "client.pdb", "gtest.dll", "../pipeforge-client.exe", "bin/pipeforge-server.exe"):
            with self.subTest(extra=extra):
                self.write_archive("pipeforge-client.exe", extra)
                with self.assertRaises(ValueError):
                    release.validate_archive(self.archive, ("client",))

    def test_missing_runtime_is_rejected(self):
        self.write_archive("pipeforge-client.exe")
        with self.assertRaises(ValueError):
            release.validate_archive(self.archive, ("client", "server"))

    def test_debug_build_is_not_packaged(self):
        (self.directory / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            release.package_runtime(self.directory, self.directory / "out", "V1.0")

    def test_asset_names_reject_unvalidated_tags(self):
        with self.assertRaises(ValueError):
            release.archive_name("client", "V1.0/other")


if __name__ == "__main__":
    unittest.main()
