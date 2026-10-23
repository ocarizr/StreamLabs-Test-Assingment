"""Integration tests for release provenance using isolated real Git repositories."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

MODULE_PATH = Path(__file__).resolve().parents[2] / "scripts/release.py"
SPEC = importlib.util.spec_from_file_location("release", MODULE_PATH)
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class ReleaseProvenanceTests(unittest.TestCase):
    def setUp(self):
        # Create isolated Git history without modifying the project's repository.
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name)
        self.git("init", "--initial-branch=master")
        self.git("config", "user.name", "Release Tests")
        self.git("config", "user.email", "release-tests@example.invalid")
        self.git("-c", "commit.gpgsign=false", "commit", "--allow-empty", "-m", "fixture")
        self.commit = self.git("rev-parse", "HEAD")

        # Seed the remote-tracking ref and release tag used by each scenario.
        self.git("update-ref", "refs/remotes/origin/master", self.commit)
        self.git("tag", "V1.0")

    def git(self, *arguments):
        return subprocess.run(
            ["git", "-C", str(self.repo), *arguments],
            check=True,
            capture_output=True,
            text=True,
            timeout=10,
        ).stdout.strip()

    def test_lightweight_and_annotated_tags_on_master_are_allowed(self):
        self.assertEqual(release.verify_release(self.repo, "V1.0", self.commit), "1.0.0")
        self.git("-c", "tag.gpgsign=false", "tag", "-a", "V1.0.1", "-m", "fixture")

        self.assertEqual(release.verify_release(self.repo, "V1.0.1", self.commit), "1.0.1")

    def test_older_master_commit_is_allowed(self):
        self.git("-c", "commit.gpgsign=false", "commit", "--allow-empty", "-m", "next")
        self.git("update-ref", "refs/remotes/origin/master", self.git("rev-parse", "HEAD"))
        self.git("checkout", "--detach", self.commit)

        self.assertEqual(release.verify_release(self.repo, "V1.0", self.commit), "1.0.0")

    def test_branch_only_commit_cannot_be_published(self):
        self.git("checkout", "-b", "feature")
        self.git("-c", "commit.gpgsign=false", "commit", "--allow-empty", "-m", "feature")
        self.git("tag", "V2.0")

        with self.assertRaises(ValueError):
            release.verify_release(self.repo, "V2.0", self.git("rev-parse", "HEAD"))

    def test_tag_and_event_commit_must_match(self):
        with self.assertRaises(ValueError):
            release.verify_release(self.repo, "V1.0", "0" * 40)

    def test_checked_out_commit_must_match_tag(self):
        self.git("-c", "commit.gpgsign=false", "commit", "--allow-empty", "-m", "next")

        with self.assertRaises(ValueError):
            release.verify_release(self.repo, "V1.0", self.commit)

    def test_missing_master_ref_fails_closed(self):
        self.git("update-ref", "-d", "refs/remotes/origin/master")

        with self.assertRaises(ValueError):
            release.verify_release(self.repo, "V1.0", self.commit)


if __name__ == "__main__":
    unittest.main()
