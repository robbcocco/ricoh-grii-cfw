# Ricoh GR II Custom Firmware (gr2_cfw)

A Magic-Lantern-style extension for the Ricoh GR II (firmware 3.00). Today's builds load from the SD card, run from RAM next to the stock firmware and never write flash: remove the card and the camera boots stock (apart from settings changed through the property setter, see the project plan §3). Planned: the release becomes a flashed firmware with safe boot; the SD loader stays for debug (the project plan M10–M12).

**Status:** M0–M9 passed on hardware (2026-10-30 – 11-01): resident task, Effect-button hook, settings with restore, TAv ISO fix, the two Effect-button menus (**CFW Menu**, **Recipes**), the fullscreen UI with instant live-view recipe preview, the two-stage loader, recipes v2 (tone curves, post matrix, hue sectors) with filmstrip swatches, Auto ISO slowest shutter/max/keep shutter, INFO, screenshot, snap distances, VIEW tab (frame lines, zebras), TAv Auto-Hi minimum, Auto ISO in M, the developer console, Japanese labels and the empty release log. Wi-Fi is turned on in the camera menu; the DEV tab only starts telnet and the console. Design and milestones: [the project plan](the project plan).

## Target
- CPU: ARMv5TE (ARM946E-S-class, MPU), big-endian, Thumb-1 only.
- Stage 1 (`cfw/gr2_cfw.bin`, loader + hook veneers, ~1 KB of code) runs in the window `0xA0017000–0xA001EFFF` (32 KiB). This is the erased 0xFF tail of the IPL copy in DRAM, unused by the running firmware in a live dump. Confirm it on your camera with the M0 probe before loading code.
- Stage 2 (`cfw/gr2_main.bin`, everything else) is loaded by stage 1 into a heap block and relocated there, so its size is not limited by the window.

## Build
```sh
make            # bin/<variant>/gr2_cfw.bin (stage 1) and gr2_main.bin (stage 2), variants release and debug
make disasm     # bin/<variant>/gr2_{cfw,main}.asm
make dist       # dist/gr2_cfw-v<VERSION>-release.zip, -debug.zip, SHA256SUMS.txt, RELEASE_NOTES.md
make check      # local pre-flight gates (stock frm, NAND backup)
```
Requires `arm-none-eabi-gcc`. Flags: `-march=armv5te -mthumb -mthumb-interwork -mbig-endian -Os`, freestanding, no libgcc. Stage 2 is linked twice (at 0 and at 0x10000000); `scripts/mkmain.py` turns the words that differ into its relocation table and fails the build on anything that is not a plain absolute address.

| Variant | `H:/cfw/cfw_log.txt` |
|---|---|
| release | Only `ERROR …` lines (a patch refused, a file not writable, …). Nothing on a normal session. |
| debug | Everything: init steps, every setting write, menu open/close, and at power-off the `cfw status` dump (heartbeat, settings, last 32 key messages). |

CI (`.github/workflows/build.yml`) builds both on every push and uploads the zips; `release.yml` (manual) bumps `VERSION` and publishes them as a GitHub release.

## Install
Prerequisite: `PROPID_DEVEL_MODE = 1` (see finding 01), so `H:/Startup.ttl` runs at boot.

1. **Probe once per camera (data only).**
   - Copy `loader/probe/Startup.ttl` and `loader/probe/Shutdown.ttl` to the card root.
   - Power on, use the camera normally (shoot, play back, menus), then power off.
   - Copy `H:/cfw/probe/*.BIN` to the maintainer's hardware data and run `python3 scripts/check_probe.py`. Continue only if it prints `PROBE PASSED`.
