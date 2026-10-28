#!/usr/bin/env python3
"""Package a native Linux build as one AppImage and a matching portable archive.

The stage contains a strict file manifest, bundled ELF dependencies, notices,
and the launcher. Game data, settings, caches and saves are never imported.
linuxdeploy handles relocatable ELF dependency paths; the AppRun script and
resource layout are shared by both download formats. No root access is used.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import pathlib
import re
import shutil
import stat
import subprocess
import tarfile

PACKAGE = "Desktop-Melee-Linux-x86_64"
FORBIDDEN = {".iso", ".gcm", ".rvz", ".ciso", ".gcz", ".wia", ".dol", ".wbfs", ".wad"}


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def record(path: pathlib.Path) -> dict:
    return {"name": path.name, "bytes": path.stat().st_size, "sha256": sha256(path)}


def git(root: pathlib.Path, *args: str) -> str:
    return subprocess.run(["git", *args], cwd=root, check=True,
                          capture_output=True, text=True).stdout.strip()


def scan(stage: pathlib.Path) -> dict:
    records = {}
    for path in sorted(stage.rglob("*")):
        name = path.relative_to(stage).as_posix()
        if path.is_symlink():
            raise SystemExit("Package contains an unflattened link: " + name)
        if path.is_dir():
            continue
        if not path.is_file():
            raise SystemExit("Package contains a special file: " + name)
        if (path.suffix.lower() in FORBIDDEN or
                any(part.lower() in {"user", ".git", ".ssh"} for part in path.relative_to(stage).parts) or
                path.name.lower() in {"settings.ini", "settings.conf", "desktop-melee.log", "portable.txt"}):
            raise SystemExit("Disc or personal data must never enter the package: " + name)
        if any(ord(ch) < 32 for ch in name) or "\\" in name:
            raise SystemExit("Unsafe package filename: " + name)
        if name == "BUILD-MANIFEST.json":
            continue
        records[name] = {"bytes": path.stat().st_size, "sha256": sha256(path)}
    return records


def flatten_links(stage: pathlib.Path) -> None:
    for path in sorted(stage.rglob("*")):
        if path.is_symlink():
            target = path.resolve(strict=True)
            if not target.is_relative_to(stage) or not target.is_file():
                raise SystemExit("Package link must resolve to an internal regular file: " + str(path))
            mode = stat.S_IMODE(target.stat().st_mode)
            data = target.read_bytes()
            path.unlink()
            path.write_bytes(data)
            path.chmod(mode)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path, required=True)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    parser.add_argument("--linuxdeploy", type=pathlib.Path, required=True)
    parser.add_argument("--appimagetool", type=pathlib.Path, required=True)
    parser.add_argument("--runtime", type=pathlib.Path, required=True)
    parser.add_argument("--runtime-dir", type=pathlib.Path, action="append", default=[])
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    version = (root / "DESKTOP_VERSION").read_text().strip()
    preview_text = (root / "DESKTOP_LINUX_PREVIEW").read_text().strip()
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise SystemExit("Invalid DESKTOP_VERSION")
    if not re.fullmatch(r"[1-9][0-9]*", preview_text):
        raise SystemExit("Invalid DESKTOP_LINUX_PREVIEW")
    preview = int(preview_text)
    release_tag = f"desktop-v{version}-linux.{preview}"
    if pathlib.Path(git(root, "rev-parse", "--show-toplevel")).resolve() != root:
        raise SystemExit("Package from the project Git checkout")
    revision = git(root, "rev-parse", "HEAD")
    dirty = bool(git(root, "status", "--porcelain", "--untracked-files=normal"))
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for name in ("DesktopMelee", "melee"):
        binary = build / name
        if not binary.is_file() or binary.read_bytes()[:4] != b"\x7fELF":
            raise SystemExit("Required native Linux build output missing: " + str(binary))
    if b"FP PERTURB" in (build / "melee").read_bytes():
        raise SystemExit("Refusing to package a determinism perturbation build")
    launcher_version = subprocess.run([str(build / "DesktopMelee"), "--version"],
        check=True, capture_output=True, text=True).stdout.strip()
    if launcher_version != f"Desktop Melee {version}-linux.{preview}":
        raise SystemExit("Compiled launcher version does not match the requested Linux release")
    engine_output = subprocess.run([str(build / "melee"), "--version"],
        check=True, capture_output=True, text=True).stdout
    # Aurora's early MEM1 allocation may log before main prints the version.
    # Require exactly the expected version line, independent of that log.
    engine_versions = [line.strip() for line in engine_output.splitlines()
                       if line.startswith("melee-pc ")]
    if engine_versions != [f"melee-pc {version}"]:
        raise SystemExit("Compiled game engine version does not match the requested Linux release")
    tool_lock = json.loads((root / "tools/linux/packaging-tools.json").read_text())["tools"]
    tools = {"linuxdeploy": args.linuxdeploy.resolve(), "appimagetool": args.appimagetool.resolve(),
             "runtime-x86_64": args.runtime.resolve()}
    for name, path in tools.items():
        if sha256(path) != tool_lock[name]["sha256"]:
            raise SystemExit("Packaging tool does not match its pinned SHA256: " + name)
    stage = output / PACKAGE
    if stage.is_symlink():
        raise SystemExit("Refusing a symlink staging directory")
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "usr/bin").mkdir(parents=True)
    (stage / "usr/lib").mkdir(parents=True)
    environment = os.environ.copy()
    environment["APPIMAGE_EXTRACT_AND_RUN"] = "1"
    environment["ARCH"] = "x86_64"
    library_search = [build / "_deps/sdl-build", *args.runtime_dir]
    for parent in (build / "_deps").glob("*-src"):
        library_search.extend([parent / "lib", parent / "lib64"])
    library_search = [path.resolve() for path in library_search if path.is_dir()]
    inherited = environment.get("LD_LIBRARY_PATH", "")
    environment["LD_LIBRARY_PATH"] = ":".join(map(str, library_search)) + (":" + inherited if inherited else "")
    deploy = [str(tools["linuxdeploy"]), "--appdir", str(stage),
              "--executable", str(build / "DesktopMelee"), "--executable", str(build / "melee"),
              "--desktop-file", str(root / "platforms/linux/DesktopMelee.desktop"),
              "--icon-file", str(root / "platforms/linux/melee.png"), "--icon-filename", "DesktopMelee",
              "--custom-apprun", str(root / "tools/linux/AppRun")]
    # SDL opens USB support dynamically; it is absent from ELF DT_NEEDED.
    usb = None
    for directory in [*library_search, *[pathlib.Path(x) for x in inherited.split(":") if x],
                      pathlib.Path("/usr/lib/x86_64-linux-gnu"), pathlib.Path("/lib/x86_64-linux-gnu")]:
        candidate = directory / "libusb-1.0.so.0"
        if candidate.is_file():
            usb = candidate.resolve()
            break
    if usb:
        deploy.extend(["--library", str(usb)])
    else:
        raise SystemExit("libusb-1.0.so.0 is required for the controller runtime package")
    subprocess.run(deploy, env=environment, check=True)
    shutil.copytree(root / "resources", stage / "usr/bin/resources")
    with gzip.open(root / "tools/initial_pipeline_cache.db.gz", "rb") as seed:
        (stage / "usr/bin/initial_pipeline_cache.db").write_bytes(seed.read())
    shutil.copy2(root / "docs/DESKTOP_LINUX.md", stage / "README.md")
    shutil.copy2(root / "DESKTOP_VERSION", stage / "DESKTOP_VERSION")
    shutil.copy2(root / "DESKTOP_LINUX_PREVIEW", stage / "DESKTOP_LINUX_PREVIEW")
    notices = stage / "licenses"
    notices.mkdir()
    for parent in [root / "licenses", root / "resources", root / "extern", root / "src/pc", build / "_deps"]:
        if not parent.is_dir():
            continue
        for path in parent.rglob("*"):
            if (path.is_file() and len(path.relative_to(parent).parts) <= 4 and
                    re.match(r"^(LICENSE|COPYING|NOTICE|FONT-LICENSE)([._-].*)?$", path.name, re.I)):
                name = "_".join(path.relative_to(root).parts) if path.is_relative_to(root) else "_".join(path.parts[-4:])
                shutil.copy2(path, notices / name)
    # Include copyright notices for the distro libraries linuxdeploy bundled.
    # Private build sysroots may be supplied through --runtime-dir.
    doc_roots = [pathlib.Path("/usr/share/doc")]
    for directory in [*args.runtime_dir, *[pathlib.Path(x) for x in inherited.split(":") if x]]:
        for ancestor in (directory, *directory.parents):
            if ancestor.name == "usr":
                doc_roots.append(ancestor / "share/doc")
                break
    for path in (stage / "usr/lib").iterdir():
        if not path.is_file():
            continue
        owner = subprocess.run(["dpkg-query", "-S", "*/" + path.name], capture_output=True, text=True)
        for line in owner.stdout.splitlines():
            package = line.split(":", 1)[0]
            if not re.fullmatch(r"[A-Za-z0-9+.-]+", package):
                continue
            for parent in doc_roots:
                notice = parent / package / "copyright"
                if notice.is_file():
                    shutil.copy2(notice, notices / ("system_" + package + "_copyright"))
                    break
    flatten_links(stage)
    # SDL dlopens the SONAME, rather than linking libusb in DT_NEEDED.
    # linuxdeploy copies an explicit --library by its real basename, which
    # can be libusb-1.0.so.0.4.0. Provide the runtime lookup name as a regular
    # file too, so controllers do not silently depend on the host's libusb.
    usb_alias = stage / "usr/lib/libusb-1.0.so.0"
    if not usb_alias.is_file():
        deployed_usb = stage / "usr/lib" / usb.name
        if not deployed_usb.is_file():
            raise SystemExit("linuxdeploy did not bundle the libusb controller runtime")
        shutil.copy2(deployed_usb, usb_alias)
    # appimagetool otherwise creates this symlink while compressing the
    # frozen stage. Supply a regular icon first, keeping both archive formats
    # identical and every file covered by the manifest.
    if not (stage / ".DirIcon").is_file():
        shutil.copy2(stage / "DesktopMelee.png", stage / ".DirIcon")
    records = scan(stage)
    metadata = {"format_version": 1, "package": PACKAGE, "desktop_version": version,
                "linux_preview": preview, "release_tag": release_tag,
                "source_revision": revision, "source_dirty": dirty,
                "desktop_repository": "https://github.com/Bluesguitartravis-droid/desktop-melee",
                "validation": "Native Linux build; real Steam Deck gameplay still needs testing with a user's disc.",
                "packaging_tools": {name: sha256(path) for name, path in tools.items()}, "files": records}
    manifest = stage / "BUILD-MANIFEST.json"
    manifest.write_text(json.dumps(metadata, indent=2) + "\n")
    payload_hash = sha256(manifest)
    archive = output / (PACKAGE + ".tar.gz")
    with tarfile.open(archive, "w:gz", format=tarfile.PAX_FORMAT) as packed:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                info = packed.gettarinfo(str(path), arcname=PACKAGE + "/" + path.relative_to(stage).as_posix())
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                with path.open("rb") as content:
                    packed.addfile(info, content)
    image = output / (PACKAGE + ".AppImage")
    subprocess.run([str(tools["appimagetool"]), "--no-appstream", "--runtime-file",
                    str(tools["runtime-x86_64"]), "--mksquashfs-opt=-processors",
                    "--mksquashfs-opt=4", str(stage), str(image)],
                   env=environment, check=True)
    image.chmod(0o755)
    if scan(stage) != records or sha256(manifest) != payload_hash:
        raise SystemExit("The package stage changed during archive creation")
    sidecar = {key: metadata[key] for key in ("format_version", "desktop_version", "linux_preview",
                                            "release_tag", "source_revision", "source_dirty")}
    sidecar.update({"appimage": record(image), "portable_archive": record(archive),
                    "payload_manifest_sha256": payload_hash})
    (output / (PACKAGE + ".manifest.json")).write_text(json.dumps(sidecar, indent=2) + "\n")
    print(json.dumps({"appimage": record(image), "archive": record(archive),
                      "payload_files": len(records) + 1, "source_dirty": dirty}, indent=2))


if __name__ == "__main__":
    main()
