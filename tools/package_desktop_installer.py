#!/usr/bin/env python3
"""Build the per-user Windows installer from a verified portable package.

Requires NSIS (makensis). The generated installer contains exactly the app
files recorded in BUILD-MANIFEST.json plus that manifest. A sidecar binds the
setup executable, portable ZIP, payload manifest and source revision for the
release packager. Local dirty builds are allowed, but cannot be published by
package_desktop_source.py. No downloads or disc data are used here.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import tempfile
import zipfile

from package_desktop_source import (
    INSTALLER_MANIFEST_NAME, INSTALLER_NAME, sha256, validate_binary, validate_member,
)


def nsis_literal(value: str) -> str:
    if any(ord(char) < 32 for char in value) or "${" in value:
        raise SystemExit("Unsafe NSIS literal")
    return value.replace("$", "$$").replace('"', '$\\"')


def validate_payload(stage: pathlib.Path, portable: pathlib.Path) -> tuple[dict, list[str]]:
    manifest_path = stage / "BUILD-MANIFEST.json"
    if manifest_path.is_symlink():
        raise SystemExit("Installer payload must not contain symbolic links")
    manifest_data = manifest_path.read_bytes()
    manifest = json.loads(manifest_data)
    version = manifest.get("desktop_version", "")
    if (not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version)
            or any(int(part) > 65535 for part in version.split("."))):
        raise SystemExit("Installer version must have three numeric components between 0 and 65535")
    if portable.name != "Desktop-Melee-Windows-x64.zip":
        raise SystemExit("Installer input must be Desktop-Melee-Windows-x64.zip")
    archived_manifest = validate_binary(portable, manifest.get("source_revision"), version,
                                        require_clean=False)
    if archived_manifest != manifest:
        raise SystemExit("Staged payload manifest differs from the portable archive")
    with zipfile.ZipFile(portable) as archive:
        if archive.read("Desktop-Melee-Windows-x64/BUILD-MANIFEST.json") != manifest_data:
            raise SystemExit("Staged manifest bytes differ from the portable archive")
    files = sorted([*manifest["files"], "BUILD-MANIFEST.json"])
    actual = set()
    for path in stage.rglob("*"):
        if path.is_symlink():
            raise SystemExit("Installer payload must not contain symbolic links")
        if path.is_file():
            actual.add(path.relative_to(stage).as_posix())
    if actual != set(files):
        raise SystemExit("Installer staging folder contains unlisted or missing files")
    for name, record in manifest["files"].items():
        validate_member(name)
        path = stage / name
        if path.stat().st_size != record["bytes"] or sha256(path) != record["sha256"]:
            raise SystemExit("Installer payload failed manifest verification: " + name)
    return manifest, files


def payload_include(stage: pathlib.Path, files: list[str]) -> str:
    """Exact file lists: no wildcards, recursive imports, or recursive deletes."""
    lines = ["; Generated from the verified portable package; do not edit.",
             "!macro DesktopCheckFiles Prefix"]
    for name in files:
        target = nsis_literal(name.replace("/", "\\"))
        lines += [f'    Push "$INSTDIR\\{target}"', "    Call ${Prefix}CheckWritable"]
    lines += ["!macroend", "", "!macro DesktopInstallFiles"]
    current_parent = None
    directories = set()
    for name in files:
        relative = pathlib.PurePosixPath(name)
        parent = str(relative.parent)
        if parent != current_parent:
            target = "" if parent == "." else "\\" + nsis_literal(parent.replace("/", "\\"))
            lines.append(f'    SetOutPath "$INSTDIR{target}"')
            current_parent = parent
        lines.append(f'    File "/oname={nsis_literal(relative.name)}" "{nsis_literal(str(stage / name))}"')
        directories.update(str(parent) for parent in relative.parents if str(parent) != ".")
    lines += ["!macroend", "", "!macro DesktopRemoveFiles"]
    for name in files:
        target = nsis_literal(name.replace("/", "\\"))
        lines += [f'    Push "$INSTDIR\\{target}"', "    Call un.DeleteOwnedFile"]
    for directory in sorted(directories, key=lambda name: (-name.count("/"), name)):
        target = nsis_literal(directory.replace("/", "\\"))
        lines.append(f'    RMDir "$INSTDIR\\{target}"')
    lines += ["!macroend", ""]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-dir", type=pathlib.Path, required=True)
    parser.add_argument("--portable-archive", type=pathlib.Path, required=True)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    parser.add_argument("--makensis", default="makensis")
    args = parser.parse_args()
    stage = args.package_dir.resolve()
    portable = args.portable_archive.resolve()
    manifest, files = validate_payload(stage, portable)
    portable_digest = sha256(portable)
    payload_digest = sha256(stage / "BUILD-MANIFEST.json")
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    installer = output / INSTALLER_NAME
    sidecar = output / INSTALLER_MANIFEST_NAME
    template = pathlib.Path(__file__).resolve().parent / "windows" / "desktop_installer.nsi"
    size_kib = (sum((stage / name).stat().st_size for name in files) + 1023) // 1024
    # Compile in an isolated directory; publish the complete executable only
    # after success. A failed compiler cannot leave a new valid-looking sidecar.
    sidecar.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix="desktop-melee-nsis-", dir=output) as directory:
        temporary = pathlib.Path(directory)
        include = temporary / "payload.nsh"
        include.write_text(payload_include(stage, files))
        built = temporary / INSTALLER_NAME
        subprocess.run([args.makensis, "-WX", "-V3",
                        "-DDESKTOP_VERSION=" + manifest["desktop_version"],
                        "-DDESKTOP_SETUP_OUTPUT=" + nsis_literal(str(built)),
                        "-DDESKTOP_PAYLOAD_INCLUDE=" + nsis_literal(str(include)),
                        "-DDESKTOP_SIZE_KIB=" + str(size_kib), str(template)], check=True)
        with built.open("rb") as executable:
            if executable.read(2) != b"MZ":
                raise SystemExit("NSIS did not produce a Windows executable")
        built.replace(installer)
    # Recheck the inputs in case another build changed a file during compression.
    current_manifest, _ = validate_payload(stage, portable)
    if (current_manifest != manifest or sha256(portable) != portable_digest
            or sha256(stage / "BUILD-MANIFEST.json") != payload_digest):
        raise SystemExit("Installer inputs changed during compilation; rebuild the installer")
    record = {
        "format_version": 1,
        "desktop_version": manifest["desktop_version"],
        "source_revision": manifest.get("source_revision"),
        "source_dirty": manifest.get("source_dirty"),
        "payload_manifest_sha256": payload_digest,
        "installer": {"name": installer.name, "bytes": installer.stat().st_size,
                      "sha256": sha256(installer)},
        "portable_archive": {"name": portable.name, "bytes": portable.stat().st_size,
                             "sha256": portable_digest},
        "installed_files": files,
    }
    sidecar.write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps({"installer": str(installer), "bytes": installer.stat().st_size,
                      "sha256": record["installer"]["sha256"], "payload_files": len(files),
                      "build_manifest": str(sidecar)}, indent=2))


if __name__ == "__main__":
    main()
