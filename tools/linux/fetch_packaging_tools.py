#!/usr/bin/env python3
"""Fetch only the pinned public AppImage packaging tools, verifying before use."""
from __future__ import annotations
import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import urllib.request


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    lock = json.loads(pathlib.Path(__file__).with_name("packaging-tools.json").read_text())

    def fetch(item: tuple[str, dict]) -> str:
        name, record = item
        output = args.output_dir / name
        if output.exists() and hashlib.sha256(output.read_bytes()).hexdigest() == record["sha256"]:
            output.chmod(0o755)
            return name + ": cached SHA256 verified"
        request = urllib.request.Request(record["url"], headers={"User-Agent": "DesktopMelee-build"})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read(64 * 1024 * 1024 + 1)
        if len(data) > 64 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != record["sha256"]:
            raise SystemExit(f"Pinned SHA256 mismatch for {name}; review and update packaging-tools.json before upgrading build tools")
        temporary = output.with_suffix(".download")
        temporary.write_bytes(data)
        temporary.chmod(0o755)
        temporary.replace(output)
        return name + ": downloaded and SHA256 verified"

    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for line in pool.map(fetch, lock["tools"].items()):
            print(line)


if __name__ == "__main__":
    main()
