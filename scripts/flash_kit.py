#!/usr/bin/env python3
"""Prepare dist/flash/: one folder per M10 flash step (`make flash-kit`).

Each step folder holds STEP.md (what to do, pass/fail, recovery) and card*/
directories whose contents are exactly the root of the SD card for that step.
Full rationale and risks: loader/flash/README.md §6/§7. Nothing here touches a
card or the camera; the user copies the files.

Inputs, all checked: fw/rg2_v003.frm (stock, SHA-256), dist/rg2_v003_loader.frm
(SHA-256 of the reproducible build), loader/flash/probe/Startup.ttl,
loader/flash/rescue/backup.ttl (ASCII + CRLF), dist/gr2_cfw-v<VERSION>-debug.zip.
"""
import hashlib
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "dist" / "flash"
STOCK = ROOT / "fw" / "rg2_v003.frm"
LOADER = ROOT / "dist" / "rg2_v003_loader.frm"
PROBE = ROOT / "loader" / "flash" / "probe" / "Startup.ttl"
RESCUE = ROOT / "loader" / "flash" / "rescue" / "backup.ttl"
STOCK_SHA = "3643850f2ae74ae967eb336794048f554caa1b11d05b8569824d5719f1838331"
LOADER_SHA = "1914ac9069cb5e9803d348bea999258884d0d8199f7ff2e257161ec44c1f91f1"   # stub v2 (card at section 3)

COMMON = """\
Before every flash step (F1a, F1b, F2, F5, U):
- `make check` printed `STATUS: ALL GATES PASSED` on the Mac.
- Battery indicator **full**; a second charged battery at hand.
- Card R (step F1b) prepared and checked, kept out of the camera.
- Card formatted **in the camera** (the firmware refuses card layouts outside the SD specification; a Mac-formatted
  card may not mount, finding 28), FAT32 if <= 32 GB. Copy exactly the `card/` contents to the card root, nothing else
  (no other `rg2_v*.frm` — the highest number wins —, no `gr_v*.frm`, no `b01firm*` or `update.ttl` files, no `backup.ttl`
  unless listed). On macOS: `find /Volumes/<card> -name '._*' -delete`.
- Check the `.frm` on the card: `shasum -a 256 /Volumes/<card>/rg2_v003.frm` = the hash in STEP.md.

Keys: **skip key** = shutter *half*-pressed while switching on (loader does nothing).
**Combo** = dial MOVIE + shutter *fully* pressed + POWER (enters BAK, runs `H:/backup.ttl`).
Never press fully on MOVIE when you mean skip.
"""

