#!/usr/bin/env python3
"""Decode the M10 probe (loader/flash/probe/Startup.ttl) copied off the card.

BOOT_PROF.BIN = 0x168 B from 0xA00C22E4: the APP's section-load profile, one
0x18-byte record per section n (count, total ms, max, min, start ms; TCM helpers
0x2004/0x2094/0x20A8 around the section load in FUN_a0022e28). LDR_STATE.BIN =
the flashed loader's status word (0xFFFFFFFF on a stock APP).

Prints the boot timeline and, given the CFW task start from the debug log
("task: running, systime 0x..."), how much earlier the flashed loader could
start each phase of the CFW than Startup.ttl (JobSystem step 13) does.

Usage: python3 scripts/loader_timing.py DIR [--task-start 0x7D0]
"""
import argparse
import struct
import sys
from pathlib import Path

PROF_BASE, REC, NREC = 0xA00C22E4, 0x18, 15
STATES = {0: "loader never ran", 1: "SKIP (half-press)", 2: "NOMOUNT", 3: "NOFILE", 4: "NOABI (stage 1 without "
          "loader ABI)", 5: "ARMED", 6: "disarmed by stage 1", 0xFFFFFFFF: "stock APP (no loader)"}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dir", type=Path)
    ap.add_argument("--task-start", type=lambda s: int(s, 0),
                    help="CFW task start systime (ms) from cfw_log.txt of the same boot")
    a = ap.parse_args(argv)

    prof = (a.dir / "BOOT_PROF.BIN").read_bytes()
    if len(prof) != REC * NREC:
        print(f"[FAIL] BOOT_PROF.BIN is {len(prof)} B, expected 0x{REC * NREC:X}")
        return 1
    state = a.dir / "LDR_STATE.BIN"
    if state.exists() and len(state.read_bytes()) == 4:
        v, = struct.unpack(">I", state.read_bytes())
        print(f"ldr_state 0x{v:08X}: {STATES.get(v, 'unknown')}")

    rows = []
    for n in range(1, NREC):
        count, total, mx, mn, start = struct.unpack_from(">5I", prof, n * REC)
        if count:
            rows.append((start, n, count, total))
    if not rows:
        print("[FAIL] no section loads recorded")
        return 1
    rows.sort()
    print(" n   start ms   load ms   end ms")
    for start, n, count, total in rows:
        print(f"{n:2d} {start:9d} {total:9d} {start + total:8d}" + ("" if count == 1 else f"   ({count} loads)"))
    first = rows[0][0]
    last_end = max(s + t for s, _, _, t in rows)
    loads = sum(t for _, _, _, t in rows)
    print(f"flashed-loader hook (first section load) at {first} ms; all sections loaded at {last_end} ms "
          f"({loads} ms of section loading)")
    order = [n for _, n, _, _ in rows]
    if 5 in order and order.index(5) + 1 < len(order):
        _, nxt, _, _ = rows[order.index(5) + 1]
        print(f"IQ RAM ready (ISP init after section 5) by the hook after section {nxt}: "
              f"{rows[order.index(5) + 1][0] + rows[order.index(5) + 1][3]} ms")
    if a.task_start is not None:
        print(f"Startup.ttl CFW task start {a.task_start} ms")
        print(f"  gain if stage 1 runs at the first hook:       {a.task_start - first} ms")
        print(f"  gain if the task starts after the last section: {a.task_start - last_end} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
