#!/usr/bin/env python3
"""Build the Native Mounts PoC EPF deterministically."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import zipfile

CONTENT = Path(__file__).resolve().parent
OUTPUT = CONTENT / "mod-native-mounts.epf"
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def safe_relative(value: str) -> str:
    path = Path(value)
    if not value or path.is_absolute() or ".." in path.parts or "\\" in value:
        raise ValueError(f"unsafe EPF member path: {value!r}")
    return path.as_posix()


def members() -> tuple[str, ...]:
    manifest = json.loads((CONTENT / "manifest.json").read_text())
    result = ["manifest.json", safe_relative(manifest["clientFrameXml"]["stockTocSource"])]
    result.extend(safe_relative(entry["source"]) for entry in manifest["content"])
    return tuple(result)


def build(output: Path) -> None:
    with zipfile.ZipFile(output, "w") as archive:
        for member in members():
            source = CONTENT / member
            if not source.is_file():
                raise FileNotFoundError(f"EPF member is missing: {member}")
            info = zipfile.ZipInfo(member, ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, source.read_bytes())


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.check:
        if not OUTPUT.is_file():
            raise SystemExit(f"missing generated package: {OUTPUT}")
        with tempfile.TemporaryDirectory(prefix="native-mounts-epf-") as directory:
            candidate = Path(directory) / OUTPUT.name
            build(candidate)
            if digest(candidate) != digest(OUTPUT):
                raise SystemExit("checked-in EPF is stale; run content/build_epf.py")
        print(f"verified {OUTPUT} ({digest(OUTPUT)})")
        return
    build(OUTPUT)
    print(f"built {OUTPUT} with {len(members())} members ({digest(OUTPUT)})")


if __name__ == "__main__":
    main()
