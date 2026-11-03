#!/usr/bin/env python3
"""Prepare dist/flash/: one folder per flash step (M10 F-steps, M11 P-steps; `make flash-kit`).

Each step folder holds STEP.md (what to do, pass/fail, recovery) and card*/
directories whose contents are exactly the root of the SD card for that step.
Full rationale and risks: loader/flash/README.md. Nothing here touches a
card or the camera; the user copies the files.

Inputs, all checked: fw/rg2_v003.frm (stock, SHA-256), dist/rg2_v003_loader.frm
(SHA-256 of the reproducible build), dist/rg2_v003_tty.frm (SHA-256 from its
`make rom` manifest), loader/flash/probe/Startup.ttl, loader/flash/rescue/backup.ttl
(ASCII + CRLF), dist/gritty-v<VERSION>-debug.zip.
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
ROM = ROOT / "dist" / "rg2_v003_tty.frm"
ROM_MANIFEST = ROOT / "dist" / "rg2_v003_tty.manifest.txt"
PROBE = ROOT / "loader" / "flash" / "probe" / "Startup.ttl"
RESCUE = ROOT / "loader" / "flash" / "rescue" / "backup.ttl"
STOCK_SHA = "3643850f2ae74ae967eb336794048f554caa1b11d05b8569824d5719f1838331"
LOADER_SHA = "1914ac9069cb5e9803d348bea999258884d0d8199f7ff2e257161ec44c1f91f1"   # stub v2 (card at section 3)


def manifest_sha():
    """The release .frm's SHA-256 as `make rom` recorded it (the build is checked by scripts/emu_release.py)."""
    if not ROM_MANIFEST.is_file():
        sys.exit(f"missing {ROM_MANIFEST.relative_to(ROOT)} (run make rom)")
    for line in ROM_MANIFEST.read_text().splitlines():
        if line.startswith("output"):
            return line.split()[-1]
    sys.exit(f"{ROM_MANIFEST.relative_to(ROOT)}: no output line")


ROM_SHA = manifest_sha()

COMMON = """\
Before every flash step (F1a, F1b, F2, F5, P1, P3, U):
- The maintainer's pre-flight safety check passed (stock firmware and NAND backup verified) on the Mac.
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

1. Card root = `card/` (only the probe `Startup.ttl`; any TTY loader `Startup.ttl` must be gone — other folders may stay).
2. Power on, wait ~10 s after the LCD comes up, power off.
3. Copy `H:/tty/probe/` to the Mac and run
   `python3 scripts/loader_timing.py <that folder> --task-start <systime>`
   (`<systime>` from `task: running, systime …` in a debug-build `tty_log.txt`; 0x7D0 if you have none).

**Pass:** `H:/tty/probe/PROF_DONE` exists, `BOOT_PROF.BIN` decodes into 12 section loads, `LDR_STATE.BIN` = `FF FF FF FF` (stock APP).
**Fail:** no files → DEVEL_MODE off or the TTL damaged in copying. Nothing was written to flash.
Send me: the `H:/tty/probe/` folder (or the `loader_timing.py` output).
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
4. Card N root = `card-N/` (probe `Startup.ttl`, **no** `cfw/gr2_cfw.bin`: move or delete it). Power on,
   wait ~10 s, power off. Copy `H:/tty/probe/` to the Mac, run `scripts/loader_timing.py` on it.
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
    ("F3-loader-starts-tty", "F3 · The loader starts TTY (no flash)", {"card": ["debug", "probe"]}, """\
**Writes flash:** no. The flashed loader loads TTY from the card without `Startup.ttl` doing it.

Card N root = `card/`: the debug zip contents with its `Startup.ttl` **replaced by the probe** `Startup.ttl` (the TTY
loader `Startup.ttl` must never be on a card while the loader is flashed). Never have card R in the camera for this step.
The debug card keeps stage 1 as `cfw/gr2_cfw.bin` (the flashed debug loader opens exactly `H:/cfw/gr2_cfw.bin`);
everything else is in `tty/`.

1. Power on. Use TTY as usual (Effect → TTY Menu / Recipes).
2. Power off. Copy `H:/tty/tty_log.txt` and `H:/tty/probe/` to the Mac.
3. Power on with the skip key held: stock camera, no TTY; `LDR_STATE.BIN` = `00 00 00 01`.

