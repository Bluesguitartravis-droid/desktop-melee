#!/usr/bin/env python3
"""Package a compiled Desktop Melee build, checking its DLL dependency closure.

This does not download anything or include disc data. Provide Microsoft's
app-local VC runtime DLLs in --runtime-dir when the Dawn build requires them.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import zipfile


OS_DLLS = {
    "kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "shell32.dll",
    "ole32.dll", "oleaut32.dll", "ntdll.dll", "winmm.dll", "version.dll",
    "imm32.dll", "setupapi.dll", "bcrypt.dll", "bcryptprimitives.dll",
    "dxgi.dll", "d3d11.dll", "d3d12.dll", "dcomp.dll", "ucrtbase.dll",
    "ws2_32.dll", "crypt32.dll", "shlwapi.dll", "msvcrt.dll", "rpcrt4.dll",
    "userenv.dll", "cfgmgr32.dll", "dwmapi.dll", "uxtheme.dll", "powrprof.dll",
    "dbghelp.dll", "winhttp.dll", "iphlpapi.dll", "hid.dll", "comctl32.dll",
    "comdlg32.dll", "shcore.dll", "wintrust.dll", "normaliz.dll", "windowscodecs.dll",
}


def dependencies(path: pathlib.Path, objdump: str) -> list[str]:
    dump = subprocess.run([objdump, "-p", str(path)], check=True,
                          capture_output=True, text=True).stdout
    return sorted(set(re.findall(r"DLL Name:\s*(\S+)", dump)), key=str.lower)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path, required=True)
    parser.add_argument("--runtime-dir", type=pathlib.Path, action="append", default=[])
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    parser.add_argument("--package-name", default="Desktop-Melee-Windows-x64")
    parser.add_argument("--objdump", default="x86_64-w64-mingw32-objdump")
    parser.add_argument("--readme", type=pathlib.Path)
    parser.add_argument("--version", help="Must match DESKTOP_VERSION when present")
    args = parser.parse_args()
    if not re.fullmatch(r"Desktop-Melee-Windows-x64(?:-[A-Za-z0-9][A-Za-z0-9._-]*)?", args.package_name):
        parser.error("--package-name must be Desktop-Melee-Windows-x64 with an optional filename-safe suffix")
    root = pathlib.Path(__file__).resolve().parents[1]
    version_file = root / "DESKTOP_VERSION"
    checked_in_version = version_file.read_text().strip() if version_file.is_file() else None
    version = args.version or checked_in_version
    if version is not None and not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        parser.error("--version / DESKTOP_VERSION must be MAJOR.MINOR.PATCH")
    if args.version and checked_in_version and args.version != checked_in_version:
        parser.error("--version does not match DESKTOP_VERSION")
    git_root = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=root,
        capture_output=True, text=True)
    source_revision, source_dirty = None, None
    if git_root.returncode == 0 and pathlib.Path(git_root.stdout.strip()).resolve() == root:
        source_revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root,
            check=True, capture_output=True, text=True).stdout.strip()
        source_dirty = bool(subprocess.run(["git", "status", "--porcelain", "--untracked-files=normal"],
            cwd=root, check=True, capture_output=True, text=True).stdout)
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    stage = output / args.package_name

    for name in ("DesktopMelee.exe", "melee.exe"):
        if not (build / name).is_file():
            raise SystemExit(f"Required build output missing: {build / name}")
    if b"FP PERTURB" in (build / "melee.exe").read_bytes():
        raise SystemExit("Refusing to package a determinism perturbation build")

    # Only the selected, validated package staging directory is replaced.
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    for name in ("DesktopMelee.exe", "melee.exe"):
        shutil.copy2(build / name, stage / name)
    (stage / "portable.txt").write_text("")
    shutil.copytree(root / "resources", stage / "resources")
    seed = build / "initial_pipeline_cache.db"
    if seed.exists():
        shutil.copy2(seed, stage / seed.name)
    else:
        # The upstream packaging stage also inflates this seed. Generate it
        # here when packaging a clean CI build whose build root lacks it.
        with gzip.open(root / "tools" / "initial_pipeline_cache.db.gz", "rb") as packed:
            (stage / "initial_pipeline_cache.db").write_bytes(packed.read())
    if version is not None:
        (stage / "DESKTOP_VERSION").write_text(version + "\n")

    candidates: dict[str, pathlib.Path] = {}
    for directory in [build, *args.runtime_dir]:
        for path in directory.rglob("*"):
            if path.is_file() and path.suffix.lower() == ".dll":
                candidates.setdefault(path.name.lower(), path)
    imports: dict[str, list[str]] = {}
    queue = [stage / "DesktopMelee.exe", stage / "melee.exe"]
    missing: dict[str, list[str]] = {}
    while queue:
        binary = queue.pop()
        imports[binary.name] = dependencies(binary, args.objdump)
        for dep in imports[binary.name]:
            lower = dep.lower()
            if lower in OS_DLLS or lower.startswith(("api-ms-win-", "ext-ms-")):
                continue
            if any(p.name.lower() == lower for p in stage.iterdir()):
                continue
            source = candidates.get(lower)
            if source is None:
                missing.setdefault(dep, []).append(binary.name)
                continue
            dest = stage / dep
            shutil.copy2(source, dest)
            queue.append(dest)
    if missing:
        raise SystemExit("Missing runtime DLLs: " + json.dumps(missing, sort_keys=True))

    # Dawn loads these shader/compiler libraries at runtime, outside PE imports.
    for name in ("dxcompiler.dll", "dxil.dll"):
        source = candidates.get(name)
        if source is not None and not (stage / name).exists():
            dest = stage / name
            shutil.copy2(source, dest)
            imports[name] = dependencies(dest, args.objdump)
            gaps = [dep for dep in imports[name]
                    if dep.lower() not in OS_DLLS
                    and not dep.lower().startswith(("api-ms-win-", "ext-ms-"))
                    and not any(p.name.lower() == dep.lower() for p in stage.iterdir())]
            if gaps:
                raise SystemExit(f"Runtime-loaded {name} has missing imports: {gaps}")

    if args.readme:
        shutil.copy2(args.readme, stage / "README.md")
    notices = stage / "licenses"
    notices.mkdir()
    license_files = []
    for parent in [root / "licenses", root / "resources", root / "extern", root / "src" / "pc", build / "_deps", *args.runtime_dir]:
        if not parent.exists():
            continue
        for path in parent.rglob("*"):
            if (path.is_file() and len(path.relative_to(parent).parts) <= 4
                    and re.match(r"^(LICENSE|COPYING|NOTICE|FONT-LICENSE)([._-].*)?$", path.name, re.I)):
                label = "_".join(path.relative_to(root).parts) if path.is_relative_to(root) else "_".join(path.parts[-3:])
                dest = notices / label
                shutil.copy2(path, dest)
                license_files.append(dest.name)
    records = {}
    for path in sorted(stage.rglob("*")):
        if path.is_file():
            if path.suffix.lower() in {".iso", ".gcm", ".rvz", ".ciso", ".dol"}:
                raise SystemExit(f"Disc data must never enter the package: {path}")
            records[str(path.relative_to(stage))] = {
                "bytes": path.stat().st_size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
    (stage / "BUILD-MANIFEST.json").write_text(json.dumps({
        "format_version": 1,
        "package": args.package_name,
        "desktop_repository": "https://github.com/Bluesguitartravis-droid/desktop-melee",
        "desktop_version": version,
        "source_revision": source_revision,
        "source_dirty": source_dirty,
        "upstream_repository": "https://github.com/999sian/melee-pc",
        "upstream_revision": "0fb0af3304d3469308b5574369a6ec86f145632f",
        "validation": "Cross-compiled; PE imports checked. Gameplay requires a user's disc and Windows runtime testing.",
        "imports": imports,
        "files": records,
    }, indent=2) + "\n")
    archive = output / f"{args.package_name}.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                z.write(path, pathlib.Path(stage.name) / path.relative_to(stage))
    print(json.dumps({"archive": str(archive), "bytes": archive.stat().st_size,
                      "binaries_checked": len(imports), "license_files": len(license_files),
                      "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}, indent=2))


if __name__ == "__main__":
    main()
