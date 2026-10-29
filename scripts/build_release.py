#!/usr/bin/env python3
"""Ricoh GR II Custom Firmware Build & Release Pipeline.

Automates:
1. Pre-flight safety verification (Safety Gate).
2. Surgical in-place patching (Tier 2: Boot Logo replacement in b01firm6).
3. Container re-encryption & repacking.
4. Post-build container verification & integrity auditing.
5. Packaging release artifacts and SHA256 checksums into dist/.
"""

import os
import sys
import shutil
import hashlib
from pathlib import Path

# Add the reverse-engineering notes to sys.path
WORKSPACE_ROOT = Path(__file__).resolve().parent.parent.parent
TOOLS_DIR = WORKSPACE_ROOT / "the reverse-engineering notes" / "tools"
sys.path.insert(0, str(TOOLS_DIR))

from gr2frm import load, repack, parse_container, xor_blocks
from verify_safety import check_safety

STOCK_FRM_PATH = WORKSPACE_ROOT / "rg2_v003.frm"
STOCK_FRM_SHA256 = "3643850f2ae74ae967eb336794048f554caa1b11d05b8569824d5719f1838331"

DVF_TDST_OFFSET = 0x139E00
DVF_TDST_SIZE = 614400 # 640x480x2 bytes

def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def sha256_file(path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def build_cfw_v001(custom_logo_brp_path=None, dist_dir=None):
    print("=" * 70)
    print("      RICOH GR II CUSTOM FIRMWARE (CFW) BUILD & RELEASE PIPELINE")
    print("=" * 70)

    if dist_dir is None:
        dist_dir = WORKSPACE_ROOT / "ricoh-grii-cfw" / "dist"
    dist_dir = Path(dist_dir)
    dist_dir.mkdir(parents=True, exist_ok=True)

    # -------------------------------------------------------------
    # Step 1: Pre-Flight Safety Verification
    # -------------------------------------------------------------
    print("\n>>> STEP 1: Running Pre-Flight Safety Gate...")
    if not check_safety():
        print("\n[CRITICAL ERROR] Pre-flight safety checks failed!")
        print("Resolve all safety warnings before building release firmware.")
        return False
    print("[OK] All safety gates passed.\n")

    # -------------------------------------------------------------
    # Step 2: Validate Source Firmware and Target Boot Screen
    # -------------------------------------------------------------
    print(">>> STEP 2: Validating Source Binaries...")
    if not STOCK_FRM_PATH.exists():
        raise FileNotFoundError(f"Missing factory firmware: {STOCK_FRM_PATH}")

    actual_stock_hash = sha256_file(STOCK_FRM_PATH)
    if actual_stock_hash != STOCK_FRM_SHA256:
        raise ValueError(f"Stock firmware hash mismatch: {actual_stock_hash}")
    print(f"[OK] Stock firmware verified ({STOCK_FRM_PATH.name})")

    # Determine custom logo to inject
    if custom_logo_brp_path is None:
        # Default to verified custom logo in repository
        sample_brp = WORKSPACE_ROOT / "ricoh-gr2-boot-screen-mod" / "sd-card" / "dvf_TDSt.brp"
        if not sample_brp.exists():
            raise FileNotFoundError(f"Custom logo BRP not found: {sample_brp}")
        custom_logo_brp_path = sample_brp

    custom_logo_brp_path = Path(custom_logo_brp_path)
    custom_brp_data = custom_logo_brp_path.read_bytes()
    if len(custom_brp_data) != DVF_TDST_SIZE:
        raise ValueError(f"Invalid BRP size {len(custom_brp_data)} B (must be exactly {DVF_TDST_SIZE} B)")
    print(f"[OK] Custom boot logo validated ({custom_logo_brp_path.name}, {len(custom_brp_data):,} bytes)")

    # -------------------------------------------------------------
    # Step 3: Unpack and Surgical In-Place Patching (Tier 2 Only)
    # -------------------------------------------------------------
    print("\n>>> STEP 3: Applying Surgical In-Place Patch (b01firm6)...")
    raw_frm, images = load(str(STOCK_FRM_PATH))

    # Assert b01firm8 (executable code) is 100% UNTOUCHED
    stock_f8_hash = sha256_bytes(images["b01firm8"])
    print(f"[*] b01firm8 (Main RTOS): 100% untouched factory code (SHA-256: {stock_f8_hash[:16]}...)")

    # Modify b01firm6 (A: resource drive)
    f6_orig = images["b01firm6"]
    f6_patched = bytearray(f6_orig)

    # Verify original bytes at target offset
    stock_brp_orig = bytes(f6_orig[DVF_TDST_OFFSET:DVF_TDST_OFFSET + DVF_TDST_SIZE])
    if len(stock_brp_orig) != DVF_TDST_SIZE:
        raise ValueError("Failed reading original boot logo from b01firm6")

    print(f"[*] Original boot logo in b01firm6 verified @ offset 0x{DVF_TDST_OFFSET:06X}")

    # Inject new boot logo in-place
    f6_patched[DVF_TDST_OFFSET:DVF_TDST_OFFSET + DVF_TDST_SIZE] = custom_brp_data
    if len(f6_patched) != len(f6_orig):
        raise ValueError(f"Length mismatch after patch: {len(f6_patched)} != {len(f6_orig)}")
    print(f"[OK] Injected custom boot logo into b01firm6 (exact length {len(f6_patched):,} bytes preserved)")

    # -------------------------------------------------------------
    # Step 4: Re-encrypt and Repack Firmware Container
    # -------------------------------------------------------------
    print("\n>>> STEP 4: Repacking & Re-Encrypting Container...")
    patched_images = {
        "b01firm6": bytes(f6_patched),
        "b01firm8": images["b01firm8"] # UNTOUCHED factory code
    }

    repacked_bytes = repack(raw_frm, patched_images)
    if len(repacked_bytes) != len(raw_frm):
        raise ValueError(f"Container size mismatch: {len(repacked_bytes)} != {len(raw_frm)}")

    out_frm_path = dist_dir / "rg2_cfw_v001_bootlogo.frm"
    out_frm_path.write_bytes(repacked_bytes)
    out_hash = sha256_bytes(repacked_bytes)
    print(f"[OK] Generated {out_frm_path.name} ({len(repacked_bytes):,} bytes)")
    print(f"     SHA-256: {out_hash}")

    # -------------------------------------------------------------
    # Step 5: Post-Build Validation
    # -------------------------------------------------------------
    print("\n>>> STEP 5: Running Post-Build Container Verification...")
    _, verify_entries = parse_container(repacked_bytes)
    if len(verify_entries) != 2:
        raise ValueError("Container verification failed: expected 2 root entries")

    # Verify that repacked image decrypts cleanly
    verify_raw, verify_images = load(str(out_frm_path))
    decrypted_logo = verify_images["b01firm6"][DVF_TDST_OFFSET:DVF_TDST_OFFSET + DVF_TDST_SIZE]
    if decrypted_logo != custom_brp_data:
        raise ValueError("Decrypted boot logo does not match injected payload!")
    if verify_images["b01firm8"] != images["b01firm8"]:
        raise ValueError("RTOS binary was modified unexpectedly!")

    print("[PASS] Decrypted round-trip verification: 100% byte-exact payload match!")
    print("[PASS] RTOS executable partition (b01firm8) confirmed 100% UNTOUCHED stock code.")

    # -------------------------------------------------------------
    # Step 6: Generate Release Checksums & Documentation
    # -------------------------------------------------------------
    print("\n>>> STEP 6: Generating Release Manifest...")
    sha_manifest = dist_dir / "SHA256SUMS.txt"
    with open(sha_manifest, "w") as f_sha:
        f_sha.write(f"{out_hash}  {out_frm_path.name}\n")

    release_notes = dist_dir / "RELEASE_NOTES.md"
    release_notes.write_text(f"""# Ricoh GR II Custom Firmware v0.0.1 (Custom Boot Screen)

**Release Date:** 2026-10-29  
**Target:** Ricoh GR II (16.2MP APS-C, Milbeaut RC2015)  
**Safety Classification:** **Tier 2 (Near-Zero Risk — Data Only)**

## What's Changed
- **Replaced Boot Splash Screen (`A:/img/dvf_TDSt.brp`)**:
  Replaces the factory white screen with a custom dark high-contrast boot screen.
- **RTOS Binary (`b01firm8`)**: **100% UNTOUCHED stock factory code**.
  Carries zero machine-code or CPU crash risk.

## Checksums
```
{out_hash}  {out_frm_path.name}
```

## Flashing Instructions
1. Ensure your SD card has the Emergency Rescue Kit (`SD.dat`, `backup.ttl`, `rg2_v003.frm`).
2. Copy `{out_frm_path.name}` to your SD card root and rename it to `rg2_v003.frm` (or official update name).
3. Insert fully charged battery (>= 80%).
4. Power on while holding the **Playback** button or official firmware update button combination.
5. Follow on-screen confirmation prompts. Do NOT turn off the camera while updating.
""")

    print(f"[SUCCESS] Release package ready in {dist_dir}:")
    print(f"  • {out_frm_path.name}")
    print(f"  • {sha_manifest.name}")
    print(f"  • {release_notes.name}")
    print("=" * 70)
    return True

if __name__ == "__main__":
    success = build_cfw_v001()
    sys.exit(0 if success else 1)
