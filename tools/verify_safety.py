#!/usr/bin/env python3
"""Ricoh GR II pre-flight safety audit (local workspace only; never inspects the SD card).

Gates (all must pass):
1. Workspace stock firmware: rg2_v003.frm is byte-exact factory stock.
2. NAND backup: the maintainer's hardware data is the known 90,409,984-byte
   `fd -f` dump (covers IPL, BAK, APP, RES; not the B:/C:/D: FTL area).

The rescue-card requirements (finding 15 §10) are a manual checklist.
"""

import hashlib
import sys
from pathlib import Path

WORKSPACE_ROOT = Path(__file__).resolve().parent.parent.parent

STOCK_FRM_SHA256 = "3643850f2ae74ae967eb336794048f554caa1b11d05b8569824d5719f1838331"
FLASHDUMP_SHA256 = "00263e785a2043442c9116e6b83fc3df46af128e4e1fdaf550a0c52a184e1bab"
FLASHDUMP_SIZE = 90409984


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    return h.hexdigest()


def check_stock_frm():
    path = WORKSPACE_ROOT / "rg2_v003.frm"
    print(f"\n[GATE 1] Stock firmware ({path}):")
    if not path.exists():
        print("  [FAIL] missing")
        return False
    if sha256_file(path) != STOCK_FRM_SHA256:
        print("  [FAIL] sha256 does not match factory rg2_v003.frm")
        return False
    print("  [OK] byte-exact factory stock")
    return True


def check_flashdump():
    path = WORKSPACE_ROOT / "the reverse-engineering notes" / "dumps" / "the NAND backup"
    print(f"\n[GATE 2] NAND backup ({path}):")
    if not path.exists():
        print("  [FAIL] missing")
        return False
    if path.stat().st_size != FLASHDUMP_SIZE or sha256_file(path) != FLASHDUMP_SHA256:
        print("  [FAIL] size/sha256 differ from the recorded fd -f dump")
        return False
    print("  [OK] matches recorded dump (flash 0x0-0x5638BFF)")
    return True


def check_safety():
    print("=" * 65)
    print("      RICOH GR II PRE-FLIGHT SAFETY AUDIT")
    print("=" * 65)
    results = [check_stock_frm(), check_flashdump()]
    print("\n" + "=" * 65)
    print("  STATUS: ALL GATES PASSED" if all(results) else "  STATUS: FAILED - resolve before flashing or testing")
    print("=" * 65)
    return all(results)


if __name__ == "__main__":
    sys.exit(0 if check_safety() else 1)