STEPS = [
    ("F0-timing-probe", "F0 · Timing probe (no flash)", {"card": ["probe"]}, """\
**Writes flash:** no. Measures the stock boot timeline that the loader will improve.

1. Card root = `card/` (only the probe `Startup.ttl`; any CFW `Startup.ttl` must be gone — other folders may stay).
2. Power on, wait ~10 s after the LCD comes up, power off.
3. Copy `H:/cfw/probe/` to the Mac and run
   `python3 scripts/loader_timing.py <that folder> --task-start <systime>`
   (`<systime>` from `task: running, systime …` in a debug-build `cfw_log.txt`; 0x7D0 if you have none).

**Pass:** `H:/cfw/probe/PROF_DONE` exists, `BOOT_PROF.BIN` decodes into 12 section loads, `LDR_STATE.BIN` = `FF FF FF FF` (stock APP).
**Fail:** no files → DEVEL_MODE off or the TTL damaged in copying. Nothing was written to flash.
Send me: the `H:/cfw/probe/` folder (or the `loader_timing.py` output).
"""),
    ("F1a-stock-update", "F1a · Same-version stock update (first flash, stock bytes)", {"card": ["stock"]}, f"""\
**Writes flash:** yes, the unmodified factory 3.00 firmware over itself. Proves the camera accepts a 3.00 → 3.00
install through the menu; everything after depends on it.

`rg2_v003.frm` SHA-256 `{STOCK_SHA}`

1. Card S root = `card/` (only the stock `rg2_v003.frm`). Battery full.
2. MENU → Set-up tab → *Confirm Firmware Version* (Ricoh's official update procedure). The camera checks the file for a
   few seconds, then shows **"Execute?"** with *Current Version 3.00* and *New Version 3.00* and No/Yes (the page closes
   by itself after 20 s idle). There is no version comparison: same-version installs are accepted (finding 27).
3. Select **Yes**. Progress page: "Checking rewrite file", then "Rewriting program" (the bar is a timer, not real
   progress). Do not touch the camera.
4. **"Firmware update complete."** appears with *Finish*: press **OK** (not PLAY). The camera switches **off**; it does
   not restart by itself. Switch it on.

**Pass:** "Firmware update complete." appeared, the camera boots, a shot and its playback work. (*Confirm Firmware
Version* showing 3.00 proves nothing in a same-version install.)
**Refused before anything is erased** — **stop here** and note the exact screen:
- title "Confirm Firmware Version" without No/Yes: the camera found no valid file (missing, misnamed, or the checksum failed);
- "Battery depleted" after Yes: battery not full;
- *New Version* blank: one module failed its check; do not press Yes (it would only fail with "Error occurred.").
**"Error occurred." during the update:** keep card S in and power-cycle. If the APP was already erased, the camera boots
into recovery and reflashes from card S by itself (LED on). If nothing happens: card R + combo (F1b).
"""),
    ("F1b-rescue-path", "F1b · Rescue card + combo (stock bytes)", {"card": ["stock", "rescue"]}, f"""\
**Writes flash:** yes, stock 3.00 again, this time through the recovery firmware (BAK). Proves the last-resort recovery.

`rg2_v003.frm` SHA-256 `{STOCK_SHA}`. `backup.ttl` creates `H:/RESCUE_RAN`, runs `up`, and only if `up` returns
(refused or failed) creates `H:/RESCUE_UP_RET` and switches off. On success `up` powers off with a 3 s wake-up and
never returns (finding 27).

1. Card R root = `card/` (stock `rg2_v003.frm` + `backup.ttl`), card formatted in the camera. Battery full.
2. Combo (cold start): dial MOVIE, hold the shutter fully down, press POWER. **Then release the shutter and turn the
   dial off MOVIE**, so the self-restart cannot enter recovery again.
3. Wait without touching anything. Nothing is shown (no LCD, no LED) for tens of seconds to a few minutes.
4. Expected: the camera switches off and **about 3 s later switches itself back on** into a normal stock boot.
5. Power off, remove card R (never leave it in the camera), check it on the Mac.

**Pass:** the camera came back on by itself and works; card R holds `RESCUE_RAN` and **no** `RESCUE_UP_RET`.
**Refused:** off and stays off, `RESCUE_UP_RET` exists: `up` refused (nothing erased if it happened in the search). Switch on
normally; report it and do not continue to F2.
**No `RESCUE_RAN`:** the script did not run (combo not taken, or the card did not mount: format it in the camera).
**Stays on in recovery (no auto power-off):** battery out, remove R, normal boot. If the APP was already erased, card S or
R in + power on reflashes again.
Keep card R ready (and out of the camera) for all later steps.
"""),
    ("F2-flash-loader", "F2 · Flash the debug loader (first modified firmware)", {"card-L": ["loader"], "card-N": ["probe"]}, f"""\
**Writes flash:** yes: stock 3.00 + the 132-byte loader stub (only APP changes; RES identical; checksum recomputed).

Loader `rg2_v003.frm` (renamed from `rg2_v003_loader.frm`) SHA-256 `{LOADER_SHA}`

1. Card L root = `card-L/` (only the loader `rg2_v003.frm`). Battery full.
2. Menu update as in F1a. **Before Yes:** the page must say "Execute?" with *New Version 3.00*. If it shows
   "Confirm Firmware Version" without No/Yes, or *New Version* is blank, the camera rejected the file (header or
   checksum): nothing was erased; stop and report.
3. "Firmware update complete." → OK → the camera switches off. Switch it on **with card L still in**: it must boot normally
   (the loader mounts the card at section 3, finds no `cfw/gr2_cfw.bin`, and stays stock).
4. Card N root = `card-N/` (probe `Startup.ttl`, **no** `cfw/gr2_cfw.bin`: move or delete the `cfw/` folder). Power on,
   wait ~10 s, power off. Copy `H:/cfw/probe/` to the Mac, run `scripts/loader_timing.py` on it.
5. Once more after a cold start (battery out for 30 s). Then power-cycle a few times; shoot, play back, check the files
   on the Mac.

**Pass:** the camera looks and behaves stock; `LDR_STATE.BIN` = `00 00 00 03` (loader ran, no stage 1 on the card), also
after the cold start; section 1 takes the F0 time (36 ms), the card mount shows up in section 3's load.
**Rejected / "Error occurred.":** as F1a.
**No LCD / hang at power-on:** battery out; boot with the skip key (stock, the card is not touched); then uninstall (U).
No usable menu at all: card R + combo.
"""),
    ("F2b-skip-nocard-usb", "F2b · Skip key, no card, USB (no flash)", {"card": ["probe"]}, """\
**Writes flash:** no.

Card N root = `card/` as in F2 (probe `Startup.ttl`, no `cfw/gr2_cfw.bin`).

a. Hold the shutter half-pressed while switching on → `LDR_STATE.BIN` = `00 00 00 01`.
b. No card in the camera → it works normally.
c. Card N in, connect USB to the Mac (USB boot) → the card mounts on the Mac, its files unchanged.

**Fail:** as F2 (skip key, then U).
"""),
    ("F3-loader-starts-cfw", "F3 · The loader starts the CFW (no flash)", {"card": ["debug", "probe"]}, """\
**Writes flash:** no. The flashed loader loads the CFW from the card without `Startup.ttl` doing it.

Card N root = `card/`: the debug zip contents with its `Startup.ttl` **replaced by the probe** `Startup.ttl` (the CFW
`Startup.ttl` must never be on a card while the loader is flashed). Never have card R in the camera for this step.

1. Power on. Use the CFW as usual (Effect → CFW Menu / Recipes).
2. Power off. Copy `H:/cfw/cfw_log.txt` and `H:/cfw/probe/` to the Mac.
3. Power on with the skip key held: stock camera, no CFW; `LDR_STATE.BIN` = `00 00 00 01`.

**Pass:** CFW up as usual; `cfw_log.txt` has `main: started by the flashed loader …`; `LDR_STATE.BIN` = `00 00 00 06`;
`loader_timing.py` shows the first-hook preload time.
**Crash (camera powers off):** next boot with the skip key; report the log. **Hang:** battery out + skip key.
Send me: `cfw_log.txt`, `H:/cfw/probe/`.
"""),
    ("F4-early-look", "F4 · M10c: menus and the recipe look during the boot (no flash)", {"card": ["debug"]}, """\
**Writes flash:** no. The CFW now installs its menus and Effect hooks while the firmware loads (section 10, ~0.8 s
after power-on) and shows the active My Settings slot's recipe look as soon as the camera has picked the slot,
instead of ~2 s after power-on.

Card root = `card/` (the debug `cfw/` folder; no `Startup.ttl`). Card R stays out.

1. Make sure a recipe is saved in a My Settings slot (Recipes → OK → MY1/MY2/MY3), e.g. Portra in MY2.
2. Turn the dial to that slot, switch off. Switch on and watch the first live-view frames: the recipe's colours should
   be there from the start (before: stock colours for about a second, then the look).
3. Right after power-on press Effect (CFW Menu): it should open as soon as the camera reacts to keys.
4. Use the camera as usual for a minute (menus, Recipes, a shot), then power off. Repeat once from a cold start
   (battery out 30 s).
5. Copy `H:/cfw/cfw_log.txt` to the Mac.

**Pass:** look visible from the first frames, menus work, no `ERROR` in the log; the log has
`main: installed at hook(10), rc 0x00000000`, `recipes: early look (slot, IQ mode)` and the `hook:` timeline.
**Crash or hang at power-on:** battery out, switch on with the shutter half-pressed (stock), send the log.
Send me: `cfw_log.txt` and what you saw on the first frames.
"""),
    ("F5-loader-v2", "F5 · Reflash the loader v2: card read after the lens start (lens bounce fix)",
     {"card-L": ["loader"], "card": ["debug"]}, f"""\
**Writes flash:** yes: stock 3.00 + the loader stub v2, over the v1 loader (same path as F2).

Why: with v1 the stub reads the card right after section 1, before the camera starts the lens; that delays the lens
start by ~190 ms and the lens extends, retracts and extends again (F4; also with a card without `cfw/`; unmounting the
card afterwards did not help). v2 reads the card at section 3, after the lens has started (~0.3 s). A USB-charge boot
no longer touches the card at all.

Loader `rg2_v003.frm` SHA-256 `{LOADER_SHA}`

1. Card L root = `card-L/` (only the loader `rg2_v003.frm`). Battery full.
2. Menu update as in F2 (check "Execute?" with *New Version 3.00* before Yes).
3. "Firmware update complete." → OK → the camera switches off. Switch it on **with card L still in** (no `cfw/`): it must
   boot normally. Watch the lens: **one** smooth extension?
4. Card root = `card/` (debug `cfw/`, no `Startup.ttl`). Dial on a My Settings slot with a recipe. Power
   on a few times, once after a cold start (battery out 30 s). Watch the lens and the first live-view frames; open the
   CFW Menu (Effect).
5. Skip key: **keep the shutter half-pressed until the LCD comes on** (the stub now reads it at ~0.3 s instead of
   ~30 ms) → stock camera, no CFW.
6. Copy `H:/cfw/cfw_log.txt` to the Mac.

**Pass:** no lens bounce in 3 and 4; recipe colours from the first frame; menus work; no `ERROR` in the log; the `hook:`
timeline starts at `0x00000003`; `main: installed at hook(10), rc 0x00000000`.
**Rejected / "Error occurred.":** as F1a. **No LCD / hang:** battery out; skip key; then uninstall (U). No usable menu:
card R + combo.
Send me: `cfw_log.txt`, whether the lens still bounces (with and without `cfw/`), and how the skip key behaved.
"""),
    ("F6-iq-ready-look", "F6 · Recipe look before live view, faster boot (no flash)", {"card": ["debug"]}, """\
**Writes flash:** no (loader v2 from F5 stays).

What changed (all on the card):
- The look is written by a hook on the camera's own "IQ data loaded" call (section 5), right before live view
  starts, instead of at section 10 when live view already runs (F5: colours came a moment after the first frames,
  and writing them while live view ran held up the boot by 0.1–0.4 s).
- The card is no longer unmounted after the CFW is read (that F4 test did not help the lens and made the camera's
  own card mount ~0.2 s slower).

Card root = `card/` (debug `cfw/`, no `Startup.ttl`). Card R stays out.

1. Dial on a My Settings slot with a recipe. Switch on a few times, once after a cold start (battery out 30 s).
   Watch the first live-view frames and the lens.
2. Once with the dial on P (no recipe): stock colours, normal start.
3. Use the camera for a minute (CFW Menu, Recipes, a shot), power off, copy `H:/cfw/cfw_log.txt` to the Mac.

**Pass:** recipe colours on the very first frame; one smooth lens extension; no `ERROR`; in the log the `hook:` timeline
has an `n = 0` entry (the IQ-ready call) followed by `recipes: early look`, and the LV frame there is still 0.
**Crash or hang at power-on:** battery out, switch on with the shutter half-pressed until the LCD comes on (stock),
send the log.
Send me: `cfw_log.txt` and what you saw on the first frames.
"""),
    ("F7-poweroff-status", "F7 · Session status at power-off without Shutdown.ttl (no flash)", {"card": ["debug"]}, """\
**Writes flash:** no.

What changed (all on the card): the debug CFW logs its session state (`status:` lines) itself when the camera powers
off, from a hook on the power-off job's own "run Shutdown.ttl" call. `Shutdown.ttl` is no longer shipped; a
`Shutdown.ttl` of your own would still run after the status lines.

Card root = `card/` (debug `cfw/`). **Delete `H:/Shutdown.ttl`** from the card (otherwise the status is logged twice).
Card R stays out.

1. Switch on with the dial on a recipe slot, use the camera briefly (CFW Menu, a shot), switch off with the power
   button. Repeat once with the dial on P.
2. Copy `H:/cfw/cfw_log.txt` to the Mac.

**Pass:** every session in the log ends with `status: power-off` followed by the `status:` lines (task heartbeat,
settings, key trace); the camera switches off as quickly as before; no `ERROR`; the next power-on is normal.
**Camera does not switch off / hangs at power-off:** battery out, then boot with the shutter half-pressed until the
LCD comes on (stock) and send the log.
Send me: `cfw_log.txt`.
"""),
    ("F8-internal-config", "F8 · Config and recipe looks in internal memory D: (no flash)", {"card": ["debug"]}, """\
**Writes flash:** no. **Writes the camera's internal D: drive:** yes (files in `D:/CFW` only, next to the camera's own
logs; Reset Settings clears them).

What changed (all on the card): the CFW keeps its settings and the looks of the recipes saved in MY1–MY3 in
`D:/CFW` (two copies with checksums, the older one is overwritten). `cfw/config.txt` is no longer written; one from an
older version is imported once.

Card root = `card/` (debug `cfw/`). Card R stays out.

1. Power on. Save a recipe into a My Settings slot (Recipes → OK → MY1/2/3) and change one CFW Menu setting
   (e.g. NR weak from), close the menu. Power off.
2. Power on with the dial on that slot: recipe colours on the first frame, the setting kept. Power off.
3. Replace the card's `cfw/` folder with a fresh copy of `card/cfw/` (as an update would): power on, the setting and
   the slot's look are still there.
4. Move that recipe's file out of `cfw/recipes/` (keep it on the Mac). Power on with the dial on the slot: the look is
   still on the first frame (from D:). Put the file back.
5. Change a setting, close the menu and pull the battery right away. Power on: the camera and the CFW start normally;
   the setting is either the old or the new value.
6. Optional, console: `dir D:/CFW` lists `CFG_A.DAT`, `CFG_B.DAT`, `LOOK_A.DAT`, `LOOK_B.DAT` (a few hundred bytes to
   ~1.2 KB each).
7. Copy `H:/cfw/cfw_log.txt` to the Mac.

**Pass:** 2–5 as described; no `ERROR` in the log; the log shows `config: D: (generation, entries)` at every start and
`recipes: look cache saved to D:` only after a change.
**Crash or hang at power-on:** battery out, switch on with the shutter half-pressed until the LCD comes on (stock),
send the log.
Send me: `cfw_log.txt`, the `dir D:/CFW` output and what you saw in 2–5.
"""),
    ("U-uninstall", "U · Uninstall (back to stock 3.00)", {"card": ["stock"]}, f"""\
**Writes flash:** yes, the stock firmware (any time after F2).

`rg2_v003.frm` SHA-256 `{STOCK_SHA}`

1. Card S root = `card/` (only the stock `rg2_v003.frm`). Battery full. Boot with the skip key if the CFW misbehaves.
2. Menu update as in F1a.
3. Optional: card N with the probe `Startup.ttl` → `LDR_STATE.BIN` = `FF FF FF FF`.

**Fail:** card R + combo (F1b).
"""),
]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def need(path, sha=None):
    if not path.is_file():
        sys.exit(f"missing {path.relative_to(ROOT)} (run make loader dist)")
    data = path.read_bytes()
    if sha and sha256(data) != sha:
        sys.exit(f"{path.relative_to(ROOT)}: SHA-256 {sha256(data)} != expected {sha}")
    return data


