# gr2_cfw: custom firmware for the Ricoh GR II

A Magic-Lantern-style extension for the Ricoh GR II with firmware 3.00. The camera gets Ricoh's firmware 3.00 plus a 132-byte loader; at power-on the loader starts the CFW from the card's `cfw/` folder, and the CFW runs from RAM next to the stock firmware. Without the card, or with the shutter half-pressed while switching on, the camera boots stock. The CFW's only lasting changes are settings written through the camera's own setter, always to values the stock menus can select (see *Menus*).

**Status:** every feature below has passed on hardware (2026-10-30 – 11-01), and the flashed loader (M10) passed every install, recovery and start test on 2026-11-01. Next: menus and the recipe look applied while the firmware loads (M10c), then the release firmware with the CFW built in (M11). Roadmap and test history: [the project plan](the project plan).

- Two new Effect-button choices, **CFW Menu** and **Recipes**, in every UI language.
- **CFW Menu:** Auto ISO slowest shutter, max ISO and *keep shutter*; TAv fixes; Auto ISO in M; Snap distances 0.7 m + 1.2 m; frame lines and zebras; Clarity strength and custom high-ISO NR; an INFO page; DEV tools (telnet developer console, screenshots).
- **Recipes:** film looks in a filmstrip, previewed live in the camera's image pipeline and saved into My Settings slots MY1–MY3.

## Install
1. Charge the battery to **full**. Format a card **in the camera** (the firmware refuses card layouts outside the SD specification; FAT32 for 32 GB and smaller).
2. Unzip `gr2_cfw-v<VERSION>-release.zip` (or `-debug.zip`) from the [releases](../../releases) into the card root: `rg2_v003.frm` and `cfw/` (both stages, fonts, recipes, licences). On macOS, delete the `._*` files afterwards. The card must hold no other `rg2_v*.frm`.
3. MENU → Set-up → *Confirm Firmware Version*. It shows **Execute?** with *New Version 3.00* (the camera accepts the same version). Choose **Yes**, wait for **Firmware update complete.**, press **OK** (the camera switches off), switch it on.
4. In *Key Custom Options*, set **Effect Button Setting** and/or **Effect Button Hold Setting** to **CFW Menu** or **Recipes**. With the dial on MY1–MY3 that assignment belongs to the slot: save it with the stock *Save My Settings*, or it is dropped at power-off.

- **Updating** the CFW only needs the new `cfw/` folder; reflash only when the zip's `rg2_v003.frm` changes.
- **Never** put a `Startup.ttl` from older CFW versions on the card: it would load the CFW a second time over the running one.
- **Stock boot:** hold the shutter half-pressed while switching on (until the screen comes on), or remove the card. If the camera hangs, take the battery out and do that.
- **Safe boot:** a start counts as failed unless the CFW runs for 20 s or the camera is switched off normally. After two failed starts in a row the CFW stays off and the camera boots stock until you switch on once with **PLAY**; the next normal start runs the CFW again. DEV → *Boot stock once* gives one stock start.
- **Uninstall:** flash Ricoh's official `rg2_v003.frm` the same way.
- **Rescue** (if the camera no longer boots at all): a camera-formatted card with Ricoh's `rg2_v003.frm` and `loader/flash/rescue/backup.ttl` as `backup.ttl`; dial on MOVIE, shutter fully pressed, POWER; release, turn the dial off MOVIE, wait until the camera restarts by itself (`loader/flash/README.md` §6).

| Build | `H:/cfw/cfw_log.txt` |
|---|---|
| release | Only `ERROR …` lines (a patch refused, a file not writable, …); empty after a normal session |
| debug | Everything: init, every setting write, menu open/close, and at power-off the `cfw status` dump (heartbeat, settings, last 32 key messages) |

## Menus
Both menus are fullscreen and use the fonts in `H:/cfw/fonts` (Inter and Barlow Condensed; without them the CFW logs an error and falls back to the large ROM font).