**Pass:** TTY up as usual; `tty_log.txt` has `main: started by the flashed loader …`; `LDR_STATE.BIN` = `00 00 00 06`;
`loader_timing.py` shows the first-hook preload time.
**Crash (camera powers off):** next boot with the skip key; report the log. **Hang:** battery out + skip key.
Send me: `tty_log.txt`, `H:/tty/probe/`.
"""),
    ("F4-early-look", "F4 · M10c: menus and the recipe look during the boot (no flash)", {"card": ["debug"]}, """\
**Writes flash:** no. TTY now installs its menus and Effect hooks while the firmware loads (section 10, ~0.8 s
after power-on) and shows the active My Settings slot's recipe look as soon as the camera has picked the slot,
instead of ~2 s after power-on.

Card root = `card/` (the debug `cfw/gr2_cfw.bin` and `tty/` folder; no `Startup.ttl`). Card R stays out.

1. Make sure a recipe is saved in a My Settings slot (Recipes → OK → MY1/MY2/MY3), e.g. Portra in MY2.
2. Turn the dial to that slot, switch off. Switch on and watch the first live-view frames: the recipe's colours should
   be there from the start (before: stock colours for about a second, then the look).
3. Right after power-on press Effect (TTY Menu): it should open as soon as the camera reacts to keys.
4. Use the camera as usual for a minute (menus, Recipes, a shot), then power off. Repeat once from a cold start
   (battery out 30 s).
5. Copy `H:/tty/tty_log.txt` to the Mac.

**Pass:** look visible from the first frames, menus work, no `ERROR` in the log; the log has
`main: installed at hook(10), rc 0x00000000`, `recipes: early look (slot, IQ mode)` and the `hook:` timeline.
**Crash or hang at power-on:** battery out, switch on with the shutter half-pressed (stock), send the log.
Send me: `tty_log.txt` and what you saw on the first frames.
"""),
    ("F5-loader-v2", "F5 · Reflash the loader v2: card read after the lens start (lens bounce fix)",
     {"card-L": ["loader"], "card": ["debug"]}, f"""\
**Writes flash:** yes: stock 3.00 + the loader stub v2, over the v1 loader (same path as F2).

Why: with v1 the stub reads the card right after section 1, before the camera starts the lens; that delays the lens
start by ~190 ms and the lens extends, retracts and extends again (F4; also with a card without `cfw/gr2_cfw.bin`; unmounting the
card afterwards did not help). v2 reads the card at section 3, after the lens has started (~0.3 s). A USB-charge boot
no longer touches the card at all.

Loader `rg2_v003.frm` SHA-256 `{LOADER_SHA}`

1. Card L root = `card-L/` (only the loader `rg2_v003.frm`). Battery full.
2. Menu update as in F2 (check "Execute?" with *New Version 3.00* before Yes).
3. "Firmware update complete." → OK → the camera switches off. Switch it on **with card L still in** (no `cfw/gr2_cfw.bin`): it must
   boot normally. Watch the lens: **one** smooth extension?
4. Card root = `card/` (debug card, no `Startup.ttl`). Dial on a My Settings slot with a recipe. Power
   on a few times, once after a cold start (battery out 30 s). Watch the lens and the first live-view frames; open the
   TTY Menu (Effect).
5. Skip key: **keep the shutter half-pressed until the LCD comes on** (the stub now reads it at ~0.3 s instead of
   ~30 ms) → stock camera, no TTY.
6. Copy `H:/tty/tty_log.txt` to the Mac.

**Pass:** no lens bounce in 3 and 4; recipe colours from the first frame; menus work; no `ERROR` in the log; the `hook:`
timeline starts at `0x00000003`; `main: installed at hook(10), rc 0x00000000`.
**Rejected / "Error occurred.":** as F1a. **No LCD / hang:** battery out; skip key; then uninstall (U). No usable menu:
card R + combo.
Send me: `tty_log.txt`, whether the lens still bounces (with and without `cfw/gr2_cfw.bin`), and how the skip key behaved.
"""),
    ("F6-iq-ready-look", "F6 · Recipe look before live view, faster boot (no flash)", {"card": ["debug"]}, """\
**Writes flash:** no (loader v2 from F5 stays).

What changed (all on the card):
- The look is written by a hook on the camera's own "IQ data loaded" call (section 5), right before live view
  starts, instead of at section 10 when live view already runs (F5: colours came a moment after the first frames,
  and writing them while live view ran held up the boot by 0.1–0.4 s).
- The card is no longer unmounted after TTY is read (that F4 test did not help the lens and made the camera's
  own card mount ~0.2 s slower).

