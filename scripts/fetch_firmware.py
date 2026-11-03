#!/usr/bin/env python3
"""Stock firmware inputs for the emulator and the loader build (`make fw`).

Puts the factory rg2_v003.frm into fw/ and extracts it there with tools/gr2frm.py:
    fw/rg2_v003.frm   stock container (SHA-256 checked)
    fw/b01firm8.bin   APP image (loaded at 0xA0020200)
    fw/b01firm6.bin   RES image
    fw/afs/           the A: drive files (isp/IQ.bin, fix/AE_F.bin, ...)

Source, first match: --frm FILE, $GR2_STOCK_FRM, else the official download from
Ricoh (zip SHA-256 checked). The firmware is Ricoh's: fw/ is never committed and
never uploaded as a build artifact.
"""
import argparse
import hashlib
import io
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "fw"
ZIP_URL = "https://www.ricoh-imaging.co.jp/english/support/digital/firmware/rg2_v003.zip"
ZIP_SHA256 = "76d28c8e48c37123f226b8aa6d236e23655146bc08070738d9e734290ea6b13d"
FRM_SHA256 = "3643850f2ae74ae967eb336794048f554caa1b11d05b8569824d5719f1838331"
FRM_IN_ZIP = "rg2_v003/rg2_v003.frm"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def local_frm(explicit):
    cands = [explicit, os.environ.get("GR2_STOCK_FRM")]
    for c in cands:
        if c and Path(c).is_file():
            data = Path(c).read_bytes()
            if sha256(data) == FRM_SHA256:
                print(f"stock firmware: {c}")
                return data
            print(f"skipping {c}: not the factory rg2_v003.frm")
    return None


def download():
    print(f"stock firmware: downloading {ZIP_URL}")
    req = urllib.request.Request(ZIP_URL, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=120) as r:
        data = r.read()
    if sha256(data) != ZIP_SHA256:
        sys.exit(f"downloaded zip SHA-256 {sha256(data)} != {ZIP_SHA256}")
    frm = zipfile.ZipFile(io.BytesIO(data)).read(FRM_IN_ZIP)
    if sha256(frm) != FRM_SHA256:
        sys.exit("rg2_v003.frm inside the zip is not the factory file")
    return frm


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--frm", type=Path, help="local stock rg2_v003.frm")
    a = ap.parse_args()
    dst = OUT / "rg2_v003.frm"
    if dst.is_file() and sha256(dst.read_bytes()) == FRM_SHA256 and (OUT / "afs").is_dir():
        print("fw/ is up to date")
        return
    frm = local_frm(a.frm) or download()
    shutil.rmtree(OUT, ignore_errors=True)
    OUT.mkdir()
    dst.write_bytes(frm)
    subprocess.run([sys.executable, str(REPO / "tools" / "gr2frm.py"), "extract", str(dst), str(OUT)], check=True)


if __name__ == "__main__":
    main()