def ttl(path):
    data = need(path)
    if not data.isascii() or b"\n" in data.replace(b"\r\n", b""):
        sys.exit(f"{path.relative_to(ROOT)} is not ASCII with CRLF only")
    return data


def main():
    version = (ROOT / "VERSION").read_text().strip()
    stock, loader = need(STOCK, STOCK_SHA), need(LOADER, LOADER_SHA)
    debug = zipfile.ZipFile(ROOT / "dist" / f"gr2_cfw-v{version}-debug.zip")
    parts = {
        "stock": {"rg2_v003.frm": stock},
        "loader": {"rg2_v003.frm": loader},
        "probe": {"Startup.ttl": ttl(PROBE)},
        "rescue": {"backup.ttl": ttl(RESCUE)},
        # the debug zip without its firmware: cfw/ (the loader is already flashed)
        "debug": {n: debug.read(n) for n in debug.namelist() if not n.endswith("/") and n != "rg2_v003.frm"},
    }
    index = ["# M10 flash steps", "", f"Built from gr2_cfw v{version}. Do them in order; each only after the previous one "
             "passed and you reported the result. Details and risks: `loader/flash/README.md` §6/§7.", "", COMMON,
             "| Step | Writes flash | Folder |", "|---|---|---|"]
    for folder, title, cards, text in STEPS:
        d = OUT / folder
        for card in d.glob("card*"):                # replace only what this script writes; results the user
            shutil.rmtree(card)                      # copied next to them (e.g. probe/) stay
        for card, kinds in cards.items():
            for kind in kinds:
                for name, data in parts[kind].items():
                    p = d / card / name
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.write_bytes(data)
        (d / "STEP.md").write_text(f"# {title}\n\n{text}\n---\n{COMMON}")
        flash = "**yes**" if "Writes flash:** yes" in text else "no"
        index.append(f"| {title} | {flash} | `{folder}/` |")
        print(f"prepared dist/flash/{folder}/ ({', '.join(cards)})")
    (OUT / "README.md").write_text("\n".join(index) + "\n")


if __name__ == "__main__":
    main()
