#!/usr/bin/env python3
"""Ricoh GR II Custom Firmware Build & Release Pipeline.

Automates:
1. Pre-flight safety verification (Safety Gate).
2. Automated download & validation of stock firmware from official Ricoh CDN if absent.
3. Surgical in-place patching (Tier 2: Boot Logo replacement in b01firm6).
4. Container re-encryption & repacking.
5. Post-build container verification & integrity auditing.
6. Packaging release artifacts and SHA256 checksums into dist/.
"""

import os
import sys
import shutil
import hashlib
import zipfile
import io
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Check local tools/ first, fallback to parent the reverse-engineering notes
LOCAL_TOOLS = REPO_ROOT / "tools"
PARENT_TOOLS = REPO_ROOT.parent / "the reverse-engineering notes" / "tools"

if LOCAL_TOOLS.exists():
    sys.path.insert(0, str(LOCAL_TOOLS))
elif PARENT_TOOLS.exists():
    sys.path.insert(0, str(PARENT_TOOLS))
else:
    raise RuntimeError("Cannot locate tools/ directory")

from gr2frm import load, repack, parse_container, nupdate_csum, update_header
from verify_safety import check_safety

STOCK_ZIP_URL = "https://www.ricoh-imaging.co.jp/english/support/digital/firmware/rg2_v003.zip"
STOCK_ZIP_SHA256 = "76d28c8e48c37123f226b8aa6d236e23655146bc08070738d9e734290ea6b13d"
STOCK_FRM_SHA256 = "3643850f2ae74ae967eb336794048f554caa1b11d05b8569824d5719f1838331"

DVF_TDST_OFFSET = 0x139E00
DVF_TDST_SIZE = 614400 # 640x480x2 bytes
def get_version(override=None) -> str:
    if override:
        return override.lstrip("v")
    version_file = REPO_ROOT / "VERSION"
    if version_file.exists():
        return version_file.read_text().strip().lstrip("v")
    return "0.1.0"