2. **CFW.** Unzip `gr2_cfw-v<VERSION>-release.zip` (or `-debug.zip`) into the card root. It creates `Startup.ttl`, `Shutdown.ttl`, `cfw/gr2_cfw.bin`, `cfw/gr2_main.bin`, `cfw/fonts/` and `cfw/recipes/` (film recipes and their licences). On macOS, remove `._*` files afterwards.
3. In *Key Custom Options*, set **Effect Button Setting** and/or **Effect Button Hold Setting** to **CFW Menu** or **Recipes**. With the dial on MY1–MY3 the assignment belongs to that My Settings slot: save the slot (stock "Save My Settings"), or it is dropped at power-off.

If the camera hangs or reboots: remove the card. The CFW lives only in RAM, and the next boot is stock. Load once per power cycle. All `.ttl` files are ASCII with CRLF line endings. Over telnet, `cfw status` returns the heartbeat as `result: <n>`.

### Hardware test of this build (debug zip)
Results 2026-11-01 (debug zip, five rounds): screenshot, snap 0.7/1.2, overlay state log, test pattern (toggle with OFF / RUNNING / ON), frame lines, zebras, TAv Auto-Hi minimum, Auto ISO in M, KEEP SHUTTER (with `+0xCC`), the developer console on port 23 with on/off, Japanese labels, no stray texts, the empty release log, the BAK recovery path, the filmstrip without the top band and without a flash on close all confirmed. Wi-Fi comes from the camera menu. Changed after the last round: the filmstrip sits 24 px higher (3:2: y 300–427), its colours are programmed before anything is drawn (the green flash on opening), and long names wrap to two lines. Still to check with the current debug zip:
1. **Filmstrip**: open and close Recipes a few times, no green flash either way; height in 3:2 (and 16:9 if you use it); *Superia X-TRA 400* on two lines; scrolling quickly shows no colour flicker on the frames.

## Menus
Both menus are fullscreen, drawn with the fonts in `H:/cfw/fonts` (Inter and Barlow Condensed rendered by `scripts/make_fonts.py`; without them the CFW logs an error and falls back to the large ROM font).

**CFW Menu** (tabbed, `design/ui-proposals/cfw-menu.jpg`). On the tab row Left/Right switch tabs and Down/OK enter the rows; Up/Down move, Left/Right/OK change the value (applied at once), Up from the first row returns to the tabs. Changes are saved to `H:/cfw/config.txt` when the menu closes. INFO is read-only (refreshed every second); on DEV, OK runs the selected action.

