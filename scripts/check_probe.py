#!/usr/bin/env python3
"""Check M0 probe dumps (copied off the card by hand) before enabling the loader.

Expects BOOT_/SHUT_A0000000.BIN (0x20000 B each) and BOOT_/SHUT_POOL.BIN
(16 B from 0xA00C2BA0) in DIR. Passes when the CFW window 0xA0017000-0xA001EFFF
holds only 0xFF in both the boot and the shutdown dump.
"""
import struct
import sys
from pathlib import Path

DUMP_BASE = 0xA0000000
WINDOW = (0xA0017000, 0xA001F000)
POOL_BASE = 0xA00C2BA0


def check_dump(path):
    data = path.read_bytes()
    if len(data) != 0x20000:
        print(f"[FAIL] {path.name}: {len(data)} B, expected 0x20000")
        return False
    lo, hi = WINDOW[0] - DUMP_BASE, WINDOW[1] - DUMP_BASE
    dirty = [i for i in range(lo, hi) if data[i] != 0xFF]
    if dirty:
        print(f"[FAIL] {path.name}: {len(dirty)} non-0xFF bytes in window, "
              f"first at 0x{DUMP_BASE + dirty[0]:08X}")
        return False
    print(f"[OK]   {path.name}: window 0x{WINDOW[0]:08X}-0x{WINDOW[1] - 1:08X} untouched (all 0xFF)")
    return True


def show_pool(path):
    data = path.read_bytes()
    if len(data) < 12:
        print(f"[WARN] {path.name}: {len(data)} B, expected 16")
        return
    bump, heap = struct.unpack_from(">II", data, 0xA00C2BA4 - POOL_BASE)
    print(f"[INFO] {path.name}: pool bump ptr 0x{bump:08X}, GlobalHeap handle 0x{heap:08X}")


def main(directory):
    d = Path(directory)
    ok = True
    for stage in ("BOOT", "SHUT"):
        dump = d / f"{stage}_A0000000.BIN"
        if not dump.exists():
            print(f"[FAIL] {dump} missing")
            ok = False
            continue
        ok &= check_dump(dump)
        pool = d / f"{stage}_POOL.BIN"
        if pool.exists():
            show_pool(pool)
    print("PROBE PASSED: loader may be enabled" if ok else "PROBE FAILED: do not load code into the window")
    return ok


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: check_probe.py <folder copied from H:/cfw/probe>")
    sys.exit(0 if main(sys.argv[1]) else 1)
