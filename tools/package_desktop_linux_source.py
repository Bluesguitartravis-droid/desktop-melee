#!/usr/bin/env python3
"""Validate Linux runtime artifacts and package their exact committed source.

This release gate is separate from the Windows stable channel. It accepts a
Linux preview tag only, requires a clean checkout, checks every portable
payload file, and binds both downloadable artifacts to the same manifest.
It neither downloads tools nor runs an AppImage.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import tarfile
import zipfile


PACKAGE = "Desktop-Melee-Linux-x86_64"
FORBIDDEN_GAME_EXTENSIONS = {
    ".iso", ".gcm", ".rvz", ".ciso", ".gcz", ".wia", ".dol", ".wbfs", ".wad"
}


def git(root: pathlib.Path, *arguments: str) -> str:
    return subprocess.run(["git", *arguments], cwd=root, check=True,
                          capture_output=True, text=True).stdout.strip()


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_name(name: str, *, manifest: bool = False) -> pathlib.PurePosixPath:
    relative = pathlib.PurePosixPath(name)
    if (not name or relative.is_absolute() or ".." in relative.parts or "\\" in name
            or str(relative) != name or any(ord(char) < 32 for char in name)):
        raise SystemExit("Unsafe package member: " + name)
    if relative.suffix.lower() in FORBIDDEN_GAME_EXTENSIONS:
        raise SystemExit("Game data must never be packaged: " + name)
    if (any(part.lower() in {"user", ".git", ".ssh"} for part in relative.parts)
            or relative.name.lower() in {"settings.ini", "settings.conf", "desktop-melee.log"}
            or (relative.name == "BUILD-MANIFEST.json" and not manifest)):
        raise SystemExit("Personal data or reserved file must never be packaged: " + name)
    return relative


def validate_binary(archive: pathlib.Path, commit: str, version: str,
                    preview: int, *, require_clean: bool = True) -> bytes:
    prefix = PACKAGE + "/"
    with tarfile.open(archive, "r:gz") as package:
        members: dict[str, tarfile.TarInfo] = {}
        for member in package:
            if member.name == PACKAGE and member.isdir():
                continue
            if not member.name.startswith(prefix):
                raise SystemExit("Unexpected Linux package root: " + member.name)
            name = member.name[len(prefix):]
            validate_name(name, manifest=name == "BUILD-MANIFEST.json")
            if name in members:
                raise SystemExit("Duplicate Linux package member: " + name)
            if not member.isfile() and not member.isdir():
                raise SystemExit("Linux release contains a link or special file: " + name)
            members[name] = member
        manifest_member = members.get("BUILD-MANIFEST.json")
        if not manifest_member or not manifest_member.isfile() or manifest_member.size > 1024 * 1024:
            raise SystemExit("Missing or invalid Linux build manifest")
        stream = package.extractfile(manifest_member)
        assert stream is not None
        manifest_data = stream.read()
        manifest = json.loads(manifest_data)
        if (manifest.get("format_version") != 1 or manifest.get("package") != PACKAGE
                or manifest.get("desktop_version") != version
                or manifest.get("linux_preview") != preview
                or manifest.get("release_tag") != f"desktop-v{version}-linux.{preview}"
                or manifest.get("source_revision") != commit
                or (require_clean and manifest.get("source_dirty") is not False)):
            raise SystemExit("Linux package is not bound to this clean commit, version and preview")
        records = manifest.get("files")
        if not isinstance(records, dict):
            raise SystemExit("Linux build manifest has no file records")
        files = {name for name, member in members.items() if member.isfile()}
        if files != set(records) | {"BUILD-MANIFEST.json"}:
            raise SystemExit("Linux archive contains unlisted or missing files")
        for required in ("AppRun", "usr/bin/DesktopMelee", "usr/bin/melee", "README.md",
                         "DESKTOP_VERSION", "DESKTOP_LINUX_PREVIEW"):
            if required not in records:
                raise SystemExit("Linux package is missing " + required)
        for name, record in records.items():
            validate_name(name)
            member = members[name]
            if not isinstance(record, dict) or member.size != record.get("bytes"):
                raise SystemExit("Linux archive file size mismatch: " + name)
            stream = package.extractfile(member)
            assert stream is not None
            digest = hashlib.sha256()
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
            if digest.hexdigest() != record.get("sha256"):
                raise SystemExit("Linux archive file hash mismatch: " + name)
            if name in {"AppRun", "usr/bin/DesktopMelee", "usr/bin/melee"} and member.mode & 0o111 == 0:
                raise SystemExit("Linux application is not executable: " + name)
        stream = package.extractfile(members["DESKTOP_VERSION"])
        assert stream is not None
        if stream.read(64).decode().strip() != version:
            raise SystemExit("Packaged version does not match the release")
        stream = package.extractfile(members["DESKTOP_LINUX_PREVIEW"])
        assert stream is not None
        if stream.read(64).decode().strip() != str(preview):
            raise SystemExit("Packaged Linux preview does not match the release")
    return manifest_data


def validate_appimage(appimage: pathlib.Path, portable: pathlib.Path,
                      manifest_data: bytes, commit: str, version: str, preview: int) -> None:
    sidecar = json.loads(appimage.with_name(PACKAGE + ".manifest.json").read_text())
    if (sidecar.get("format_version") != 1 or sidecar.get("desktop_version") != version
            or sidecar.get("linux_preview") != preview
            or sidecar.get("release_tag") != f"desktop-v{version}-linux.{preview}"
            or sidecar.get("source_revision") != commit or sidecar.get("source_dirty") is not False):
        raise SystemExit("AppImage is not bound to this clean source commit, version and preview")
    for key, path in (("appimage", appimage), ("portable_archive", portable)):
        record = sidecar.get(key, {})
        if (record.get("name") != path.name or record.get("bytes") != path.stat().st_size
                or record.get("sha256") != sha256(path)):
            raise SystemExit("Linux artifact hash or size mismatch: " + path.name)
    if sidecar.get("payload_manifest_sha256") != hashlib.sha256(manifest_data).hexdigest():
        raise SystemExit("AppImage payload is not bound to the portable archive manifest")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--binary-archive", required=True, type=pathlib.Path)
    parser.add_argument("--appimage", required=True, type=pathlib.Path)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    if pathlib.Path(git(root, "rev-parse", "--show-toplevel")).resolve() != root:
        raise SystemExit("Source must be packaged from its own Git checkout")
    version = (root / "DESKTOP_VERSION").read_text().strip()
    preview_text = (root / "DESKTOP_LINUX_PREVIEW").read_text().strip()
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise SystemExit("DESKTOP_VERSION must be MAJOR.MINOR.PATCH")
    if not re.fullmatch(r"[1-9][0-9]*", preview_text):
        raise SystemExit("DESKTOP_LINUX_PREVIEW must be a positive integer")
    preview = int(preview_text)
    if args.tag != f"desktop-v{version}-linux.{preview}":
        raise SystemExit("Linux tag must match DESKTOP_VERSION and DESKTOP_LINUX_PREVIEW")
    commit = git(root, "rev-parse", "HEAD")
    if git(root, "status", "--porcelain", "--untracked-files=normal"):
        raise SystemExit("Release packaging requires a clean checkout")
    existing = subprocess.run(["git", "rev-parse", "--verify", "--quiet",
                              "refs/tags/" + args.tag + "^{commit}"], cwd=root,
                             capture_output=True, text=True)
    if existing.returncode == 0 and existing.stdout.strip() != commit:
        raise SystemExit("Existing Linux tag points to another commit")
    portable, appimage, output = args.binary_archive.resolve(), args.appimage.resolve(), args.output_dir.resolve()
    if portable.name != PACKAGE + ".tar.gz" or appimage.name != PACKAGE + ".AppImage":
        raise SystemExit("Linux artifact filenames must use the documented x86_64 names")
    if portable.parent != output or appimage.parent != output:
        raise SystemExit("Linux assets must be in --output-dir")
    manifest_data = validate_binary(portable, commit, version, preview)
    validate_appimage(appimage, portable, manifest_data, commit, version, preview)
    source = output / f"Desktop-Melee-Linux-Source-{version}.zip"
    prefix = f"Desktop-Melee-Linux-Source-{version}/"
    subprocess.run(["git", "archive", "--format=zip", f"--prefix={prefix}",
                    "--output", str(source), commit], cwd=root, check=True)
    try:
        with zipfile.ZipFile(source) as archive:
            for item in archive.infolist():
                if not item.is_dir():
                    validate_name(item.filename[len(prefix):])
            if (archive.read(prefix + "DESKTOP_VERSION").decode().strip() != version
                    or archive.read(prefix + "DESKTOP_LINUX_PREVIEW").decode().strip() != preview_text
                    or archive.testzip() is not None):
                raise SystemExit("Committed source identity or CRC check failed")
    except BaseException:
        source.unlink(missing_ok=True)
        raise
    assets = (appimage, portable, source)
    sums = output / "Desktop-Melee-Linux-SHA256SUMS"
    sums.write_text("".join(f"{sha256(path)}  {path.name}\n" for path in assets))
    (output / "LINUX-RELEASE-NOTES.md").write_text(
        f"# Desktop Melee {version} — Linux preview {preview}\n\n"
        "Native Linux x86_64 preview, intended for Steam Deck Desktop Mode and "
        "other composited X11 desktops. **The desktop overlay requires an X11 "
        "session. Current SteamOS defaults to Wayland; select the X11/Legacy "
        "desktop session in Steam's Developer settings before entering Desktop Mode.**\n\n"
        "Download **Desktop-Melee-Linux-x86_64.AppImage**, mark it executable "
        "in the file properties, and open it. No administrator access or "
        "SteamOS system modification is required. The portable tarball is an "
        "alternative; extract it and run `AppRun`.\n\n"
        "This preview adds transparent, click-through desktop presentation, "
        "a panel floor and moving window platforms. Wayland and Gaming Mode "
        "can use the ordinary game window; their desktop overlays and Linux "
        "wallpaper terrain are not implemented in this preview.\n\n"
        "You need your own local USA 1.02 Melee disc image. No game data is "
        "included or uploaded. The Linux launcher checks this repository for "
        "compatible Linux updates without blocking startup, then offers a "
        "download; installing an update remains explicit.\n\n"
        "The build runs native regressions, X11 integration checks, a software-Vulkan "
        "overlay lifecycle test and disc-free executable smoke checks. These checks do not certify "
        "Steam Deck hardware, actual matches, controller mappings or battery use.\n\n"
        "See [Linux setup and limitations](https://github.com/Bluesguitartravis-droid/desktop-melee/blob/"
        f"{commit}/docs/DESKTOP_LINUX.md) for details. "
        f"The source ZIP corresponds exactly to commit `{commit}`. "
        "`Desktop-Melee-Linux-SHA256SUMS` contains the three asset hashes. "
        "The Windows stable release remains separate.\n"
    )
    print(json.dumps({"tag": args.tag, "version": version, "preview": preview,
                      "commit": commit, "assets": [str(path) for path in (*assets, sums)]}, indent=2))


if __name__ == "__main__":
    main()