| Tab | Row | Values | What it does | Survives power-off without the card |
|---|---|---|---|---|
| EXPOSURE | TAv: cap ISO at Auto-Hi max | OFF / **ON** | TAv mode honours ISO Auto-High max with any Auto ISO setting (stock uses a hidden ISO 25600 ceiling unless ISO = Auto-Hi and DR compensation is Off). DR compensation can still lower the ceiling | No (RAM) |
| EXPOSURE | Auto ISO slowest shutter | **STOCK** (1/40) / 1/60 + crop / 1/15 … 1/250 | Shutter speed at which ISO AUTO starts raising the ISO in P/Av/Auto (also AUTO-HI when its slowest shutter is Auto). "1/60 + crop" gives 1/60 at 28 mm, 1/80 at 35 mm crop, 1/100 at 47 mm crop. Not in Movie | No (RAM) |
| EXPOSURE | Auto ISO max | **STOCK** (800; 3200 with DR) / ISO 400 … 25600 | Upper ISO limit of ISO AUTO (with or without DR compensation; the DR mode's own cap still applies). Fixed ISOs are unaffected | No (RAM) |
| EXPOSURE | Auto ISO at max ISO (P, Av) | **SLOWER SHUTTER** / KEEP SHUTTER | What P, Av (and Auto) do once ISO AUTO has reached its max: stock keeps slowing the shutter (down to 1 s / 8 s); KEEP SHUTTER holds the *Auto ISO slowest shutter* (1/40 if STOCK) and lets the picture get darker | No (RAM) |
| EXPOSURE | Auto-Hi slowest shutter | **STOCK** / Auto, 1/2 … 1/250 | ISO Auto-High slowest shutter speed | Yes, like a menu change |
| EXPOSURE | TAv: Auto-Hi minimum too | **OFF** / ON | In TAv, ISO AUTO behaves like AUTO-HI: the ISO Auto-High minimum (and maximum) apply. The ISO setting itself is unchanged | No (RAM) |
| EXPOSURE | Auto ISO in M | **OFF** / ON | In M with ISO AUTO or AUTO-HI, the camera picks the ISO from the light (between ISO 100 and *Auto ISO max*, ISO 800 if STOCK) instead of using ISO 100. Exposure compensation is not applied in M. Takes effect when M is entered | No (RAM) |
| FOCUS | Snap distances | **STOCK** / 0.7m + 1.2m | The snap choices "1m" and "1.5m" focus at 0.7 m and 1.2 m instead (half-press, Full Press Snap, start-up focus, distance display and labels) | No (RAM) |
| VIEW | Frame lines | **OFF** / XPan 65:24 / 6x7 / 4:5 / 1:1 / 16:9 | Darkens live view outside the chosen frame and draws a white line on its edges, inside the image area of the current aspect ratio. Drawn into the camera's own grid-guide layer: shown in the shooting screens, gone in menus and playback, never in photos | No (config) |
| VIEW | Zebras | **OFF** / Y 235+ / Y 245+ / Y 252+ | Animated stripes on live-view areas (4×4 px cells) whose brightness reaches the threshold; updated ~10× a second, not while a CFW menu is open | No (config) |
| IMAGE | Clarity strength | **STOCK** / 1–9 | Strength of Effect "Clarity" (5 = none, stock default 7); other effects are unaffected | Yes, like a menu change |
| IMAGE | High-ISO NR | **STOCK** / CUSTOM | High-ISO NR in Manual mode with the three thresholds below | Yes, like a menu change |
| IMAGE | NR weak / medium / strong from | ISO 100 … 25600 (medium/strong also OFF) | ISO where weak / medium / strong NR starts (kept in order) | Yes, like a menu change |
| INFO | Images stored, Shutter exposures, Battery (level + raw ADC, 8 s running average), Temperature (sensor, lens), Card (free/total, shots left), Firmware + serial, Uptime, Power-ons | read-only | from the camera's own counters and sensors; changed values are redrawn once a second | — |
| DEV | Telnet console | **OFF** / ON | Switches the CFW developer console on port 23 on or off (see "Developer console"). ON shows `telnet 192.168.0.1`, or `Wi-Fi off (camera menu)` until Wi-Fi is turned on in the camera menu. **No password**: anyone on the camera's Wi-Fi gets a full shell. Off at every boot | No |
| DEV | Screenshot to card | OK | Closes the menu and ~1.5 s later saves live view + the camera's on-screen display as `H:/cfw/shotNNNN.bmp` (640×480) | — |
| DEV | Reload recipes, Save IQ RAM | OK | Rescan `H:/cfw/recipes`; write the camera's image-processing tables to `H:/cfw/iq_ram.bin` | — |
| DEV | Overlay state to log, Overlay test pattern | OK | Hardware checks for the overlay layer (finding 23 §4.5): close the menu, then ~1.5 s later log the display-plane state / draw a test pattern into the shown grid-guide buffer (OK again removes it) | — |
| DEV | Wi-Fi network / password, Free memory pool | read-only | the camera's Wi-Fi credentials (while on), free pool memory and the stage-2 load address | — |

STOCK hands the property back to the camera and restores the value it had before the CFW changed it (unless you changed it in the camera menu meanwhile). With the CFW loaded, active settings are re-applied after a mode-dial or My Settings change. Outside MY modes the camera saves property settings at power-off, so a stock boot keeps the last value, which is always one the stock menu can select.

**Recipes** (filmstrip, `design/ui-proposals/recipes-B-filmstrip.jpg` without the name line). The filmstrip sits 24 px above the bottom of the live-view image (above the black bar the camera leaves under 3:2 and 16:9 live view); the rest of live view stays visible and shows the selected recipe's look at once while you browse (in Standard, Vivid, Setting1 or Setting2; other modes keep their own look). Names too wide for a frame wrap to two lines. Left/Right browse, OK, then Left/Right pick MY1/MY2/MY3 in the band that appears above the strip and OK saves the recipe into that My Settings slot (Up/Down goes back); the band then shows the result until the next key. The slot keeps its other settings (exposure mode, ISO, buttons, …) and takes the recipe's name and sliders; the CFW remembers which recipe file the slot holds and shows its look while that slot is selected and the recipe's base Image Setting is active (without the card the slot keeps the sliders, without the look).

Each filmstrip frame shows four soft patches, one per hue quadrant of the camera's hue-sector stage (magenta, red, green, cyan-blue), as Standard renders them, passed through the recipe's colour matrix, the hue sector of that colour, tone curves and post matrix, with the change exaggerated 2× so the look is visible at a glance (B&W bases show grey). The film base is slightly transparent (live view shows through it), the frames and sprocket holes are solid. The patch and film colours are exact: the overlay reprograms 21 of its 28 palette entries while it is open (each recipe keeps its entries while it stays on screen, so a scroll changes at most three recipes' patches) and restores them on close; colours are always set before the pixels that use them are drawn and removed before the colours are restored.

## Developer console
DEV → *Telnet console* starts the CFW's telnet server on port 23 (`telnet 192.168.0.1` once Wi-Fi is on in the camera menu and you are connected to the camera's network); OK again stops it. It replaces the stock telnet shell: stock commands still work (typed directly or with `sh`), and unlike the stock shell, which answered only `result: N` because command output goes to the serial port, the console captures all `printf` output and CFW log lines in a 16 KiB ring buffer and shows it:

| Command | Output |
|---|---|
| `help` | command list |
| `status` | CFW version and build, uptime, capture state, every CFW setting |
| `log [bytes]` | the last captured output (default 2048 bytes) |
| `tail [seconds]` | new output as it arrives (default 10 s, max 300) |
| `sh <command>` | runs a stock shell command and prints its output plus `result: N <text>` |
| `tasks` | RTOS tasks: id, name, state, priority, stack top and size |
| `heap` | GlobalHeap size/free/largest block, memory pools, the pool map |
| `mem <addr> [len]` | hex + ASCII dump, hex arguments, at most 0x400 bytes, DRAM `0xA0000000–0xBFFFFFFF` (or its `0x2…` alias) only |
| `wifi` | Wi-Fi state as the system controller sees it (modes, request, status, job state); stock `wifi` is `sh wifi` |
| `exit` | closes the session |
| anything else | runs as a stock shell command, like `sh` (e.g. `ver`) |

While the console is on, `printf` output no longer reaches the serial port (switching it off restores that). The stock `attach` command uses the same hook slot: running `sh attach start …` replaces the capture. No password.

## Recipes
One ASCII text file per recipe in `H:/cfw/recipes/` (8.3 file name, CRLF, `#` comments, at most 4 KB, up to 24 files). The shipped film files in `recipes/` are generated by the maintainer's recipe tool from [a third-party GR III project GR III fits (spektrafilm, CC BY-SA 4.0; Classic Neg: FujifilmCameraProfiles, CC BY-NC-SA 4.0, non-commercial). Licence texts: `recipes/licenses/`.

| Key | Values | Writes (per My Settings slot) |
|---|---|---|
| `name` (required) | up to 23 characters | filmstrip frame and slot name |
| `base` (required, before every key except `name`) | `standard`, `vivid`, `setting1`, `setting2` (Image Settings) or an effect: `bright`, `bw`, `bw_te`, `hicontrast_bw`, `cross_process`, `positive_film`, `bleach_bypass`, `retro`, `miniature`, `high_key`, `slight`, `clarity`, `brilliance`, `vibrant`, `hdr_tone`, `portrait` | Image Settings / Effect |
| `saturation`, `contrast`, `sharpness` | −4 … +4 (only where the stock menu offers it for that base) | that mode's slider |
| `vignetting` | `off`, `weak`, `medium`, `strong` | that mode's vignetting |
| `hue` | Cross Process: `basic`, `magenta`, `yellow`; Bleach Bypass: `normal`, `warm`, `cool` | that mode's hue |
| `toning` | B&W (TE): `sepia`, `red`, `green`, `blue`, `purple`; Miniature/Clarity/HDR Tone: `bw`, `sepia`, `color` | that mode's toning |
| `clarity` / `glossiness` | 1 … 9 (`clarity` / `brilliance` base only; 5 = none) | Clarity / Glossiness |
| `wb` | `auto`, `multi_auto`, `daylight`, `shade`, `cloudy`, `incandescent1`, `incandescent2`, `fl_daylight`, `fl_neutral`, `fl_cool`, `fl_warm`, `cte`, `kelvin` | White balance |
| `wb_kelvin` | 2500 … 10000 (required with `wb=kelvin`) | Colour temperature |
| `wb_ab`, `wb_gm` | −7 … +7 (amber +/blue −, green +/magenta −; needs `wb`) | WB compensation of that WB mode |
| `exposure` | e.g. `+0.7`, `-1.0` (EV, rounded to 1/3) | Exposure compensation |
| `dr` | `off`, `weak`, `medium`, `strong`, `auto` | Dynamic range compensation |
| `nr` | `off`, `weak`, `medium`, `strong`, `auto` | High-ISO NR |

**Look keys** (`standard`, `vivid`, `setting1`, `setting2` bases only; applied by the CFW in the camera's image-processing tables, not stored in the slot). All are relative to the camera's own Standard rendering, so the contrast and saturation sliders keep working on top:

| Key | Values | Stage |
|---|---|---|
| `matrix` | 9 integers −511 … 511, ×128, rows R, G, B | colour matrix in linear RGB (128,0,0,0,128,0,0,0,128 = Standard) |
| `tone`, `tone_r`, `tone_g`, `tone_b` | 33 integers 0 … 255: output at Standard tone codes 0, 8, 16, …, 256 | film tone curve per channel (`tone` = all three; 0,8,…,248,255 = Standard). Black lift, highlight roll-off/paper white, split toning |
| `post_matrix` | 9 integers −511 … 511, ×128 | 3×3 on display RGB after the curves (hue rotation, per-axis saturation) |
| `hue1` … `hue12` | 9 integers −511 … 511, ×128 each | per hue-sector 3×3 in linear RGB (sectors not listed stay unchanged). Sectors by Cb/Cr angle from +Cb towards +Cr: 1–3 magenta, 4–6 red/orange/yellow, 7–9 yellow-green/green, 10–12 cyan/blue (quadrants confirmed on hardware) |

An invalid file is skipped and logged (`ERROR recipes: invalid line in FILE.TXT <line>`).

Test recipes (names start with `~`, listed last; delete them after testing): `lvtest.txt` swaps red and blue; `t_curve.txt` holds the red curve dark (live view turns cyan); `t_post.txt` doubles saturation with the post matrix; `t_hueq1.txt` … `t_hueq4.txt` turn every hue grey except one quadrant of hue-sector tables (Q2 keeps reds, Q3 greens, Q4 blues, Q1 magentas).

## Layout
| Path | Purpose |
|---|---|
| `src/entry.S`, `src/boot.c`, `include/boot.h`, `gr2_boot.ld` | Stage 1 (window): zero `.bss`, window canary, load `H:/cfw/gr2_main.bin` into the heap, apply its relocations, call it; loader ABI v1 header and section-load hook for the flashed debug loader (M10b) |
| `src/main.c`, `gr2_main.ld`, `scripts/mkmain.py` | Stage 2 entry (command registration, menu/hook/input install, task start); its link script and relocation packer |
| `src/menu.c` | "CFW Menu" / "Recipes" choices in Effect Button / Effect Button Hold menus (RAM records, extended string tables) |
| `src/hooks.c`, `src/veneers.S`, `src/arm.S` | Effect press/hold BL retargets (verify-first, one atomic halfword store each, fixed-address veneers in stage 1 jumping through target words) |
| `src/ui.c` | Both overlays: tabbed CFW Menu (settings, INFO, DEV) and Recipes filmstrip (CFW task only) |
| `src/settings.c`, `include/settings.h` | Settings: property overrides with recorded stock values; TAv cap, Auto ISO, Snap and ISO getter-hook RAM patches (findings 18, 21) |
| `src/info.c`, `include/info.h` | INFO and DEV tabs: counters, battery, temperatures, card; console on/off, screenshot, recipe rescan, IQ dump, overlay checks (finding 21) |
| `src/overlay.c`, `include/overlay.h` | VIEW tab: frame lines and zebras stamped into the camera's full plane (layer 2) after every stock flip (finding 23, Tier A) |
| `src/console.c`, `include/console.h` | Developer console: telnet server on port 23 (on/off), printf capture hook + CFW log mirror in a 16 KiB ring, commands (finding 21 §3.2, §5) |
| `src/recipes.c`, `include/recipes.h` | Recipes: folder scan, parser, My Settings slot write, which look is shown (finding 19) |
| `src/look.c`, `include/look.h` | Recipe looks in IQ RAM: section copies, per-mode list repointing, matrix/hue/tone/YCbCr records, live-view refresh (finding 20) |
| `src/mem.c` | `memset`/`memcpy` for compiler-generated calls |
| `recipes/` | Shipped recipe files (packaged as `cfw/recipes/`) and `licenses/` |
| the maintainer's recipe tool | Generates the film recipe files from the GR III fits |
| `fonts/` | Overlay fonts (`*.fnt`, packaged as `cfw/fonts/`) and their OFL `licenses/` |
| `design/ui-proposals/` | UI mockups (640×480 OSD, stock palette, shown 2×); the CFW Menu and proposal B are implemented |
| `src/config.c`, `include/config.h` | `H:/cfw/config.txt` load/save |
| `src/osd.c`, `include/osd.h` | Draw lock, private OSD buffer + layer switch, fills, anti-aliased text from the `.fnt` fonts (ROM-font fallback), palette nearest-colour |
| `src/input.c` | Key capture via the APP system-message handler pointer (pass-through when closed) |
| `src/shell.c` | `cfw` shell command (registered in the RTOS shell registry) |
| `src/task.c`, `include/cfw.h` | Resident "CFW" task: 20 ms event/key poll, 200 ms settings poll, 1 s heartbeat |
| `src/log.c`, `include/cfw_log.h` | Log to `H:/cfw/cfw_log.txt`: `ERR` always, `DBG` in debug builds only |
| `include/gr2_hw.h` | Verified RTOS addresses, property IDs, menu record layout |
| `include/gr2_fmt.h` | Division-free string/hex formatting |
| `loader/` | `Startup.ttl` (payload loader), `Shutdown.ttl` (`cfw status`), `probe/` (M0 data-only probe) |
| `scripts/package_sd.py` | SD zips per variant, checksums, release notes (`make dist`) |
| `scripts/make_fonts.py` | Rasterises Inter / Barlow Condensed into the `.fnt` files (needs Pillow and the TTFs) |
| `scripts/bump_version.py` | SemVer bump of `VERSION` (release workflow) |
| `scripts/check_probe.py` | Offline check of probe dumps |
| `scripts/build_release.py` | Boot-logo `.frm` builder (separate flashing experiment, not part of the CFW, not run by make or CI) |
| `loader/flash/`, `scripts/build_loader_frm.py`, `scripts/emu_loader.py`, `scripts/loader_timing.py` | M10 flashed loader, prepared and never flashed: stub source, `.frm` builder, emulation, boot-timing probe. Not run by make or CI; read `loader/flash/README.md` first |
| `tools/` | Synced copies of the reverse-engineering notes |