Card root = `card/` (debug card, no `Startup.ttl`). Card R stays out.

1. Dial on a My Settings slot with a recipe. Switch on a few times, once after a cold start (battery out 30 s).
   Watch the first live-view frames and the lens.
2. Once with the dial on P (no recipe): stock colours, normal start.
3. Use the camera for a minute (TTY Menu, Recipes, a shot), power off, copy `H:/tty/tty_log.txt` to the Mac.

**Pass:** recipe colours on the very first frame; one smooth lens extension; no `ERROR`; in the log the `hook:` timeline
has an `n = 0` entry (the IQ-ready call) followed by `recipes: early look`, and the LV frame there is still 0.
**Crash or hang at power-on:** battery out, switch on with the shutter half-pressed until the LCD comes on (stock),
send the log.
Send me: `tty_log.txt` and what you saw on the first frames.
"""),
    ("F7-poweroff-status", "F7 · Session status at power-off without Shutdown.ttl (no flash)", {"card": ["debug"]}, """\
**Writes flash:** no.

What changed (all on the card): the debug TTY logs its session state (`status:` lines) itself when the camera powers
off, from a hook on the power-off job's own "run Shutdown.ttl" call. `Shutdown.ttl` is no longer shipped; a
`Shutdown.ttl` of your own would still run after the status lines.

Card root = `card/` (debug card). **Delete `H:/Shutdown.ttl`** from the card (otherwise the status is logged twice).
Card R stays out.

1. Switch on with the dial on a recipe slot, use the camera briefly (TTY Menu, a shot), switch off with the power
   button. Repeat once with the dial on P.
2. Copy `H:/tty/tty_log.txt` to the Mac.

**Pass:** every session in the log ends with `status: power-off` followed by the `status:` lines (task heartbeat,
settings, key trace); the camera switches off as quickly as before; no `ERROR`; the next power-on is normal.
**Camera does not switch off / hangs at power-off:** battery out, then boot with the shutter half-pressed until the
LCD comes on (stock) and send the log.
Send me: `tty_log.txt`.
"""),
    ("F8-internal-config", "F8 · Config and recipe looks in internal memory D: (no flash)", {"card": ["debug"]}, """\
**Writes flash:** no. **Writes the camera's internal D: drive:** yes (files in `D:/CFW` only, next to the camera's own
logs; Reset Settings clears them).

What changed (all on the card): TTY keeps its settings and the looks of the recipes saved in MY1–MY3 in
`D:/CFW` (two copies with checksums, the older one is overwritten). `tty/config.txt` is no longer written; one from an
older version is imported once.

Card root = `card/` (debug card). Card R stays out.

1. Power on. Save a recipe into a My Settings slot (Recipes → OK → MY1/2/3) and change one TTY Menu setting
   (e.g. NR weak from), close the menu. Power off.
2. Power on with the dial on that slot: recipe colours on the first frame, the setting kept. Power off.
3. Replace the card's `tty/` folder with a fresh copy of `card/tty/` (as an update would): power on, the setting and
   the slot's look are still there.
4. Move that recipe's file out of `tty/recipes/` (keep it on the Mac). Power on with the dial on the slot: the look is
   still on the first frame (from D:). Put the file back.
5. Change a setting, close the menu and pull the battery right away. Power on: the camera and TTY start normally;
   the setting is either the old or the new value.
6. Optional, console: `dir D:/CFW` lists `CFG_A.DAT`, `CFG_B.DAT`, `LOOK_A.DAT`, `LOOK_B.DAT` (a few hundred bytes to
   ~1.2 KB each).
7. Copy `H:/tty/tty_log.txt` to the Mac.

**Pass:** 2–5 as described; no `ERROR` in the log; the log shows `config: D: (generation, entries)` at every start and
`recipes: look cache saved to D:` only after a change.
**Crash or hang at power-on:** battery out, switch on with the shutter half-pressed until the LCD comes on (stock),
send the log.
Send me: `tty_log.txt`, the `dir D:/CFW` output and what you saw in 2–5.
"""),
    ("F8b-config-card-backup", "F8b · Settings back after a battery pull (card copy) (no flash)", {"card": ["debug"]}, """\
**Writes flash:** no. F8 showed that the camera formats its internal D: drive on the first start after a battery pull
or crash (stock behaviour, finding 34 §11). TTY now also keeps a copy of its settings in `tty/config.txt` on the
card and takes it when D: is empty.

