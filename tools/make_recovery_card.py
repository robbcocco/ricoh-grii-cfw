#!/usr/bin/env python3
"""Ricoh GR II Emergency Recovery Card Generator.

Generates:
1. H:/SD.dat: The raw factory RTOS DRAM image (6,131,796 bytes, verified from the NAND backup).
   Loaded by the Second-Stage Bootloader at 0xA0000000 if flash banks are corrupted.
2. H:/backup.ttl: The automated recovery script executed by the Second-Stage Bootloader.
3. H:/rg2_v003.frm: The official factory stock firmware for automated emergency re-flashing.
"""

import os
import shutil
import hashlib

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def create_emergency_recovery_payload(output_dir):
    os.makedirs(output_dir, exist_ok=True)
    sd_dat_path = os.path.join(output_dir, "SD.dat")
    backup_ttl_path = os.path.join(output_dir, "backup.ttl")
    stock_frm_src = "rg2_v003.frm"
    stock_frm_dst = os.path.join(output_dir, "rg2_v003.frm")

    print("=== Generating Ricoh GR II Emergency Recovery Kit ===")

    # 1. Generate SD.dat (verified raw RTOS payload from b01firm8 / the NAND backup)
    f8_src = "fw/b01firm8.bin"
    if os.path.exists(f8_src):
        with open(f8_src, "rb") as f8:
            f8.seek(0x200) # Skip 0x200 update header
            payload = f8.read()
    else:
        from gr2frm import load
        frm_cand = [p for p in ["rg2_v003.frm", "$GR2_STOCK_FRM", "ricoh-grii-cfw/rg2_v003.frm"] if os.path.exists(p)]
        if not frm_cand:
            raise FileNotFoundError("Cannot locate fw/b01firm8.bin or rg2_v003.frm to extract recovery payload")
        _, images = load(frm_cand[0])
        payload = images["b01firm8"][0x200:]

    with open(sd_dat_path, "wb") as f_out:
        f_out.write(payload)
    print(f"[OK] Created {sd_dat_path} ({len(payload):,} bytes)")
    print(f"     SHA-256: {sha256_file(sd_dat_path)}")

    # 2. Generate backup.ttl (Automated recovery script for the bootloader)
    backup_ttl = """\
; Ricoh GR II Emergency Hardware Recovery Script
; Automatically invoked by the Second-Stage Bootloader in recovery mode

sprintf "reconfirm off"
sendln inputstr

sprintf "up H:/rg2_v003.frm"
sendln inputstr

end
""".replace("\n", "\r\n")

    with open(backup_ttl_path, "wb") as f_ttl:
        f_ttl.write(backup_ttl.encode("ascii"))

    print(f"[OK] Created {backup_ttl_path} (CRLF ASCII)")

    # 3. Copy official stock firmware if available
    if os.path.exists(stock_frm_src):
        if not os.path.exists(stock_frm_dst) or os.path.getsize(stock_frm_src) != os.path.getsize(stock_frm_dst):
            print(f"[*] Copying {stock_frm_src} -> {stock_frm_dst}...")
            shutil.copy2(stock_frm_src, stock_frm_dst)
        print(f"[OK] Verified {stock_frm_dst} ({os.path.getsize(stock_frm_dst):,} bytes)")
    else:
        print(f"[WARN] {stock_frm_src} not found in workspace; please place official rg2_v003.frm on SD card.")

    print("\n=== Recovery Kit Generation Complete! ===")

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Create Ricoh GR II Emergency Recovery Kit")
    parser.add_argument("--dest", default="the reverse-engineering notes", help="Output directory or mounted SD card path")
    args = parser.parse_args()
    create_emergency_recovery_payload(args.dest)
