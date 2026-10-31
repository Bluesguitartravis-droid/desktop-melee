#!/usr/bin/env python3
"""Prepare exact-commit source and checksums for a Desktop Melee release.

The Windows archive must already have passed package_desktop_windows.py.
Release packaging deliberately requires a clean checkout and a binary
manifest bound to this same commit and desktop version. No network is used.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import zipfile


FORBIDDEN_GAME_EXTENSIONS = {".iso", ".gcm", ".rvz", ".ciso", ".gcz", ".wia", ".dol", ".wbfs", ".wad"}
INSTALLER_NAME = "Desktop-Melee-Setup-x64.exe"
INSTALLER_MANIFEST_NAME = "Desktop-Melee-Setup-x64.manifest.json"


def git(root: pathlib.Path, *arguments: str) -> str:
    return subprocess.run(["git", *arguments], cwd=root, check=True,
                          capture_output=True, text=True).stdout.strip()


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_member(name: str) -> pathlib.PurePosixPath:
    relative = pathlib.PurePosixPath(name)
    if (not name or relative.is_absolute() or ".." in relative.parts or "\\" in name
            or str(relative) != name or any(ord(char) < 32 for char in name)
            or any(char in name for char in ':*?"<>|')
            or any(part.endswith((" ", ".")) for part in relative.parts)):
        raise SystemExit("Unsafe package member: " + name)
    if relative.suffix.lower() in FORBIDDEN_GAME_EXTENSIONS:
        raise SystemExit("Game data must never be packaged: " + name)
    if (relative.parts[0].lower() == "user" or relative.name.lower() == "settings.ini"
            or name.lower() in {"uninstall.exe", "build-manifest.json"}):
        raise SystemExit("Personal data or reserved file must never be packaged: " + name)
    return relative


def validate_binary(archive: pathlib.Path, commit: str | None, version: str,
                    *, require_clean: bool = True) -> dict:
    prefix = "Desktop-Melee-Windows-x64/"
    manifest_name = prefix + "BUILD-MANIFEST.json"
    with zipfile.ZipFile(archive) as package:
        manifest = json.loads(package.read(manifest_name))
        if (manifest.get("package") != "Desktop-Melee-Windows-x64"
                or manifest.get("desktop_version") != version
                or manifest.get("source_revision") != commit
                or (require_clean and manifest.get("source_dirty") is not False)):
            raise SystemExit("Windows package is not bound to this clean commit and DESKTOP_VERSION")
        records = manifest["files"]
        expected = {prefix + name for name in records} | {manifest_name}
        actual = {item.filename for item in package.infolist() if not item.is_dir()}
        if (actual != expected or len(actual) != len([item for item in package.infolist() if not item.is_dir()])
                or len({name.casefold() for name in expected}) != len(expected)):
            raise SystemExit("Windows archive contains unlisted or missing files")
        for required in ("DesktopMelee.exe", "melee.exe", "README.md", "DESKTOP_VERSION", "portable.txt"):
            if required not in records:
                raise SystemExit("Windows package is missing " + required)
        for name, record in records.items():
            validate_member(name)
            data = package.read(prefix + name)
            if len(data) != record["bytes"] or hashlib.sha256(data).hexdigest() != record["sha256"]:
                raise SystemExit("Windows archive failed manifest verification: " + name)
        if package.read(prefix + "DESKTOP_VERSION").decode().strip() != version:
            raise SystemExit("Packaged desktop version does not match the release")
    return manifest


def validate_installer(installer: pathlib.Path, sidecar: pathlib.Path,
                       binary: pathlib.Path, commit: str, version: str) -> None:
    manifest = json.loads(sidecar.read_text())
    if (manifest.get("format_version") != 1 or manifest.get("desktop_version") != version
            or manifest.get("source_revision") != commit or manifest.get("source_dirty") is not False):
        raise SystemExit("Installer is not bound to this clean source commit and version")
    for key, path in (("installer", installer), ("portable_archive", binary)):
        record = manifest.get(key, {})
        if (record.get("name") != path.name or record.get("bytes") != path.stat().st_size
                or record.get("sha256") != sha256(path)):
            raise SystemExit("Installer build manifest does not match " + path.name)
    with zipfile.ZipFile(binary) as package:
        payload = package.read("Desktop-Melee-Windows-x64/BUILD-MANIFEST.json")
    if manifest.get("payload_manifest_sha256") != hashlib.sha256(payload).hexdigest():
        raise SystemExit("Installer payload does not match the portable archive")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--binary-archive", type=pathlib.Path, required=True)
    parser.add_argument("--installer", type=pathlib.Path, required=True)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    if pathlib.Path(git(root, "rev-parse", "--show-toplevel")).resolve() != root:
        raise SystemExit("Release source must be packaged from its own Git checkout")
    version = (root / "DESKTOP_VERSION").read_text().strip()
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise SystemExit("DESKTOP_VERSION must be MAJOR.MINOR.PATCH")
    if args.tag != "desktop-v" + version:
        raise SystemExit("Release tag does not match DESKTOP_VERSION")
    commit = git(root, "rev-parse", "HEAD")
    if git(root, "status", "--porcelain", "--untracked-files=normal"):
        raise SystemExit("Release packaging requires a clean checkout; commit the intended source first")
    tag = subprocess.run(["git", "rev-parse", "--verify", "--quiet",
                          "refs/tags/" + args.tag + "^{commit}"], cwd=root,
                         capture_output=True, text=True)
    if tag.returncode == 0 and tag.stdout.strip() != commit:
        raise SystemExit("Existing release tag points to a different commit")

    binary = args.binary_archive.resolve()
    if binary.name != "Desktop-Melee-Windows-x64.zip":
        raise SystemExit("The Windows release asset must be named Desktop-Melee-Windows-x64.zip")
    validate_binary(binary, commit, version)
    installer = args.installer.resolve()
    if installer.name != INSTALLER_NAME:
        raise SystemExit("The setup release asset must be named " + INSTALLER_NAME)
    validate_installer(installer, installer.with_name(INSTALLER_MANIFEST_NAME), binary, commit, version)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if binary.parent != output or installer.parent != output:
        raise SystemExit("The Windows ZIP and installer must be in --output-dir with the other release files")
    source = output / f"Desktop-Melee-Source-{version}.zip"
    subprocess.run(["git", "archive", "--format=zip",
                    f"--prefix=Desktop-Melee-Source-{version}/",
                    "--output", str(source), commit], cwd=root, check=True)
    with zipfile.ZipFile(source) as archive:
        for item in archive.infolist():
            if pathlib.PurePosixPath(item.filename).suffix.lower() in FORBIDDEN_GAME_EXTENSIONS:
                source.unlink()
                raise SystemExit("Game data must never enter a source release: " + item.filename)
        prefix = f"Desktop-Melee-Source-{version}/"
        if archive.read(prefix + "DESKTOP_VERSION").decode().strip() != version:
            raise SystemExit("Committed source version does not match the Windows archive")
        if archive.testzip() is not None:
            raise SystemExit("Source archive failed its CRC check")
    files = (installer, binary, source)
    sums = output / "SHA256SUMS"
    sums.write_text("".join(f"{sha256(path)}  {path.name}\n" for path in files))
    (output / "RELEASE-NOTES.md").write_text(
        f"# Desktop Melee {version}\n\n"
        "Download and run **Desktop-Melee-Setup-x64.exe**. It installs for your "
        "Windows account without administrator access and adds a Start Menu "
        "shortcut, with an optional desktop shortcut. Updates reuse the same "
        "installation; uninstall keeps your saves, settings, and files you added.\n\n"
        "For a portable copy, extract **Desktop-Melee-Windows-x64.zip** and run "
        "**DesktopMelee.exe** inside the extracted folder. The source ZIP is for development.\n\n"
        "This Windows x64 development release includes the desktop overlay, "
        "moving window platforms, experimental wallpaper terrain, and a startup "
        "check for newer Desktop Melee releases. From this version forward, the "
        "launcher can download the official setup, verify its GitHub SHA-256 digest, "
        "install it silently, and relaunch Desktop Melee without opening a browser.\n\n"
        "Wallpaper contours now become connected slopes so fighters can follow "
        "hill outlines. Taskbar floor collision is resolved before bottom-screen "
        "KO checks, including downward throws. Wallpaper detection remains "
        "experimental and works best with clear landscape outlines.\n\n"
        "You need your own USA 1.02 Melee disc image. No game files are included.\n\n"
        "Automated checks exercise native game and collision logic, wallpaper "
        "geometry, update metadata, and Windows DLL dependencies. The Windows "
        "binaries are cross-compiled; this workflow does not run a real match "
        "or test controller behavior on Windows.\n\n"
        f"The source ZIP is the exact Git commit `{commit}` used for the build. "
        "`SHA256SUMS` contains the checksums of the setup EXE and both ZIP files.\n"
    )
    print(json.dumps({"tag": args.tag, "version": version, "commit": commit,
                      "assets": [str(installer), str(binary), str(source), str(sums)]}, indent=2))


if __name__ == "__main__":
    main()