Card root = `card/` (debug card). Card R stays out.

1. Power on. Save a recipe into MY1 and change one TTY Menu setting, close the menu. Normal power-off, power on: both
   kept.
2. Change the setting again, close the menu, pull the battery. Power on with the dial on MY1: the recipe colours from
   the first frame and the new setting are back.
3. Optional, console, right after step 2's start: `dir D:/` (expect only fresh `log` files and `CFW`).
4. Normal power-off, power on once more; copy `H:/tty/tty_log.txt` to the Mac.

**Pass:** 1–2 as described; no `ERROR`; after the pull the log shows `config: imported H:/tty/config.txt` and then
`config: saved to D:`; after a normal power-off `config: D: (generation, entries)`.
Send me: `tty_log.txt` and the `dir D:/` output.
"""),
    ("F9-early-settings", "F9 · TTY settings before the first frame, TTY from ~1.2 s (no flash)", {"card": ["debug"]}, """\
**Writes flash:** no.

What changed (all on the card; finding 35):
- The TTY settings (TAv cap, Auto ISO slowest/max/at max, Auto-Hi slowest shutter, Clarity, NR, Snap, TAv min, Auto
  ISO in M) are applied during the boot, at the same moment as the recipe look (before live view), instead of at
  ~2.3 s.
- The TTY task starts while the camera finishes booting: frame lines/zebras, the Effect button and INFO work from
  about 1.2 s; the TTY Menu opens once the fonts are read (after the camera's own card mount, ~1.9 s), Recipes once the
  recipe list is read (~2.3 s). A press before that opens the menu as soon as it can.

Card root = `card/` (debug card; your `tty/config.txt` and D: settings stay). Card R stays out.

1. TTY Menu: set several settings away from STOCK, e.g. Auto ISO slowest 1/125, Auto ISO max 6400, Auto ISO in M ON,
   Snap 0.7m + 1.2m, Clarity 9, frame lines on. Close the menu, normal power-off.
2. Power on in P: frame lines with the first live-view frames; take a shot right away (dim room): shutter not slower
   than 1/125, ISO not above 6400.
3. Power on with the dial on M, ISO AUTO: the first shot right away uses an auto ISO (not 100).
4. Focus mode Snap at "1m": the distance shown is 0.7 m from the start.
5. Press Effect (TTY Menu) right after power-on: it opens as soon as possible (≈2 s or earlier), not lost.
6. A My Settings slot with Clarity 5: set TTY Clarity back to STOCK on that slot: the camera shows 5.
7. Once with PLAY held at power-on (playback start), then switch to shooting: settings in force, images shown.
8. Copy `H:/tty/tty_log.txt` to the Mac.

**Pass:** 2–7 as described; no `ERROR`; the log has `settings: applied at boot` before the `hook:` timeline's
`0x0000000A` entry, `task: loop live` around 1.2 s, `task: late phase` after `init done`; hook(12)/(13) times within
~30 ms of F8b.
**Crash or hang at power-on:** battery out, switch on with the shutter half-pressed until the LCD comes on (stock),
send the log.
Send me: `tty_log.txt` and what you saw in 2–7.
"""),
    ("F10-safe-boot", "F10 · Safe boot: two failed starts disarm TTY, PLAY re-arms it (no flash)", {"card": ["debug"]},
     """\
**Writes flash:** no. **Writes:** two spare bits of the camera's clock chip (finding 30; your R2a probe).

What changed (all on the card): a start counts as failed unless TTY runs 20 s or the camera is switched
off normally. After two failed starts in a row TTY stays off (stock camera) until you switch on once with PLAY.
DEV → "Boot stock once" gives one stock start. The skip key (shutter half-pressed) works as before.

Card root = `card/` (debug card). Card R stays out. Battery pulls also clear the camera's D: drive (stock); TTY
takes its settings back from `tty/config.txt` as in F8b.

1. Power on, use the camera for more than 20 s, power off normally. Power on: TTY is there (TTY Menu, frame lines).
2. Power on and pull the battery after ~5 s. Power on again and pull the battery after ~5 s.
3. Power on: **stock camera** (no TTY Menu on Effect, no frame lines). Power off normally, power on: still stock.
4. Power on with **PLAY**: stock playback. Power off. Power on normally: TTY is back.
5. DEV → "Boot stock once" (OK). Power off, power on: stock. Power off, power on: TTY is back.
6. Copy `H:/tty/tty_log.txt` to the Mac.

**Pass:** 1–5 as described; the log's first line of every TTY start is `safeboot: state (before, after)` (0 → 1 after a
normal start, 1 → 2 for the start after the first pull); no `ERROR`.
**Stuck without TTY:** step 4 (PLAY power-on); or from the console `rtc opsr setb 2 0` and `rtc opsr setb 3 0`.
Send me: `tty_log.txt` and what you saw in 1–5.
"""),
    ("P1-release-flash", "P1 · Flash the release firmware (TTY inside the camera)", {"card": ["rom"]}, f"""\
