#!/usr/bin/env python3
"""Validate release identity/assets before granting a tag a public release."""

import hashlib
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]


def project_version():
    match = re.search(r"project\(obs-quick-paste VERSION (\d+\.\d+\.\d+) LANGUAGES", (ROOT / "CMakeLists.txt").read_text())
    if not match:
        raise ValueError("Cannot find the project's stable semantic version")
    version = match.group(1)
    tag = os.environ.get("RELEASE_TAG", "")
    if tag and tag != f"v{version}":
        raise ValueError(f"Tag {tag!r} does not match project version v{version}")
    return version


def verify_assets(directory, version):
    expected = {
        f"obs-quick-paste-{version}-windows-x64.exe",
        f"obs-quick-paste-{version}-windows-x64.zip",
        f"obs-quick-paste-{version}-macos-arm64.pkg",
        f"obs-quick-paste-{version}-macos-arm64.zip",
    }
    actual = {p.name for p in directory.iterdir()}
    if actual != expected:
        raise ValueError(f"Unexpected release assets: missing={expected - actual}, extra={actual - expected}")
    lines = []
    for name in sorted(expected):
        path = directory / name
        if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"Invalid or empty asset: {name}")
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        lines.append(f"{digest.hexdigest()}  {name}\n")
    (directory / "SHA256SUMS.txt").write_text("".join(lines), encoding="utf-8")


def main():
    version = project_version()
    if sys.argv[1:] == ["version"]:
        if output := os.environ.get("GITHUB_OUTPUT"):
            with open(output, "a", encoding="utf-8") as stream:
                stream.write(f"version={version}\n")
        print(version)
    elif len(sys.argv) == 3 and sys.argv[1] == "assets":
        verify_assets(Path(sys.argv[2]), version)
        print(f"Verified all four {version} assets and wrote SHA256SUMS.txt")
    else:
        raise ValueError("Usage: release.py version | assets DIRECTORY")


if __name__ == "__main__":
    main()
