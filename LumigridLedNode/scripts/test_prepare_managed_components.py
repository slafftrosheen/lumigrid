"""Preservation and integrity regressions; run with the configured ESP-IDF Python."""
import contextlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import prepare_managed_components as managed


NODE = Path(__file__).resolve().parents[1]
REPO = NODE.parent


class ManagedComponentTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.node = Path(self.temporary.name)
        self.component = self.node / "managed_components" / managed.COMPONENT
        self.component.mkdir(parents=True)

    def checkout(self):
        prefix = f"LumigridLedNode/managed_components/{managed.COMPONENT}/"
        tracked = subprocess.check_output(["git", "ls-files", "-z", "--", prefix],
                                          cwd=REPO, text=True).split("\0")
        for path in filter(None, tracked):
            target = self.component / path[len(prefix):]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(REPO / path, target)
        shutil.copyfile(NODE / "dependencies.lock", self.node / "dependencies.lock")
        self.assertEqual(managed.component_hash(self.component), managed.CHECKOUT_HASH)

    def write_lock(self, digest):
        lock = {"dependencies": {"espressif/cjson": {"component_hash": digest,
                "version": managed.REGISTRY_VERSION, "source": {"type": "service"}}}}
        (self.node / "dependencies.lock").write_text(json.dumps(lock))

    def complete(self, files=None):
        (self.component / "idf_component.yml").write_text(json.dumps({"files": files or {}}))
        (self.component / "source.c").write_text("original\n")
        (self.component / ".git").write_text("gitdir: upstream\n")
        (self.component / "ignored").write_text("fixture\n")
        digest = managed.component_hash(self.component)
        (self.component / ".component_hash").write_text(digest)
        self.write_lock(digest)

    def test_known_checkout_is_preserved_in_unique_backups(self):
        self.checkout()
        before = {p.relative_to(self.component): p.read_bytes()
                  for p in self.component.rglob("*") if p.is_file()}
        with contextlib.redirect_stdout(io.StringIO()):
            backup = managed.prepare(self.node)
            self.assertFalse(self.component.exists())
            self.assertEqual(before, {p.relative_to(backup): p.read_bytes()
                                     for p in backup.rglob("*") if p.is_file()})
            self.assertIsNone(managed.prepare(self.node))
            shutil.copytree(backup, self.component)
            second = managed.prepare(self.node)
        self.assertNotEqual(backup, second)
        self.assertTrue(backup.is_dir())
        self.assertTrue(second.is_dir())

    def test_modified_or_added_files_are_preserved_and_rejected(self):
        self.checkout()
        for target in ("cJSON/cJSON.c", "cJSON/test/local-notes.txt"):
            with self.subTest(target=target):
                path = self.component / target
                original = path.read_bytes() if path.exists() else None
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"local changes\n")
                with self.assertRaises(ValueError):
                    managed.prepare(self.node)
                self.assertEqual(path.read_bytes(), b"local changes\n")
                self.assertFalse((self.node / ".pio").exists())
                if original is None:
                    path.unlink()
                else:
                    path.write_bytes(original)

    def test_changed_marker_or_lock_does_not_relocate_checkout(self):
        self.checkout()
        marker = self.component / ".component_hash"
        marker.write_text("0" * 64)
        with self.assertRaises(ValueError):
            managed.prepare(self.node)
        self.assertEqual(marker.read_text(), "0" * 64)
        marker.write_text(managed.REGISTRY_HASH)
        self.write_lock("0" * 64)
        with self.assertRaises(ValueError):
            managed.prepare(self.node)
        self.assertTrue(self.component.is_dir())
        self.assertFalse((self.node / ".pio").exists())

    def test_complete_package_is_unchanged_and_git_pointer_is_hashed(self):
        self.complete()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertIsNone(managed.prepare(self.node))
            managed.verify(self.node)
        (self.component / ".git").unlink()
        with self.assertRaises(ValueError):
            managed.verify(self.node)

    def test_verification_uses_manifest_exclusions_and_locked_hash(self):
        self.complete({"exclude": ["ignored"]})
        (self.component / "ignored").write_text("excluded change\n")
        with contextlib.redirect_stdout(io.StringIO()):
            managed.verify(self.node)
        (self.component / "source.c").write_text("modified source\n")
        # A replacement marker cannot authorize altered bytes against the lock.
        (self.component / ".component_hash").write_text(managed.component_hash(self.component))
        with self.assertRaises(ValueError):
            managed.prepare(self.node)
        with self.assertRaises(ValueError):
            managed.verify(self.node)


if __name__ == "__main__":
    unittest.main()
