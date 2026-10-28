#!/usr/bin/env python3
"""Compare an actual AppImage's extracted payload with the portable release.

The AppImage is a locally built executable being verified, not an untrusted
download. Extraction executes its runtime only, in an isolated temporary
directory; the application is never launched by this check. No FUSE or root
access is required. Local dirty builds can be inspected; the separate source
release gate still requires a clean, exact-commit build.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import tempfile

from package_desktop_linux_source import sha256, validate_binary


def verify_extracted(stage: pathlib.Path, portable: pathlib.Path) -> dict:
    manifest_path = stage / "BUILD-MANIFEST.json"
    if manifest_path.is_symlink() or not manifest_path.is_file() or manifest_path.stat().st_size > 1024 * 1024:
        raise SystemExit("AppImage lacks a regular, bounded build manifest")
    data = manifest_path.read_bytes()
    manifest = json.loads(data)
    portable_data = validate_binary(portable, manifest["source_revision"],
        manifest["desktop_version"], manifest["linux_preview"], require_clean=False)
    if portable_data != data:
        raise SystemExit("AppImage and portable archive have different build manifests")
    files = {}
    for path in stage.rglob("*"):
        if path.is_symlink():
            raise SystemExit("AppImage contains a symbolic link: " + str(path.relative_to(stage)))
        if path.is_dir():
            continue
        if not path.is_file():
            raise SystemExit("AppImage contains a special file")
        files[path.relative_to(stage).as_posix()] = path
    expected = set(manifest["files"]) | {"BUILD-MANIFEST.json"}
    if set(files) != expected:
        raise SystemExit("AppImage contains unlisted or missing payload files")
    for name, record in manifest["files"].items():
        path = files[name]
        if path.stat().st_size != record["bytes"] or sha256(path) != record["sha256"]:
            raise SystemExit("AppImage payload differs from its manifest: " + name)
        if name in {"AppRun", "usr/bin/DesktopMelee", "usr/bin/melee"} and path.stat().st_mode & 0o111 == 0:
            raise SystemExit("AppImage entry point is not executable: " + name)
    return {"verified_files": len(files), "desktop_version": manifest["desktop_version"],
            "linux_preview": manifest["linux_preview"], "source_revision": manifest["source_revision"],
            "source_dirty": manifest["source_dirty"]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--appimage", type=pathlib.Path, required=True)
    parser.add_argument("--binary-archive", type=pathlib.Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="desktop-melee-appimage-check-") as temporary:
        result = subprocess.run([str(args.appimage.resolve()), "--appimage-extract"],
            cwd=temporary, capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise SystemExit("AppImage extraction failed: " + result.stderr[-4000:])
        stage = pathlib.Path(temporary) / "squashfs-root"
        print(json.dumps(verify_extracted(stage, args.binary_archive.resolve()), indent=2))


if __name__ == "__main__":
    main()