def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def sha256_file(path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def ensure_stock_firmware() -> Path:
    """Finds or downloads the official stock rg2_v003.frm."""
    candidates = [
        REPO_ROOT / "rg2_v003.frm",
        REPO_ROOT.parent / "rg2_v003.frm",
        Path.cwd() / "rg2_v003.frm"
    ]
    for c in candidates:
        if c.exists() and sha256_file(c) == STOCK_FRM_SHA256:
            return c

    # Download from Ricoh official CDN
    target_path = REPO_ROOT / "rg2_v003.frm"
    print(f"[*] Stock firmware not found locally. Fetching official firmware from Ricoh CDN...")
    print(f"    URL: {STOCK_ZIP_URL}")
    req = urllib.request.Request(STOCK_ZIP_URL, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req) as resp:
        zip_data = resp.read()

    actual_zip_hash = sha256_bytes(zip_data)
    if actual_zip_hash != STOCK_ZIP_SHA256:
        raise ValueError(f"Downloaded ZIP checksum mismatch: {actual_zip_hash}")
    print(f"[OK] Downloaded official ZIP ({len(zip_data):,} bytes, verified SHA-256)")

    zf = zipfile.ZipFile(io.BytesIO(zip_data))
    frm_data = zf.read("rg2_v003/rg2_v003.frm")
    actual_frm_hash = sha256_bytes(frm_data)
    if actual_frm_hash != STOCK_FRM_SHA256:
        raise ValueError(f"Extracted FRM checksum mismatch: {actual_frm_hash}")

    target_path.write_bytes(frm_data)
    print(f"[OK] Extracted stock {target_path.name} ({len(frm_data):,} bytes)")
    return target_path

def build_cfw_v001(custom_logo_brp_path=None, dist_dir=None, skip_safety_gate=False, version=None):
    version = get_version(version)
    print("=" * 70)
    print(f"      RICOH GR II CUSTOM FIRMWARE (CFW v{version}) BUILD PIPELINE")
    print("=" * 70)
    if dist_dir is None:
        dist_dir = REPO_ROOT / "dist"
    dist_dir = Path(dist_dir)
    dist_dir.mkdir(parents=True, exist_ok=True)

    # -------------------------------------------------------------
    # Step 1: Pre-Flight Safety Verification
    # -------------------------------------------------------------
    print("\n>>> STEP 1: Running Pre-Flight Safety Gate (local workspace)...")
    if skip_safety_gate:
        print("[SKIPPED] Safety gate skipped (--skip-safety-gate / CI). Output must not be flashed before running verify_safety.py.\n")
    elif not check_safety():
        print("\n[CRITICAL ERROR] Pre-flight safety checks failed.")
        print("Resolve all failures before building release firmware, or pass --skip-safety-gate for an unflashed build.")
        return False
    else:
        print("[OK] Safety gate cleared.\n")

    # -------------------------------------------------------------
    # Step 2: Validate / Fetch Source Firmware
    # -------------------------------------------------------------
    print(">>> STEP 2: Validating Source Binaries...")
    stock_frm_path = ensure_stock_firmware()
    print(f"[OK] Stock firmware verified: {stock_frm_path}")

    # Determine custom logo to inject
    if custom_logo_brp_path is None:
        local_logo = REPO_ROOT / "assets" / "boot_logo" / "dvf_TDSt.brp"
        sample_brp = REPO_ROOT.parent / "ricoh-gr2-boot-screen-mod" / "sd-card" / "dvf_TDSt.brp"
        if local_logo.exists():
            custom_logo_brp_path = local_logo
        elif sample_brp.exists():
            custom_logo_brp_path = sample_brp
        else:
            raise FileNotFoundError("Custom logo BRP not found in assets/ or workspace")

    custom_logo_brp_path = Path(custom_logo_brp_path)
    custom_brp_data = custom_logo_brp_path.read_bytes()
    if len(custom_brp_data) != DVF_TDST_SIZE:
        raise ValueError(f"Invalid BRP size {len(custom_brp_data)} B (must be exactly {DVF_TDST_SIZE} B)")
    print(f"[OK] Custom boot logo validated ({custom_logo_brp_path.name}, {len(custom_brp_data):,} bytes)")

    # -------------------------------------------------------------
    # Step 3: Unpack and Surgical In-Place Patching (Tier 2 Only)
    # -------------------------------------------------------------
    print("\n>>> STEP 3: Applying Surgical In-Place Patch (b01firm6)...")
    raw_frm, images = load(str(stock_frm_path))

    # Assert b01firm8 (executable code) is 100% UNTOUCHED
    stock_f8_hash = sha256_bytes(images["b01firm8"])
    print(f"[*] b01firm8 (Main RTOS): 100% untouched factory code (SHA-256: {stock_f8_hash[:16]}...)")

    # Modify b01firm6 (A: resource drive)
    f6_orig = images["b01firm6"]
    f6_patched = bytearray(f6_orig)

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

    out_frm_path = dist_dir / f"rg2_cfw_v{version}_bootlogo.frm"
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
    for name, img in verify_images.items():
        if nupdate_csum(img) != update_header(img)["nUpdateCsum"]:
            raise ValueError(f"{name}: nUpdateCsum does not match payload")

    print("[PASS] Decrypted round-trip verification: byte-exact payload match.")
    print("[PASS] b01firm8 (APP) identical to stock; nUpdateCsum valid for every image "
          f"(b01firm6 0x{update_header(verify_images['b01firm6'])['nUpdateCsum']:08X}).")

    # -------------------------------------------------------------
    # Step 6: Generate Release Checksums & Documentation
    # -------------------------------------------------------------
    print("\n>>> STEP 6: Generating Release Manifest...")
    sha_manifest = dist_dir / "SHA256SUMS.txt"
    with open(sha_manifest, "w") as f_sha:
        f_sha.write(f"{out_hash}  {out_frm_path.name}\n")

    # Also build standalone ARM bare-metal binary if make was run
    bin_cfw = REPO_ROOT / "bin" / "gr2_cfw.bin"
    if bin_cfw.exists():
        out_bin = dist_dir / f"gr2_cfw_v{version}.bin"
        shutil.copy2(bin_cfw, out_bin)
        bin_hash = sha256_file(out_bin)
        with open(sha_manifest, "a") as f_sha:
            f_sha.write(f"{bin_hash}  {out_bin.name}\n")
        print(f"[OK] Included bare-metal payload: {out_bin.name} ({out_bin.stat().st_size:,} bytes)")

    release_notes = dist_dir / "RELEASE_NOTES.md"
    release_notes.write_text(f"""# Ricoh GR II Custom Firmware v{version} (Custom Boot Screen)

**Target:** Ricoh GR II, firmware 3.00 (`rg2_v003.frm`)  
**Change:** `A:/img/dvf_TDSt.brp` (boot logo, RES module `b01firm6`) replaced in place, same length;
`nUpdateCsum` of `b01firm6` recomputed with the verified algorithm. `b01firm8` (APP) is byte-identical to stock.  
**Status:** Untested on hardware. The camera's acceptance of a recomputed checksum and of a same-version
(3.00 -> 3.00) install is statically derived, not observed.

## Checksums
```
{sha_manifest.read_text().strip()}
```

## Risks
- The updater destroys the APP header before writing. If the update is interrupted, the IPL boots BAK, which
  reflashes from `rg2_v*.frm` on the card - only if the card has no `backup.ttl`.
- If the new logo makes APP crash, APP still passes the IPL header check; an exception boots BAK, which then
  powers off instead of reflashing. Recovery then needs shell access to BAK (UART); not tested.

## Flashing (official menu procedure)
1. Prepare the rescue card by hand: the stock `rg2_v003.frm` as the only `rg2_v*.frm` in the root, no
   `backup.ttl`. Keep that card unchanged.
2. On a **separate** card, copy `{out_frm_path.name}` to the root as `rg2_v003.frm` (the updater only finds
   `rg2_v*.frm`). Never put it on the rescue card.
3. Fully charged battery. MENU -> Fn1 -> WB (Set-up) -> [Confirm Firmware Version] -> Yes. Do not power off.
4. To revert, repeat step 3 with the rescue card (stock `rg2_v003.frm`).
""")

    print(f"[SUCCESS] Release package ready in {dist_dir}:")
    for f in dist_dir.iterdir():
        if f.is_file():
            print(f"  • {f.name} ({f.stat().st_size:,} bytes)")
    print("=" * 70)
    return True

if __name__ == "__main__":
    skip_gate = "--skip-safety-gate" in sys.argv or os.environ.get("CI") == "true"
    success = build_cfw_v001(skip_safety_gate=skip_gate)
    sys.exit(0 if success else 1)