**CFW Menu.** On the tab row Left/Right switch tabs and Down/OK enter the rows; Up/Down move, Left/Right change the value (applied at once), OK toggles the two-value settings (ON/OFF and the like), Up from the first row returns to the tabs. Rows are grouped by what they do. With the flashed loader the settings are applied during the boot, before the first live-view frame. Changes are saved when the menu closes, to the camera's internal memory (`D:/CFW`, kept across firmware updates) and, as a backup, to `cfw/config.txt` on the card. The camera itself clears its internal `D:` drive after a battery pull or a crash (and *Reset Settings* clears it too); the CFW then restores the settings from the card copy. INFO is read-only (refreshed every second); on DEV, OK runs the selected action.

| Tab | Row | Values | What it does | Survives power-off without the card |
|---|---|---|---|---|
| EXPOSURE | Auto ISO max | **STOCK** (800; 3200 with DR) / ISO 400 … 25600 | Upper ISO limit of ISO AUTO (the DR mode's own cap still applies). Fixed ISOs are unaffected | No (RAM) |
| EXPOSURE | Auto ISO slowest shutter | **STOCK** (1/40) / 1/60 + crop / 1/15 … 1/250 | Shutter speed at which ISO AUTO starts raising the ISO in P/Av/Auto (also AUTO-HI when its slowest shutter is Auto). "1/60 + crop" gives 1/60 at 28 mm, 1/80 at 35 mm crop, 1/100 at 47 mm crop. Not in Movie | No (RAM) |
| EXPOSURE | Auto ISO at max ISO (P, Av) | **SLOWER SHUTTER** / KEEP SHUTTER | Once ISO AUTO is at its max, stock keeps slowing the shutter (down to 1 s / 8 s); KEEP SHUTTER holds *Auto ISO slowest shutter* and lets the picture get darker | No (RAM) |
| EXPOSURE | Auto ISO in M | **OFF** / ON | In M with ISO AUTO or AUTO-HI, the camera picks the ISO from the light (ISO 100 to *Auto ISO max*) instead of using ISO 100. No exposure compensation in M. Takes effect at power-on and when M is entered | No (RAM) |
| EXPOSURE | Auto-Hi slowest shutter | **STOCK** / Auto, 1/2 … 1/250 | ISO Auto-High slowest shutter speed | Yes, like a menu change |
| EXPOSURE | TAv: cap ISO at Auto-Hi max | OFF / **ON** | TAv honours the ISO Auto-High max with any Auto ISO setting (stock uses a hidden ISO 25600 ceiling unless ISO = Auto-Hi and DR compensation is off). DR compensation can still lower the ceiling | No (RAM) |
| EXPOSURE | TAv: Auto-Hi minimum too | **OFF** / ON | In TAv, ISO AUTO behaves like AUTO-HI: the Auto-High minimum (and maximum) apply | No (RAM) |
| FOCUS | Snap distances | **STOCK** / 0.7m + 1.2m | The snap choices "1m" and "1.5m" focus at 0.7 m and 1.2 m (half-press, Full Press Snap, start-up focus, distance display and labels) | No (RAM) |
| VIEW | Frame lines | **OFF** / XPan 65:24 / 6x7 / 4:5 / 1:1 / 16:9 | Darkens live view outside the frame and outlines it, inside the image area of the current aspect ratio. Drawn into the camera's grid-guide layer: shooting screens only, never in photos | No (config) |
| VIEW | Zebras | **OFF** / Y 235+ / Y 245+ / Y 252+ | Animated stripes on live-view areas (4×4 px cells) at or above the brightness threshold, ~10× a second, not while a CFW menu is open | No (config) |
| IMAGE | Clarity strength | **STOCK** / 1–9 | Strength of Effect "Clarity" (5 = none, stock 7) | Yes, like a menu change |
| IMAGE | High-ISO NR | **STOCK** / CUSTOM | High-ISO NR in Manual mode with the three thresholds below | Yes, like a menu change |
| IMAGE | NR weak / medium / strong from | ISO 100 … 25600 (medium/strong also OFF) | ISO where each NR level starts (kept in order) | Yes, like a menu change |
| INFO | Images stored, Shutter exposures, Battery (level + raw ADC, 8 s average), Temperature (sensor, lens), Card (free/total, shots left), Firmware + serial, Uptime, Power-ons | read-only | The camera's own counters and sensors, redrawn once a second when they change | — |
| DEV | Telnet console | **OFF** / ON | The developer console on port 23 (below). ON shows `telnet 192.168.0.1`, or `Wi-Fi off (camera menu)` until Wi-Fi is on. **No password**: anyone on the camera's Wi-Fi gets a shell. Off at every boot | No |
| DEV | Screenshot to card | OK | Closes the menu; ~1.5 s later saves live view + on-screen display as `H:/cfw/shotNNNN.bmp` (640×480) | — |
| DEV | Reload recipes, Save IQ RAM | OK | Rescan `H:/cfw/recipes`; write the image-processing tables to `H:/cfw/iq_ram.bin` | — |
| DEV | Overlay state to log, Overlay test pattern | OK / OFF · RUNNING · ON | Overlay-layer diagnostics: ~1.5 s after the menu closes, log the display-plane state / draw a test pattern into the grid-guide buffer (OK again removes it) | — |
| DEV | Boot stock once | OK | The next power-on is a stock boot (the one after it runs the CFW again) | — |
| DEV | Wi-Fi network / password, Free memory pool | read-only | Wi-Fi credentials (while on), free pool memory and the stage-2 load address | — |

STOCK hands a property back to the camera and restores the value it had before the CFW changed it (unless it was changed in the camera menu meanwhile). Active settings are re-applied after a mode-dial or My Settings change. Outside MY modes the camera saves properties at power-off, so a stock boot keeps the last value.

## Recipes
**Filmstrip.** The strip sits just above the bottom of the live-view image; the rest of live view shows the selected recipe's look at once while you browse (in Standard, Vivid, Setting1 or Setting2; other modes keep their own look). Left/Right browse; OK, then Left/Right pick MY1/MY2/MY3 and OK saves the recipe into that My Settings slot (Up/Down goes back). The slot keeps its other settings and takes the recipe's name and sliders; the CFW remembers the recipe file per slot and shows its look while that slot and its base Image Setting are active (without the card the slot keeps the sliders, not the look).

Each frame shows four patches, one per hue quadrant (magenta, red, green, cyan-blue), rendered through the recipe's look with the change exaggerated 2×. The film base is slightly transparent; frames and sprocket holes are solid.

**Files.** One ASCII file per recipe in `H:/cfw/recipes/` (8.3 name, CRLF, `#` comments, ≤ 4 KB, up to 24 files). The shipped films are generated from [a third-party GR III project GR III fits (spektrafilm, CC BY-SA 4.0; Classic Neg: FujifilmCameraProfiles, CC BY-NC-SA 4.0, non-commercial); licence texts in `recipes/licenses/`.

| Key | Values | Writes (per My Settings slot) |
|---|---|---|
| `name` (required) | up to 23 characters | filmstrip frame and slot name |
| `base` (required, before every key except `name`) | `standard`, `vivid`, `setting1`, `setting2` (Image Settings) or an effect: `bright`, `bw`, `bw_te`, `hicontrast_bw`, `cross_process`, `positive_film`, `bleach_bypass`, `retro`, `miniature`, `high_key`, `slight`, `clarity`, `brilliance`, `vibrant`, `hdr_tone`, `portrait` | Image Settings / Effect |
| `saturation`, `contrast`, `sharpness` | −4 … +4 (where the stock menu offers it for that base) | that mode's slider |
| `vignetting` | `off`, `weak`, `medium`, `strong` | that mode's vignetting |
| `hue` | Cross Process: `basic`, `magenta`, `yellow`; Bleach Bypass: `normal`, `warm`, `cool` | that mode's hue |
| `toning` | B&W (TE): `sepia`, `red`, `green`, `blue`, `purple`; Miniature/Clarity/HDR Tone: `bw`, `sepia`, `color` | that mode's toning |
| `clarity` / `glossiness` | 1 … 9 (`clarity` / `brilliance` base only; 5 = none) | Clarity / Glossiness |
| `wb` | `auto`, `multi_auto`, `daylight`, `shade`, `cloudy`, `incandescent1`, `incandescent2`, `fl_daylight`, `fl_neutral`, `fl_cool`, `fl_warm`, `cte`, `kelvin` | White balance |
| `wb_kelvin` | 2500 … 10000 (required with `wb=kelvin`) | Colour temperature |
| `wb_ab`, `wb_gm` | −7 … +7 (amber +/blue −, green +/magenta −; needs `wb`) | WB compensation |
| `exposure` | e.g. `+0.7`, `-1.0` (EV, rounded to 1/3) | Exposure compensation |
| `dr` | `off`, `weak`, `medium`, `strong`, `auto` | Dynamic range compensation |
| `nr` | `off`, `weak`, `medium`, `strong`, `auto` | High-ISO NR |

**Look keys** (`standard`, `vivid`, `setting1`, `setting2` bases only; applied by the CFW in the image-processing tables, not stored in the slot), all relative to the camera's Standard rendering so the contrast and saturation sliders still work on top:

| Key | Values | Stage |
|---|---|---|
| `matrix` | 9 integers −511 … 511, ×128, rows R, G, B | colour matrix in linear RGB (128,0,0,0,128,0,0,0,128 = Standard) |
| `tone`, `tone_r`, `tone_g`, `tone_b` | 33 integers 0 … 255: output at Standard tone codes 0, 8, …, 256 | tone curve per channel (`tone` = all three; 0,8,…,248,255 = Standard) |
| `post_matrix` | 9 integers −511 … 511, ×128 | 3×3 on display RGB after the curves |
| `hue1` … `hue12` | 9 integers −511 … 511, ×128 each | per hue-sector 3×3 in linear RGB; sectors by Cb/Cr angle: 1–3 magenta, 4–6 red/orange/yellow, 7–9 yellow-green/green, 10–12 cyan/blue |

An invalid file is skipped and logged (`ERROR recipes: invalid line in FILE.TXT <line>`). Test recipes (names start with `~`, listed last): `lvtest.txt` swaps red and blue; `t_curve.txt` holds the red curve dark; `t_post.txt` doubles saturation; `t_hueq1.txt` … `t_hueq4.txt` keep one hue quadrant and grey the rest.

## Developer console
DEV → *Telnet console* starts the CFW's telnet server on port 23 (`telnet 192.168.0.1` once Wi-Fi is on in the camera menu and you are on the camera's network); OK again stops it. It replaces the stock telnet shell: stock commands still work, and their `printf` output is captured (with the CFW log) in a 16 KiB ring buffer instead of going to the serial port.

| Command | Output |
|---|---|
| `help` | command list |
| `status` | version and build, uptime, capture state, every CFW setting |
| `log [bytes]` | the last captured output (default 2048 bytes) |
| `tail [seconds]` | new output as it arrives (default 10 s, max 300) |
| `sh <command>` | a stock shell command, its output and `result: N <text>` |
| `tasks` | RTOS tasks: id, name, state, priority, stack |
| `heap` | GlobalHeap size/free/largest block, memory pools |
| `mem <addr> [len]` | hex + ASCII dump (hex arguments, ≤ 0x400 bytes, DRAM `0xA0000000–0xBFFFFFFF` or its `0x2…` alias) |
| `wifi` | Wi-Fi state as the system controller sees it |
| `exit` | closes the session |
| anything else | runs as a stock shell command (e.g. `ver`) |

Switching the console off restores `printf` to the serial port. The stock `attach` command uses the same hook: `sh attach start …` replaces the capture.

## Building
Requirements: `arm-none-eabi-gcc` (GCC 16 locally, Ubuntu's `gcc-arm-none-eabi` in CI), Python ≥ 3.12, and for the tests `pip install -r requirements-dev.txt`.

```sh
make            # bin/{release,debug}/gr2_cfw.bin (stage 1) + gr2_main.bin (stage 2)
make dist       # ISA audit + loader .frm + dist/gr2_cfw-v<VERSION>-{release,debug}.zip, SHA256SUMS.txt, RELEASE_NOTES.md
make test       # lint + ISA audit + emulator tests + loader .frm check (what CI runs)
make help       # every target
```

- **Stages.** Stage 1 (`gr2_cfw.bin`, loader + hook veneers) runs in the 32 KiB window `0xA0017000–0xA001EFFF`, the erased tail of the IPL copy in DRAM. It loads stage 2 (`gr2_main.bin`, everything else) into a heap block and relocates it; stage 2 is linked twice and `scripts/mkmain.py` turns the difference into its relocation table.
- **Target.** ARMv5TE, big-endian, Thumb-1, freestanding, no libgcc; `make audit` rejects anything else.
- **Firmware inputs.** `make fw` puts Ricoh's stock `rg2_v003.frm` (a local copy, or the official download, SHA-256 checked) and its extracted images into `fw/` (never committed). The emulator tests, the loader build and `make dist` need it; the zips carry the loader `.frm` built from it.
- **Development without the loader:** `loader/Startup.ttl` (not shipped) loads the CFW the old way on a stock camera with `PROPID_DEVEL_MODE` = 1; never use it on a camera with the loader.
- **CI** (`.github/workflows/ci.yml`): `make WERROR=1 test` and `make dist` on every push and pull request; uploads the SD zips, ELFs and emulator renders.
- **Releases** (`.github/workflows/release.yml`): bump `VERSION` on main, then push the tag `v<VERSION>`; the workflow tests, packages and publishes the zips.

## Repository layout
| Path | Contents |
|---|---|
| `src/`, `include/` | CFW sources; `include/gr2_hw.h` holds every firmware address the CFW uses |
| `gr2_boot.ld`, `gr2_main.ld` | Stage-1 window and stage-2 link scripts |
| `loader/` | `Startup.ttl` (development loader for stock cameras, not shipped), `probe/` (window check), `flash/` (M10 flashed loader: stub, rescue script, test plan) |
| `recipes/`, `fonts/` | Shipped recipe files and overlay fonts, with their licences |
| `scripts/` | `package_sd.py` (zips), `mkmain.py` (stage-2 relocations), `fetch_firmware.py` (`make fw`), `build_loader_frm.py` + `emu_loader.py` (`make loader`), `flash_kit.py` (`make flash-kit`: per-step card folders for the flash tests), `loader_timing.py`, `check_probe.py`, `make_fonts.py` |
| `tests/emu/` | Unicorn emulator tests of both builds against the stock firmware |
| `tools/gr2frm.py` | `.frm` container tool (unpack, checksum, repack) |

Reverse-engineering notes live in the separate `the reverse-engineering notes` repository (findings 01–26).

## Licence
MIT for the CFW code ([LICENSE](LICENSE)). Fonts (SIL OFL 1.1) and film recipes (CC BY-SA 4.0 / CC BY-NC-SA 4.0) keep their own licences. The release zips' `rg2_v003.frm` contains Ricoh's firmware 3.00 (Ricoh's property, not covered by the MIT licence) with the loader patch. Not affiliated with Ricoh, Kodak or Fujifilm; use at your own risk.
