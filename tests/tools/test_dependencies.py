"""Reject unpinned sources and incomplete security-service results."""

import importlib.util
import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "dependency_audit", ROOT / "scripts/check_dependencies.py"
)
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class DependencyAudit(unittest.TestCase):
    def setUp(self):
        self.source = (ROOT / "cmake/Dependencies.cmake").read_text(encoding="utf-8")
        self.dependencies = audit.inventory(self.source)

    def test_moving_git_ref_missing_hash_and_new_dependency_are_rejected(self):
        cases = (
            self.source.replace("GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723", "GIT_TAG main"),
            self.source.replace("URL_HASH SHA256=", "UNVERIFIED_SHA256="),
            self.source + "\nFetchContent_Declare(unknown\n GIT_TAG main\n)\n",
            self.source + "\nFetchContent_Declare(unknown GIT_TAG main)\n",
        )

        for source in cases:
            with self.subTest(source=source[-50:]), self.assertRaises(ValueError):
                audit.inventory(source)

    def test_results_preserve_findings_and_reject_missing_or_paginated_responses(self):
        complete = [{} for _ in self.dependencies]
        complete[0] = {"vulns": [{"id": "TEST-advisory"}]}

        with patch.object(
            audit, "urlopen", return_value=io.BytesIO(json.dumps({"results": complete}).encode())
        ):
            self.assertEqual(
                audit.query_advisories(self.dependencies)[0]["vulns"][0]["id"], "TEST-advisory"
            )

        for result in (
            {},
            {"results": []},
            {"results": [{"next_page_token": "more"}] * 5},
            {"results": [{"vulns": [{}]}] * 5},
        ):
            with self.subTest(result=result), patch.object(
                audit, "urlopen", return_value=io.BytesIO(json.dumps(result).encode())
            ), self.assertRaises(ValueError):
                audit.query_advisories(self.dependencies)


if __name__ == "__main__":
    unittest.main()
