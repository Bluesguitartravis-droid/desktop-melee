#!/usr/bin/env python3
"""Exercise Linux release provenance with isolated Git and archive fixtures.

Executable contents are deliberate fixture bytes. These tests check release
identity and archive integrity, not ELF execution or game performance.
"""

from __future__ import annotations

import hashlib
import io
import json
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

from verify_desktop_linux import verify_extracted


PACKAGE = "Desktop-Melee-Linux-x86_64"
SOURCE_TOOL = pathlib.Path(__file__).with_name("package_desktop_linux_source.py")


class LinuxReleaseTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        (self.root / "tools").mkdir()
        shutil.copy2(SOURCE_TOOL, self.root / "tools" / SOURCE_TOOL.name)
        (self.root / ".gitignore").write_text("/dist/\n")
        (self.root / "DESKTOP_VERSION").write_text("0.1.7\n")
        (self.root / "DESKTOP_LINUX_PREVIEW").write_text("1\n")
        (self.root / "README.md").write_text("Source fixture.\n")
        self.git("init", "--quiet")
        self.commit()
        (self.root / "dist").mkdir()
        self.portable = self.root / "dist" / (PACKAGE + ".tar.gz")
        self.appimage = self.root / "dist" / (PACKAGE + ".AppImage")
        self.sidecar = self.root / "dist" / (PACKAGE + ".manifest.json")

    def git(self, *arguments: str) -> str:
        return subprocess.run(["git", "-c", "user.name=Release test",
            "-c", "user.email=release-test@example.invalid", "-c", "core.hooksPath=/dev/null",
            *arguments], cwd=self.root, check=True, capture_output=True, text=True).stdout.strip()

    def commit(self) -> None:
        self.git("add", ".")
        self.git("commit", "--quiet", "-m", "Linux release fixture")
        self.revision = self.git("rev-parse", "HEAD")

    def binary(self, *, revision: str | None = None, version: str = "0.1.7",
               preview: int = 1, dirty: bool = False, extra: str | None = None,
               corrupt: bool = False, link: bool = False, executable: bool = True,
               packaged_preview: int | None = None) -> None:
        files = {"AppRun": b"launcher fixture", "usr/bin/DesktopMelee": b"UI fixture",
                 "usr/bin/melee": b"engine fixture", "README.md": b"Extract and launch.",
                 "DESKTOP_VERSION": (version + "\n").encode(),
                 "DESKTOP_LINUX_PREVIEW": (str(preview if packaged_preview is None else packaged_preview) + "\n").encode()}
        manifest = {"format_version": 1, "package": PACKAGE, "desktop_version": version,
                    "linux_preview": preview, "source_revision": revision or self.revision,
                    "release_tag": f"desktop-v{version}-linux.{preview}",
                    "source_dirty": dirty,
                    "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                              for name, data in files.items()}}
        manifest_data = json.dumps(manifest).encode()
        with tarfile.open(self.portable, "w:gz") as package:
            for name, data in {**files, "BUILD-MANIFEST.json": manifest_data}.items():
                if corrupt and name == "usr/bin/melee":
                    data = b"changed fixture"
                member = tarfile.TarInfo(PACKAGE + "/" + name)
                member.size = len(data)
                member.mode = 0o755 if executable and name in {"AppRun", "usr/bin/melee", "usr/bin/DesktopMelee"} else 0o644
                package.addfile(member, io.BytesIO(data))
            if extra:
                member = tarfile.TarInfo(PACKAGE + "/" + extra)
                member.size = 5
                package.addfile(member, io.BytesIO(b"extra"))
            if link:
                member = tarfile.TarInfo(PACKAGE + "/linked")
                member.type = tarfile.SYMTYPE
                member.linkname = "/etc/passwd"
                package.addfile(member)
        self.appimage.write_bytes(b"AppImage fixture; identity testing only")
        sidecar = {"format_version": 1, "desktop_version": version,
                   "linux_preview": preview, "source_revision": revision or self.revision,
                   "release_tag": f"desktop-v{version}-linux.{preview}",
                   "source_dirty": dirty,
                   "payload_manifest_sha256": hashlib.sha256(manifest_data).hexdigest()}
        for key, path in (("appimage", self.appimage), ("portable_archive", self.portable)):
            sidecar[key] = {"name": path.name, "bytes": path.stat().st_size,
                            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        self.sidecar.write_text(json.dumps(sidecar))

    def package(self, tag: str = "desktop-v0.1.7-linux.1") -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, "tools/package_desktop_linux_source.py", "--tag", tag,
            "--binary-archive", str(self.portable), "--appimage", str(self.appimage),
            "--output-dir", "dist"], cwd=self.root, capture_output=True, text=True)

    def extracted_fixture(self) -> pathlib.Path:
        stage = self.root / "dist/extracted"
        stage.mkdir()
        with tarfile.open(self.portable, "r:gz") as archive:
            for member in archive:
                # Only this test's regular-file fixture uses this helper.
                relative = pathlib.PurePosixPath(member.name).relative_to(PACKAGE)
                self.assertNotIn("..", relative.parts)
                self.assertTrue(member.isfile())
                path = stage / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                source = archive.extractfile(member)
                assert source is not None
                path.write_bytes(source.read())
                path.chmod(member.mode)
        return stage

    def test_exact_source_and_checksums(self) -> None:
        self.binary()
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(self.root / "dist/Desktop-Melee-Linux-Source-0.1.7.zip") as source:
            self.assertEqual(source.read("Desktop-Melee-Linux-Source-0.1.7/README.md"), b"Source fixture.\n")
            self.assertFalse(any("/dist/" in name or "/.git/" in name for name in source.namelist()))
        lines = (self.root / "dist/Desktop-Melee-Linux-SHA256SUMS").read_text().splitlines()
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
        self.binary(version="0.1.6")
        self.assertNotEqual(self.package().returncode, 0)

    def test_wrong_binary_preview_is_rejected(self) -> None:
        self.binary(preview=2)
        self.assertNotEqual(self.package().returncode, 0)

    def test_mismatched_packaged_preview_file_is_rejected(self) -> None:
        self.binary(packaged_preview=2)
        result = self.package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Packaged Linux preview does not match", result.stderr)

    def test_dirty_binary_is_rejected(self) -> None:
        self.binary(dirty=True)
        self.assertNotEqual(self.package().returncode, 0)

    def test_unlisted_file_is_rejected(self) -> None:
        self.binary(extra="unlisted.txt")
        self.assertNotEqual(self.package().returncode, 0)

    def test_traversal_is_rejected(self) -> None:
        self.binary(extra="../escape.txt")
        self.assertNotEqual(self.package().returncode, 0)

    def test_symlink_is_rejected(self) -> None:
        self.binary(link=True)
        self.assertNotEqual(self.package().returncode, 0)

    def test_corrupted_payload_is_rejected(self) -> None:
        self.binary(corrupt=True)
        self.assertNotEqual(self.package().returncode, 0)

    def test_nonexecutable_entrypoint_is_rejected(self) -> None:
        self.binary(executable=False)
        self.assertNotEqual(self.package().returncode, 0)

    def test_changed_appimage_is_rejected(self) -> None:
        self.binary()
        self.appimage.write_bytes(b"different bytes")
        self.assertNotEqual(self.package().returncode, 0)

    def test_wrong_manifest_binding_is_rejected(self) -> None:
        self.binary()
        sidecar = json.loads(self.sidecar.read_text())
        sidecar["payload_manifest_sha256"] = "f" * 64
        self.sidecar.write_text(json.dumps(sidecar))
        self.assertNotEqual(self.package().returncode, 0)

    def test_stable_and_wrong_preview_tags_are_rejected(self) -> None:
        self.binary()
        for tag in ("desktop-v0.1.7", "desktop-v0.1.7-linux.2", "desktop-v0.1.8-linux.1"):
            with self.subTest(tag=tag):
                self.assertNotEqual(self.package(tag).returncode, 0)

    def test_tag_at_another_commit_is_rejected(self) -> None:
        self.git("tag", "desktop-v0.1.7-linux.1")
        (self.root / "README.md").write_text("Another source commit.")
        self.commit()
        self.binary()
        self.assertNotEqual(self.package().returncode, 0)

    def test_game_image_in_source_is_rejected_and_zip_removed(self) -> None:
        (self.root / "test.iso").write_bytes(b"forbidden suffix fixture")
        self.commit()
        self.binary()
        self.assertNotEqual(self.package().returncode, 0)
        self.assertFalse((self.root / "dist/Desktop-Melee-Linux-Source-0.1.7.zip").exists())

    def test_extracted_appimage_matches_portable(self) -> None:
        self.binary(dirty=True)
        report = verify_extracted(self.extracted_fixture(), self.portable)
        self.assertEqual(report["verified_files"], 7)
        self.assertEqual(report["source_revision"], self.revision)
        self.assertTrue(report["source_dirty"])

    def test_extracted_appimage_corruption_is_rejected(self) -> None:
        self.binary()
        stage = self.extracted_fixture()
        (stage / "usr/bin/melee").write_bytes(b"altered binary")
        with self.assertRaises(SystemExit):
            verify_extracted(stage, self.portable)

    def test_extracted_appimage_extra_file_is_rejected(self) -> None:
        self.binary()
        stage = self.extracted_fixture()
        (stage / "unlisted").write_bytes(b"unexpected")
        with self.assertRaises(SystemExit):
            verify_extracted(stage, self.portable)

    def test_extracted_appimage_link_is_rejected(self) -> None:
        self.binary()
        stage = self.extracted_fixture()
        (stage / "usr/bin/melee").unlink()
        (stage / "usr/bin/melee").symlink_to("/etc/passwd")
        with self.assertRaises(SystemExit):
            verify_extracted(stage, self.portable)

    def test_extracted_appimage_missing_execution_bit_is_rejected(self) -> None:
        self.binary()
        stage = self.extracted_fixture()
        (stage / "AppRun").chmod(0o644)
        with self.assertRaises(SystemExit):
            verify_extracted(stage, self.portable)


if __name__ == "__main__":
    unittest.main()
