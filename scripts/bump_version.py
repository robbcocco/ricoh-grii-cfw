#!/usr/bin/env python3
"""SemVer version bumping utility for Ricoh GR II Custom Firmware.

Usage:
    python3 scripts/bump_version.py patch   # 0.1.0 -> 0.1.1
    python3 scripts/bump_version.py minor   # 0.1.0 -> 0.2.0
    python3 scripts/bump_version.py major   # 0.1.0 -> 1.0.0
    python3 scripts/bump_version.py 0.2.5   # sets explicit version
"""

import sys
import re
from pathlib import Path

VERSION_FILE = Path(__file__).resolve().parent.parent / "VERSION"

def parse_semver(ver_str):
    m = re.match(r"^v?(\d+)\.(\d+)\.(\d+)$", ver_str.strip())
    if not m:
        raise ValueError(f"Invalid SemVer string: {ver_str}")
    return int(m.group(1)), int(m.group(2)), int(m.group(3))

def bump_version(bump_type="patch"):
    if not VERSION_FILE.exists():
        current_ver = "0.1.0"
    else:
        current_ver = VERSION_FILE.read_text().strip()

    major, minor, patch = parse_semver(current_ver)

    if bump_type == "patch":
        new_ver = f"{major}.{minor}.{patch + 1}"
    elif bump_type == "minor":
        new_ver = f"{major}.{minor + 1}.0"
    elif bump_type == "major":
        new_ver = f"{major + 1}.0.0"
    else:
        # Explicit version string
        m, mi, p = parse_semver(bump_type)
        new_ver = f"{m}.{mi}.{p}"

    VERSION_FILE.write_text(new_ver + "\n")
    print(f"Bumped version: v{current_ver} -> v{new_ver}")
    return new_ver

if __name__ == "__main__":
    b_type = sys.argv[1] if len(sys.argv) > 1 else "patch"
    new_v = bump_version(b_type)
    # Output raw version on stdout if invoked in shell script
