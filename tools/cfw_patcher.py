#!/usr/bin/env python3
"""Ricoh GR II in-place firmware patcher.

Applies exact-length patches or A: drive file replacements to rg2_v003.frm with
expected-bytes assertions.  `build_frm` repacks via gr2frm.repack, which
recomputes nUpdateCsum for every changed image.
"""

import os
import sys
import argparse
import hashlib
from gr2frm import load, repack, parse_container, afs_entries

STOCK_FRM = "rg2_v003.frm"

class PatchAssertionError(Exception):
    pass

class CfwPatcher:
    def __init__(self, base_frm_path=STOCK_FRM):
        self.base_frm_path = base_frm_path
        with open(base_frm_path, "rb") as f:
            self.frm_bytes = f.read()
        self.raw_frm, self.images = load(base_frm_path)
        self.modified_images = {k: bytearray(v) for k, v in self.images.items()}
        self.log = []

    def replace_afs_file(self, afs_path, replacement_bytes):
        """Replace an A: drive file (e.g. "A:/img/dvf_TDSt.brp") in b01firm6; same length only."""
        f6 = self.modified_images["b01firm6"]
        for name, off, size in afs_entries(bytes(f6)):
            if name == afs_path:
                if len(replacement_bytes) != size:
                    raise ValueError(f"{afs_path}: replacement is {len(replacement_bytes)} B, file is {size} B")
                self.patch_binary_bytes("b01firm6", off, bytes(f6[off:off + size]), replacement_bytes, afs_path)
                return
        raise FileNotFoundError(f"AFS path {afs_path} not found in b01firm6")

    def patch_binary_bytes(self, target_image, offset, expected_bytes, replacement_bytes, description=""):
        """Apply an in-place patch to target_image with pre-condition assertion."""
        if target_image not in self.modified_images:
            raise ValueError(f"Unknown target image {target_image}")
        if len(expected_bytes) != len(replacement_bytes):
            raise ValueError(f"Length mismatch: replacement ({len(replacement_bytes)} B) != expected ({len(expected_bytes)} B)")

        img = self.modified_images[target_image]
        actual_bytes = bytes(img[offset:offset+len(expected_bytes)])

        if actual_bytes != expected_bytes:
            raise PatchAssertionError(
                f"Patch assertion failed at 0x{offset:08X} in {target_image}!\n"
                f"  Expected: {expected_bytes.hex()}\n"
                f"  Actual:   {actual_bytes.hex()}\n"
                f"  Aborting to prevent corrupting firmware."
            )

        img[offset:offset+len(replacement_bytes)] = replacement_bytes
        desc = f" [{description}]" if description else ""
        self.log.append(f"Applied patch to {target_image} @ 0x{offset:08X} ({len(replacement_bytes)} B){desc}")
        print(f"[OK] {self.log[-1]}")

    def build_frm(self, output_path):
        """Repack and write the modified firmware container."""
        print(f"[*] Repacking firmware into {output_path}...")
        final_images = {k: bytes(v) for k, v in self.modified_images.items()}
        repacked_bytes = repack(self.frm_bytes, final_images)

        # Pre-flight verify repacked container
        _, entries = parse_container(repacked_bytes)
        if len(entries) != 2:
            raise ValueError(f"Container corruption: expected 2 root entries, got {len(entries)}")

        with open(output_path, "wb") as f_out:
            f_out.write(repacked_bytes)

        h = hashlib.sha256(repacked_bytes).hexdigest()
        print(f"[SUCCESS] Wrote {output_path} ({len(repacked_bytes):,} bytes)")
        print(f"          SHA-256: {h}")
        return output_path

if __name__ == "__main__":
    print("Ricoh GR II CFW Patcher Engine Ready.")
