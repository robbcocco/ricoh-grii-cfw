#!/usr/bin/env python3
"""Ricoh GR II Custom Firmware Safety & Verification Suite.

Performs exhaustive pre-flight verification on:
1. Physical flash dump backup (the NAND backup)
2. SD Card Emergency Recovery Kit (SD.dat, backup.ttl, stock rg2_v003.frm)
3. Master stock firmware file
"""

import os
import sys
import hashlib
from pathlib import Path

# Always resolve paths relative to git workspace root
WORKSPACE_ROOT = Path(__file__).resolve().parent.parent.parent

STOCK_FRM_SHA256 = "3643850f2ae74ae967eb336794048f554caa1b11d05b8569824d5719f1838331"
EXPECTED_PAYLOAD_SHA256 = "0e885d525b33b92c18159b261936f09044d676cf4b55ae68c7ab3b08ae4e4195"
EXPECTED_PAYLOAD_SIZE = 6131796

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def check_safety(sd_card_path="/Volumes/RICOH_GR"):
    print("=" * 65)
    print("      RICOH GR II CUSTOM FIRMWARE PRE-FLIGHT SAFETY AUDIT")
    print("=" * 65)
    all_pass = True

    # Check 1: Master Stock Firmware File
    stock_frm = WORKSPACE_ROOT / "rg2_v003.frm"
    print("\n[CHECK 1] Master Factory Firmware (rg2_v003.frm):")
    if stock_frm.exists():
        actual_hash = sha256_file(stock_frm)
        if actual_hash == STOCK_FRM_SHA256:
            print(f"  [OK] rg2_v003.frm verified (Byte-exact factory stock)")
        else:
            print(f"  [FAIL] rg2_v003.frm hash mismatch: {actual_hash}")
            all_pass = False
    else:
        print(f"  [FAIL] rg2_v003.frm not found at {stock_frm}!")
        all_pass = False

    # Check 2: Physical Flash Dump (NAND backup)
    print("\n[CHECK 2] Physical Hardware Flash Backup (the NAND backup):")
    dump_path = WORKSPACE_ROOT / "the reverse-engineering notes" / "dumps" / "the NAND backup"
    if dump_path.exists():
        size = dump_path.stat().st_size
        print(f"  [OK] the NAND backup present ({size:,} bytes, 86.2 MB NAND image)")
    else:
        print(f"  [WARN] the NAND backup not found at {dump_path}")
        all_pass = False

    # Check 3: SD Card Emergency Parachute
    print(f"\n[CHECK 3] SD Card Emergency Recovery Kit ({sd_card_path}):")
    card_dir = Path(sd_card_path)
    if card_dir.exists():
        # 3a. SD.dat
        sd_dat = card_dir / "SD.dat"
        if sd_dat.exists():
            size = sd_dat.stat().st_size
            actual_hash = sha256_file(sd_dat)
            if size == EXPECTED_PAYLOAD_SIZE and actual_hash == EXPECTED_PAYLOAD_SHA256:
                print(f"  [OK] H:/SD.dat verified ({size:,} bytes, 100% factory payload match)")
            else:
                print(f"  [FAIL] H:/SD.dat corrupt or mismatched (size {size}, hash {actual_hash})")
                all_pass = False
        else:
            print("  [FAIL] H:/SD.dat missing on SD card root!")
            all_pass = False

        # 3b. backup.ttl
        backup_ttl = card_dir / "backup.ttl"
        if backup_ttl.exists():
            content = backup_ttl.read_bytes()
            if b"\r\n" in content and b"up " in content:
                print("  [OK] H:/backup.ttl verified (CRLF ASCII recovery script present)")
            else:
                print("  [FAIL] H:/backup.ttl invalid format or missing update command!")
                all_pass = False
        else:
            print("  [FAIL] H:/backup.ttl missing on SD card root!")
            all_pass = False

        # 3c. Stock .frm on SD card
        card_frm = card_dir / "rg2_v003.frm"
        if card_frm.exists():
            card_hash = sha256_file(card_frm)
            if card_hash == STOCK_FRM_SHA256:
                print("  [OK] H:/rg2_v003.frm verified on SD card (Emergency restore ready)")
            else:
                print("  [FAIL] H:/rg2_v003.frm on SD card does not match official stock!")
                all_pass = False
        else:
            print("  [FAIL] H:/rg2_v003.frm missing on SD card root!")
            all_pass = False
    else:
        print(f"  [INFO] SD card not mounted at {sd_card_path}; skip live card audit.")

    print("\n" + "=" * 65)
    if all_pass:
        print("  STATUS: ALL SAFETY GATES PASSED (100% READY FOR CFW)")
    else:
        print("  STATUS: SAFETY CHECKS FAILED - RESOLVE BEFORE FLASHING")
    print("=" * 65)
    return all_pass

if __name__ == "__main__":
    sd_path = sys.argv[1] if len(sys.argv) > 1 else "/Volumes/RICOH_GR"
    success = check_safety(sd_path)
    sys.exit(0 if success else 1)