**Writes flash:** yes: stock 3.00 with the release loader (reads TTY from the camera's internal A: drive, never
from the card at boot) and the TTY files added to A:. Replaces the debug loader. Needs F10 passed.

`rg2_v003.frm` (= `dist/rg2_v003_tty.frm` renamed) SHA-256 `{ROM_SHA}`

1. Still on the debug loader with your card: if the TTY settings or MY-slot recipes are at defaults (F10's battery
   pulls cleared them), set them again in the TTY Menu / Recipes; each change writes `H:/tty/config.txt`. Then copy
   `H:/tty/config.txt` and any own recipes in `H:/tty/recipes/` to the Mac.
2. Card S: format in the camera, root = `card/` (only this `rg2_v003.frm`). Battery full. Menu update as in F1a:
   *New Version 3.00* → **Yes** → "Firmware update complete." → **OK** → the camera switches off.
3. Switch on **without a card**: TTY Menu on Effect, Recipes lists only Stock, frame lines/settings as before.
   (If the settings are back to defaults, the update cleared D:; step 5 brings them back.)
4. Power off. Switch on with the **skip key**: stock camera (no TTY Menu). Power off.
5. Card: format in the camera, create `tty/` and put `config.txt` (and your own recipes in `tty/recipes/`) back.
   Power on: within ~2 s your settings are in force; own recipes appear in Recipes.
6. Shoot, play back, power off and on a few times: start-up as fast as with the loader, no lens bounce.

**Pass:** 2–6 as described; no `H:/tty/tty_log.txt` (the release build writes only `ERROR` lines).
**TTY misbehaves:** skip key → stock; or two battery pulls within 20 s of power-on → stock until a PLAY power-on.
Back to the debug loader: the F5 card (`rg2_v003_loader.frm`) by menu update; to stock: U.
**Rejected / "Error occurred." / no LCD:** as F1a/F5 (card S stays in; card R + combo, F1b).
Send me: what you saw in 2–6, and `tty_log.txt` if it exists.
"""),
    ("P2-release-checks", "P2 · Release firmware: boot paths (no flash)", {"card": []}, """\
**Writes flash:** no. Card = your card from P1 step 5.

1. Power on with **PLAY**: playback; switch to shooting: TTY Menu and your settings there.
2. Camera off, USB cable to the Mac (charging / USB connection): behaves as stock; unplug, power on normally: TTY.
3. Ten power-ons in a row (any mix of card in / out): no lens bounce, no hang.
4. DEV → "Boot stock once": the next power-on is stock, the one after has TTY again.
5. Put a recipe with a built-in's file name (e.g. an edited `portra.txt`) in `tty/recipes/`: Recipes shows your version.

**Pass:** 1–5 as described; no `H:/tty/tty_log.txt`.
Send me: what you saw in 1–5, and `tty_log.txt` if it exists.
"""),
    ("P3-uninstall-reinstall", "P3 · Uninstall to stock, then reinstall the release firmware", {
        "card-stock": ["stock"], "card-tty": ["rom"]}, f"""\
**Writes flash:** yes, twice: stock 3.00 (`card-stock/`, SHA-256 `{STOCK_SHA}`), then the release firmware again
(`card-tty/`, SHA-256 `{ROM_SHA}`).

1. Card S root = `card-stock/`. Menu update as in F1a. Switch on (any card): stock camera, no TTY Menu.
2. Card root = `card-tty/` (format in the camera first). Menu update again. Switch on without a card: TTY is back.
3. Your card from P1 step 5 back in.

**Pass:** 1–2 as described. **Fail:** as P1.
Send me: what you saw.
"""),
    ("X1-settings-export", "X1 · Export the camera's presets as recipes (no flash)", {"card": ["debug"]}, """\
