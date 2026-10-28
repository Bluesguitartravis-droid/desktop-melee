#!/usr/bin/env python3
"""Exercise release provenance guards with isolated Git/source/ZIP fixtures.

The tiny executable payloads here are deliberately test data; this verifies
release metadata and source binding, not PE loading or Windows gameplay.
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

from package_desktop_installer import payload_include, validate_payload


SOURCE_TOOL = pathlib.Path(__file__).resolve().with_name("package_desktop_source.py")


class ReleaseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        (self.root / "tools").mkdir()
        shutil.copy2(SOURCE_TOOL, self.root / "tools" / SOURCE_TOOL.name)
        (self.root / ".gitignore").write_text("/dist/\n")
        (self.root / "DESKTOP_VERSION").write_text("0.1.6\n")
        (self.root / "README.md").write_text("Source fixture.\n")
        self.git("init", "--quiet")
        self.commit()
        (self.root / "dist").mkdir()

    def git(self, *arguments: str) -> str:
        return subprocess.run(["git", "-c", "user.name=Release test",
            "-c", "user.email=release-test@example.invalid", "-c", "core.hooksPath=/dev/null",
            *arguments], cwd=self.root, check=True, capture_output=True, text=True).stdout.strip()

    def commit(self) -> None:
        self.git("add", ".")
        self.git("commit", "--quiet", "-m", "Release fixture")
        self.revision = self.git("rev-parse", "HEAD")

    def binary(self, *, revision: str | None = None, version: str = "0.1.6",
               dirty: bool = False, extra: bool = False) -> None:
        files = {"DesktopMelee.exe": b"launcher fixture", "melee.exe": b"engine fixture",
                 "README.md": b"Extract and launch.", "portable.txt": b"",
                 "DESKTOP_VERSION": (version + "\n").encode()}
        manifest = {"package": "Desktop-Melee-Windows-x64", "desktop_version": version,
                    "source_revision": revision or self.revision, "source_dirty": dirty,
                    "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                              for name, data in files.items()}}
        portable = self.root / "dist/Desktop-Melee-Windows-x64.zip"
        manifest_data = json.dumps(manifest).encode()
        with zipfile.ZipFile(portable, "w") as package:
            for name, data in files.items():
                package.writestr("Desktop-Melee-Windows-x64/" + name, data)
            package.writestr("Desktop-Melee-Windows-x64/BUILD-MANIFEST.json", manifest_data)
            if extra:
                package.writestr("Desktop-Melee-Windows-x64/unlisted.txt", "Unexpected file")
        installer = self.root / "dist/Desktop-Melee-Setup-x64.exe"
        installer.write_bytes(b"setup fixture; metadata checks only")
        sidecar = {"format_version": 1, "desktop_version": version,
                   "source_revision": revision or self.revision, "source_dirty": dirty,
                   "payload_manifest_sha256": hashlib.sha256(manifest_data).hexdigest()}
        for key, path in (("installer", installer), ("portable_archive", portable)):
            sidecar[key] = {"name": path.name, "bytes": path.stat().st_size,
                            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        self.sidecar_path = self.root / "dist/Desktop-Melee-Setup-x64.manifest.json"
        self.sidecar_path.write_text(json.dumps(sidecar))

    def stage(self) -> pathlib.Path:
        with zipfile.ZipFile(self.root / "dist/Desktop-Melee-Windows-x64.zip") as archive:
            archive.extractall(self.root / "dist")
        return self.root / "dist/Desktop-Melee-Windows-x64"

    def change_sidecar(self, key: str, value: object) -> None:
        sidecar = json.loads(self.sidecar_path.read_text())
        sidecar[key] = value
        self.sidecar_path.write_text(json.dumps(sidecar))

    def package(self, tag: str = "desktop-v0.1.6") -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, "tools/package_desktop_source.py", "--tag", tag,
            "--binary-archive", "dist/Desktop-Melee-Windows-x64.zip",
            "--installer", "dist/Desktop-Melee-Setup-x64.exe", "--output-dir", "dist"],
            cwd=self.root, capture_output=True, text=True)

    def test_exact_source_and_checksums(self) -> None:
        self.binary()
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(self.root / "dist/Desktop-Melee-Source-0.1.6.zip") as source:
            self.assertEqual(source.read("Desktop-Melee-Source-0.1.6/README.md"), b"Source fixture.\n")
            self.assertFalse(any("/dist/" in name or "/.git/" in name for name in source.namelist()))
        lines = (self.root / "dist/SHA256SUMS").read_text().splitlines()
        self.assertEqual(len(lines), 3)
        for line in lines:
            digest, filename = line.split("  ")
            self.assertEqual(digest, hashlib.sha256((self.root / "dist" / filename).read_bytes()).hexdigest())

    def test_dirty_checkout_is_rejected(self) -> None:
        self.binary()
        (self.root / "README.md").write_text("Uncommitted change.\n")
        self.assertNotEqual(self.package().returncode, 0)

    def test_wrong_binary_commit_is_rejected(self) -> None:
        self.binary(revision="0" * 40)
        self.assertNotEqual(self.package().returncode, 0)

    def test_wrong_binary_version_is_rejected(self) -> None:
        self.binary(version="0.1.5")
        self.assertNotEqual(self.package().returncode, 0)

    def test_dirty_binary_is_rejected(self) -> None:
        self.binary(dirty=True)
        self.assertNotEqual(self.package().returncode, 0)

    def test_unlisted_binary_file_is_rejected(self) -> None:
        self.binary(extra=True)
        self.assertNotEqual(self.package().returncode, 0)

    def test_tag_version_mismatch_is_rejected(self) -> None:
        self.binary()
        self.assertNotEqual(self.package("desktop-v0.1.7").returncode, 0)

    def test_tag_at_different_commit_is_rejected(self) -> None:
        self.git("tag", "desktop-v0.1.6")
        (self.root / "README.md").write_text("Another commit.\n")
        self.commit()
        self.binary()
        self.assertNotEqual(self.package().returncode, 0)

    def test_game_container_in_source_is_rejected(self) -> None:
        (self.root / "test.iso").write_bytes(b"not a real game; forbidden suffix fixture")
        self.commit()
        self.binary()
        self.assertNotEqual(self.package().returncode, 0)
        self.assertFalse((self.root / "dist/Desktop-Melee-Source-0.1.6.zip").exists())

    def test_changed_installer_is_rejected(self) -> None:
        self.binary()
        (self.root / "dist/Desktop-Melee-Setup-x64.exe").write_bytes(b"different setup fixture")
        self.assertNotEqual(self.package().returncode, 0)

    def test_installer_wrong_commit_is_rejected(self) -> None:
        self.binary()
        self.change_sidecar("source_revision", "f" * 40)
        self.assertNotEqual(self.package().returncode, 0)

    def test_installer_dirty_source_is_rejected(self) -> None:
        self.binary()
        self.change_sidecar("source_dirty", True)
        self.assertNotEqual(self.package().returncode, 0)

    def test_installer_wrong_portable_is_rejected(self) -> None:
        self.binary()
        self.change_sidecar("portable_archive", {"name": "Desktop-Melee-Windows-x64.zip",
                                               "bytes": 1, "sha256": "0" * 64})
        self.assertNotEqual(self.package().returncode, 0)

    def test_installer_wrong_payload_is_rejected(self) -> None:
        self.binary()
        self.change_sidecar("payload_manifest_sha256", "0" * 64)
        self.assertNotEqual(self.package().returncode, 0)

    def test_installer_staging_verified_and_removal_is_exact(self) -> None:
        self.binary(dirty=True)
        stage = self.stage()
        manifest, files = validate_payload(stage, stage.with_suffix(".zip"))
        self.assertTrue(manifest["source_dirty"])
        generated = payload_include(stage, files)
        remove = generated.split("!macro DesktopRemoveFiles", 1)[1]
        self.assertEqual(remove.count("Call un.DeleteOwnedFile"), len(files))
        self.assertNotIn("/r", generated)
        self.assertNotIn("*", generated)
        self.assertNotIn("\\user", generated)
        self.assertNotIn("settings.ini", generated)
        for name in files:
            self.assertIn(f'Push "$INSTDIR\\{name}"', remove)

    def test_installer_staging_corruption_is_rejected(self) -> None:
        self.binary()
        stage = self.stage()
        (stage / "melee.exe").write_bytes(b"changed engine fixture")
        with self.assertRaises(SystemExit):
            validate_payload(stage, stage.with_suffix(".zip"))

    def test_installer_personal_data_is_rejected(self) -> None:
        self.binary()
        stage = self.stage()
        (stage / "user").mkdir()
        (stage / "user/savedata").write_bytes(b"personal file fixture")
        with self.assertRaises(SystemExit):
            validate_payload(stage, stage.with_suffix(".zip"))

    def test_installer_symlink_is_rejected(self) -> None:
        self.binary()
        stage = self.stage()
        (stage / "linked.txt").symlink_to(stage / "README.md")
        with self.assertRaises(SystemExit):
            validate_payload(stage, stage.with_suffix(".zip"))


if __name__ == "__main__":
    unittest.main()