**Writes flash:** no. Runs on the debug loader (F2/F5). If the camera runs the release firmware (P1), flash the debug
loader back first with the F5 card (`F5-loader-v2/card-L/`, menu update as in F5), then come back here.

What is new (all on the card): DEV → "Export settings to card" (and `export` in the developer console) writes the
presets saved in the camera as recipe files into `tty/export/`: MY1–MY3, the My Settings Box entries, the live
settings, Setting1/Setting2 and effects whose sliders were changed. The slot files are only read.

Card root = `card/` (debug card, no `Startup.ttl`). Card R stays out.

1. Stock menus, dial on P: set something distinctive (Image Settings Setting2 with Saturation +2, Contrast -2,
   Sharpness +1, Vignetting Weak; White Balance Color Temperature 6500 K with compensation A2 / G-3 (or the closest
   the grid offers); EV +0.7; Dynamic Range Weak; High-ISO NR Custom; ISO 400; ISO AUTO-HI max 6400 / min 200).
   Save it: MENU → Save My Settings → **MY1**, name `EXPORT1`.
2. Set an effect (Cross Process, Hue Magenta, Saturation -2), WB Daylight with a compensation, EV -1.0; save to
   **MY2**, name `EXPORT2`. If you use My Settings Box, save the same into a Box entry.
3. Recipes → Portra 400 → OK → **MY3** → OK.
4. TTY Menu → DEV → "Export settings to card" → OK. The row shows `N files in tty/export`.
5. Power off. Copy `tty/export/` and `tty/tty_log.txt` to the Mac.
6. Move `tty/export/my1.txt` into `tty/recipes/` (the rest stays on the Mac). Power on, DEV → Reload recipes.
7. Recipes → `EXPORT1` → OK → **MY3** → OK. Dial on MY3: compare it with MY1 in the stock menus, page by page
   (Image Settings and sliders, WB and its compensation, EV, DR, High-ISO NR and its Custom thresholds, ISO, ISO
   AUTO-HI). Live view on MY3 shows stock Setting2 colours again (MY1 held no recipe look).
8. DEV → "Export settings to card" again; copy `tty/export/my3.txt` and `tty/tty_log.txt` to the Mac.

**Pass:** 4 shows the file count; the files are my1, my2, my3 (with Portra's look keys and a comment naming
PORTRA.TXT), live, set1, set2 (+ box0N, fxNN where used); every value of step 1 appears in my1.txt; in 7 every
compared page of MY3 equals MY1 (shooting mode, focus etc. stay MY3's); my3.txt from step 8 has the same keys as
my1.txt apart from the comments; no `ERROR` in the log.
**Fail:** the row shows an error, a value differs, or a file is missing: keep the card as it is.
Send me: `tty/export/` from 5 and 8, `tty_log.txt`, and what differed in 7 (a photo of the page).
"""),
    ("U-uninstall", "U · Uninstall (back to stock 3.00)", {"card": ["stock"]}, f"""\
**Writes flash:** yes, the stock firmware (any time after F2).

`rg2_v003.frm` SHA-256 `{STOCK_SHA}`

1. Card S root = `card/` (only the stock `rg2_v003.frm`). Battery full. Boot with the skip key if TTY misbehaves.
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
    debug = zipfile.ZipFile(ROOT / "dist" / f"gritty-v{version}-debug.zip")
    parts = {
        "stock": {"rg2_v003.frm": stock},
        "loader": {"rg2_v003.frm": loader},
        "rom": {"rg2_v003.frm": need(ROM, ROM_SHA)},
        "probe": {"Startup.ttl": ttl(PROBE)},
        "rescue": {"backup.ttl": ttl(RESCUE)},
        # the debug zip without its firmware: cfw/gr2_cfw.bin + tty/ (the loader is already flashed)
        "debug": {n: debug.read(n) for n in debug.namelist() if not n.endswith("/") and n != "rg2_v003.frm"},
    }
    index = ["# Flash steps", "", f"Built from GR II TTY v{version}. Do them in order; each only after the previous one "
             "passed and you reported the result. Details: `loader/flash/README.md`.", "", COMMON,
             "| Step | Writes flash | Folder |", "|---|---|---|"]
    for folder, title, cards, text in STEPS:
        d = OUT / folder
        d.mkdir(parents=True, exist_ok=True)
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
        print(f"prepared dist/flash/{folder}/ ({', '.join(cards) or 'no card files'})")
    (OUT / "README.md").write_text("\n".join(index) + "\n")


if __name__ == "__main__":
    main()
