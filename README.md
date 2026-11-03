# GR II TTY

Gritty: custom firmware for the Ricoh GR II (firmware 3.00). TTY for short.

**Website:** [robbcocco.github.io/gritty](https://robbcocco.github.io/gritty/): download, install guide, FAQ and a LUT-to-recipe converter. **Source and releases:** [github.com/robbcocco/gritty](https://github.com/robbcocco/gritty).

GR II TTY adds the things the GR II never got: film recipes you can preview live and save into the MY slots, smarter Auto ISO, closer Snap distances, frame lines and zebras, and a few tools for tinkerers. It installs like an official Ricoh update, through the camera's own menu, and it is Ricoh's firmware 3.00 underneath: everything stock keeps working, and nothing it saves can confuse a stock camera.

**For:** Ricoh GR II only, firmware **3.00**. Not for the GR, GR III or GR IIIx.

> **Use at your own risk.** Installing custom firmware can, in the worst case, leave a camera that needs the rescue procedure below. Every step here was tested on a real GR II, but no warranty is given, and this project has nothing to do with Ricoh.

## What you get
- **Two new choices for the Effect button:** **TTY Menu** and **Recipes**, in every menu language.
- **Recipes:** film looks (Portra, Gold, Ektar, Pro 400H, Superia, UltraMax, Vision3, Cinema Green, Cinema Yellow, Cinema Warm, Vivid Slide …), a rangefinder colour look and three black-and-white looks in a filmstrip. Live view shows the look while you browse; one press saves it into MY1, MY2 or MY3, and the camera shows that look whenever the slot is active, from the very first frame after power-on. The last entry, **Stock**, puts a slot back to the camera's own rendering. Your own presets (MY1–MY3, My Settings Box, Setting1/Setting2) export to recipe files with one press.
- **TTY Menu:** Auto ISO slowest shutter, maximum ISO and *keep shutter*; Auto ISO in M; TAv fixes; Snap distances 0.7 m and 1.2 m; frame lines (XPan, 6×7, 4:5, 1:1, 16:9) and zebras; Clarity strength and custom high-ISO noise reduction; an INFO page with shutter count, battery, temperatures; developer tools.
- **Fast:** TTY lives inside the camera and starts with it; power-on is as quick as stock.
- **Safe:** hold a key to start stock, automatic fallback to stock after failed starts, and a full uninstall with Ricoh's own file.

## Before you start
- A **full battery** (the camera refuses updates on a low battery), and ideally a second one.
- An SD card of 32 GB or less, **formatted in the camera** (MENU → Set-up → Format). Cards formatted on a computer may not be accepted.
- Your camera on Ricoh's firmware **3.00** (MENU → Set-up → *Confirm Firmware Version*). If it shows an older version, install [Ricoh's 3.00 update](https://www.ricoh-imaging.co.jp/english/support/digital/firmware/rg2_v003.zip) first, the same way as below.
- Read *Recovery* below once, so you know the key combinations before you need them.

## Install
1. Download `gritty-v<VERSION>.zip` from the [releases](https://github.com/robbcocco/gritty/releases) and unzip it.
2. Copy **only `rg2_v003.frm`** to the root of the freshly formatted card. On a Mac, delete the hidden `._*` files afterwards (`find /Volumes/<card> -name '._*' -delete`). There must be no other `rg2_v*.frm` on the card.
3. Put the card in the camera, switch it on, and open MENU → Set-up → **Confirm Firmware Version**. After a few seconds it asks **Execute?** with *Current Version 3.00* and *New Version 3.00* (installing the same version number is normal).
4. Choose **Yes**. The camera checks the file and rewrites the program; don't touch it until **Firmware update complete.** appears. Press **OK**; the camera switches off.
5. Switch on. Then copy the zip's `tty/` folder (your recipes) to the card root; you can delete `rg2_v003.frm` from the card now.
6. Assign the menus: MENU → *Key Custom Options* → **Effect Button Setting** and/or **Effect Button Hold Setting** → **TTY Menu** or **Recipes**. (With the mode dial on MY1–MY3 that assignment belongs to the slot: save it with the stock *Save My Settings*.)

That's it. Press the Effect button and the TTY Menu or the Recipes filmstrip opens.

## Using it

### Recipes
Press the Effect button you assigned to **Recipes**.
- **Left/Right** browse the films. Live view shows each look at once (in the Standard, Vivid, Setting1 or Setting2 image settings).
- **OK**, then **Left/Right** to choose **MY1 / MY2 / MY3**, then **OK** to save. **Up/Down** goes back.
- Turn the mode dial to that MY slot: the camera now shoots with that look. The slot keeps its other settings (exposure, focus …) and takes the recipe's name, image settings and white balance.
- **Stock**, always the last entry, undoes a recipe. Previewing it shows the camera's own rendering; saved into a slot, it puts everything a recipe can change back to the camera's factory values (image settings and their sliders, white balance and its compensation, exposure compensation, dynamic range compensation, noise reduction and its custom thresholds, ISO and the ISO AUTO-HI settings), clears the slot's name and removes the film look. The slot's other settings (exposure mode, focus …) stay.

Each frame of the strip shows four colour patches (magenta, red, green, cyan-blue) through the film's look, exaggerated so you can tell films apart at a glance.

**Your recipe files** live in `tty/recipes/` on the card: one small text file per film. Delete the ones you don't want, add your own (format below), and choose DEV → *Reload recipes* or restart. The looks of the films saved in MY1–MY3 are also kept inside the camera, so they still work without the card.

### Exporting your presets
DEV → **Export settings to card** (or `export` in the developer console) writes the presets your camera holds as recipe files into `tty/export/` on the card. The row then shows how many files it wrote, or why it could not ("no card").

| File | Preset |
|---|---|
| `my1.txt` … `my3.txt` | My Settings MY1–MY3 as saved (a slot never saved is skipped) |
| `box01.txt` … `box06.txt` | the My Settings Box entries that hold a setting |
| `live.txt` | the settings in use right now |
| `set1.txt`, `set2.txt` | the sliders of Image Settings Setting1 and Setting2 |
| `fxNN.txt` | an effect whose sliders you changed (NN = its number, e.g. `fx09.txt` Positive Film) |

- Nothing in the camera changes: the slot files are only read. Every export rewrites these files.
- `tty/export/` does not appear in Recipes. Move the files you like into `tty/recipes/` (rename them as you like, 8.3 names), choose DEV → *Reload recipes*, and save them into a slot as usual: every value a file lists is set exactly as the preset had it.
- `name=` is the preset's name in the camera, or MY1, Box 1, Live … when it has none (a name longer than 23 characters or with `#` is changed, a comment says so).
- A MY slot that holds a TTY recipe also gets that recipe's look keys (`live.txt` those of the slot the dial is on), so the file carries the whole look.
- What a recipe cannot hold is a `#` comment: shooting mode, focus, snap distance, metering, drive, aspect ratio, crop, image size and quality, ND filter, slow-shutter NR, a measured manual white balance, the Image Settings kept underneath an effect, and any stored value the camera menu does not offer.
- While a TTY Menu setting holds a value (Clarity strength, High-ISO NR, Auto-Hi slowest shutter), the files carry the camera's own value, not TTY's.

### TTY Menu
Press the Effect button you assigned to **TTY Menu**.
- On the tab row, **Left/Right** switch tabs; **Down** or **OK** enter the list.
- **Up/Down** move, **Left/Right** change the value (it applies at once), **OK** flips ON/OFF-style settings. **Up** from the first row returns to the tabs. The shutter button closes the menu.
- Every setting can go back to **STOCK**, which restores the camera's own value.
- Settings are saved inside the camera and, as a backup, in `tty/config.txt` on the card. If the camera loses them (it clears that internal area after a battery pull or a crash, and *Reset Settings* clears it too), TTY takes them back from the card.

| Tab | Setting | Values | What it does |
|---|---|---|---|
| EXPOSURE | Auto ISO max | **STOCK** (800; 3200 with DR) / ISO 400 … 25600 | Highest ISO that ISO AUTO uses (the DR mode's own cap still applies) |
| EXPOSURE | Auto ISO slowest shutter | **STOCK** (1/40) / 1/60 + crop / 1/15 … 1/250 | Shutter speed at which ISO AUTO starts raising the ISO in P, Av and Auto. "1/60 + crop" means 1/60 at 28 mm, 1/80 at the 35 mm crop, 1/100 at the 47 mm crop. Not in Movie |
| EXPOSURE | Auto ISO at max ISO (P, Av) | **SLOWER SHUTTER** / KEEP SHUTTER | What happens once ISO AUTO is at its maximum: stock keeps slowing the shutter; KEEP SHUTTER holds the slowest shutter above and lets the picture get darker |
| EXPOSURE | Auto ISO in M | **OFF** / ON | In M with ISO AUTO or AUTO-HI, the camera picks the ISO from the light instead of using ISO 100 |
| EXPOSURE | Auto-Hi slowest shutter | **STOCK** / Auto, 1/2 … 1/250 | ISO Auto-High slowest shutter |
| EXPOSURE | TAv: cap ISO at Auto-Hi max | OFF / **ON** | TAv respects the ISO Auto-High maximum (stock silently allows up to ISO 25600) |
| EXPOSURE | TAv: Auto-Hi minimum too | **OFF** / ON | In TAv, ISO AUTO also respects the Auto-High minimum |
| FOCUS | Snap distances | **STOCK** / 0.7m + 1.2m | The Snap choices "1m" and "1.5m" focus at 0.7 m and 1.2 m instead (labels, display and Full Press Snap included) |
| VIEW | Frame lines | **OFF** / XPan 65:24 / 6x7 / 4:5 / 1:1 / 16:9 | Darkens live view outside the frame and outlines it; never recorded in photos |
| VIEW | Zebras | **OFF** / Y 235+ / Y 245+ / Y 252+ | Stripes over areas at or above that brightness |
| IMAGE | Clarity strength | **STOCK** / 1–9 | Strength of the Clarity effect (5 = none, stock 7) |
| IMAGE | High-ISO NR | **STOCK** / CUSTOM | Custom high-ISO noise reduction with the three thresholds below |
| IMAGE | NR weak / medium / strong from | ISO 100 … 25600 (medium/strong also OFF) | ISO where each NR level starts |
| INFO | Images stored, shutter count, battery, temperatures, card, firmware, uptime, power-ons | read-only | The camera's own counters and sensors |
| DEV | Telnet console | **OFF** / ON | A developer console over the camera's Wi-Fi (below). **No password**: anyone on the camera's Wi-Fi gets a shell. Off at every start |
| DEV | Screenshot to card | OK | Saves live view and the screen as `tty/shotNNNN.bmp` |
| DEV | Reload recipes, Save IQ RAM | OK | Re-read `tty/recipes`; dump the image-processing tables to `tty/iq_ram.bin` |
| DEV | Export settings to card | OK | Your presets as recipe files in `tty/export/` (see *Exporting your presets*) |
| DEV | Overlay state to log, Overlay test pattern | OK | Diagnostics for the drawing layer |
| DEV | Boot stock once | OK | The next power-on starts stock; the one after runs TTY again |
| DEV | Wi-Fi network / password, Free memory pool | read-only | While Wi-Fi is on: its name and password |

## Recovery
TTY never changes the parts of the camera that do the recovery (its boot loader and Ricoh's rescue mode), so all of these always work:

| Situation | What to do |
|---|---|
| Start the camera **without TTY**, once | Hold the **shutter half-pressed** while you switch on, until the screen comes on. Or choose DEV → *Boot stock once* beforehand. |
| The camera **hangs** | Take the battery out, put it back, and start with the shutter half-pressed. |
| TTY **crashes or hangs at start** | Nothing to do: after **two failed starts in a row** (a start counts as good once TTY has run 20 s, or the camera was switched off normally) the camera starts stock by itself and stays stock. |
| TTY **stays off** after that | Switch on **with the PLAY button** once (the camera starts in playback, stock). The next normal start runs TTY again. |
| **Uninstall** | Put Ricoh's official [`rg2_v003.frm`](https://www.ricoh-imaging.co.jp/english/support/digital/firmware/rg2_v003.zip) alone on a camera-formatted card and run *Confirm Firmware Version* → **Yes**, exactly as in *Install*. The camera is then 100% stock; your photos and camera settings stay. |
| The camera **no longer starts at all** | Rescue card: a camera-formatted card with Ricoh's official `rg2_v003.frm` and the zip's `rescue/backup.ttl` copied to the card root as `backup.ttl`. Turn the mode dial to **MOVIE**, press the **shutter fully** and hold it, press **POWER**; release, turn the dial off MOVIE, and wait: the camera reinstalls the stock firmware by itself and restarts (this takes a while; don't touch it). Remove the card afterwards: never leave a card with `backup.ttl` in the camera. |

Good to know:
- Saved TTY choices are harmless on a stock camera: the Effect button choices simply act as "Off" (press) and "Wi-Fi" (hold).
- If TTY logs a problem, it writes `tty/tty_log.txt` on the card. A normal session writes nothing.

## Updating
Install a newer `rg2_v003.frm` the same way as above; your settings stay. Copy new recipe files into `tty/recipes/` as you like; recipe updates never need a reflash.

**From v1.0.x to v1.1.0:** the card folder is now `tty/` instead of `cfw/`, and TTY no longer reads `cfw/` at all (no fallback). After flashing the new `rg2_v003.frm` as usual:
1. Move your own recipes from `cfw/recipes/` to `tty/recipes/` (the zip's `tty/` folder brings the shipped ones).
2. If you want to keep your exports, move `cfw/export/` to `tty/export/`.
3. Optionally move `cfw/config.txt` to `tty/config.txt`. The settings themselves are kept in the camera's internal memory, as are the looks saved in MY1–MY3.
4. Delete the `cfw/` folder.

Your Effect button choice is kept too: a camera set to the old *CFW Menu* now shows **TTY Menu**. The log is now `tty/tty_log.txt`.

## Recipe file format
One ASCII text file per film in `tty/recipes/`: an 8.3 file name (`myfilm.txt`), Windows line endings (CRLF), `#` comments, at most 4 KB, up to 48 files. Recipes appear sorted by name, Stock always last. A file that doesn't parse is skipped and named in `tty/tty_log.txt`.

| Key | Values | Sets (in the MY slot) |
|---|---|---|
| `name` (required) | up to 23 characters | filmstrip frame and slot name |
| `base` (required, before every key except `name`) | `standard`, `vivid`, `setting1`, `setting2` (Image Settings) or an effect: `bright`, `bw`, `bw_te`, `hicontrast_bw`, `cross_process`, `positive_film`, `bleach_bypass`, `retro`, `miniature`, `high_key`, `slight`, `shift_crop`, `clarity`, `brilliance`, `vibrant`, `hdr_tone`, `portrait` | Image Settings / Effect |
| `saturation`, `contrast`, `sharpness` | −4 … +4 (where the stock menu offers it for that base); `hicontrast_bw` contrast: `max`, `-1`, `-2` | that mode's slider |
| `vignetting` | `off`, `weak`, `medium`, `strong` | that mode's vignetting |
| `hue` | Cross Process: `basic`, `magenta`, `yellow`; Bleach Bypass: `normal`, `warm`, `cool` | that mode's hue |
| `toning` | B&W (TE): `sepia`, `red`, `green`, `blue`, `purple`; Miniature/Clarity/HDR Tone: `bw`, `sepia`, `color` | that mode's toning |
| `clarity` / `glossiness` | 1 … 9 (`clarity` / `brilliance` base only; 5 = none) | Clarity / Glossiness |
| `wb` | `auto`, `multi_auto`, `daylight`, `shade`, `cloudy`, `incandescent1`, `incandescent2`, `fl_daylight`, `fl_neutral`, `fl_cool`, `fl_warm`, `cte`, `kelvin` | White balance |
| `wb_kelvin` | 2500 … 10000 (required with `wb=kelvin`) | Colour temperature |
| `wb_ab`, `wb_gm` | −8 … +8 (amber +/blue −, green +/magenta −; needs `wb`) | WB compensation |
| `exposure` | e.g. `+0.7`, `-1.0` (EV, rounded to 1/3) | Exposure compensation |
| `dr` | `off`, `weak`, `medium`, `strong`, `auto` | Dynamic range compensation |
| `nr` | `off`, `weak`, `medium`, `strong`, `auto`, `custom` | High-ISO NR |
| `nr_weak`, `nr_medium`, `nr_strong` | ISO 100 … 25600 in 1/3 stops (`nr_medium`, `nr_strong` also `off`); all three with `nr=custom` only, weak ≤ medium ≤ strong | ISO where each custom NR level starts |
| `iso` | `auto`, `auto_hi` or ISO 100 … 25600 in 1/3 stops (e.g. `400`, `1250`) | ISO |
| `auto_hi_max`, `auto_hi_min` | ISO 200 … 25600 / ISO 100 … 12800 (min below max) | ISO AUTO-HI maximum / minimum |
| `auto_hi_shutter` | `auto`, `1/2`, `1/2.5`, `1/3`, `1/4`, `1/5`, `1/6`, `1/8`, `1/10`, `1/13`, `1/15`, `1/20`, `1/25`, `1/30`, `1/40`, `1/50`, `1/60`, `1/80`, `1/100`, `1/125`, `1/160`, `1/200`, `1/250` | ISO AUTO-HI slowest shutter |

**Look keys** (`standard`, `vivid`, `setting1`, `setting2` bases only) are applied by TTY in the camera's image-processing tables, relative to the Standard rendering, so the contrast and saturation sliders still work on top:

| Key | Values | Stage |
|---|---|---|
| `matrix` | 9 integers −511 … 511, ×128, rows R, G, B | colour matrix in linear RGB (128,0,0,0,128,0,0,0,128 = Standard) |
| `tone`, `tone_r`, `tone_g`, `tone_b` | 33 integers 0 … 255: output at Standard tone codes 0, 8, …, 256 | tone curve per channel (`tone` = all three; 0,8,…,248,255 = Standard) |
| `post_matrix` | 9 integers −511 … 511, ×128 | 3×3 on display RGB after the curves |
| `hue1` … `hue12` | 9 integers −511 … 511, ×128 each | per hue-sector 3×3 in linear RGB; sectors by Cb/Cr angle: 1–3 magenta, 4–6 red/orange/yellow, 7–9 yellow-green/green, 10–12 cyan/blue |

The shipped films are fitted to [spektrafilm](https://github.com/andreavolpato/spektrafilm) film simulations by Andrea Volpato: spektrafilm's own runtime renders each film and print pair, and the look keys above are fitted to its output (CC BY-SA 4.0; see `tty/recipes/licenses/`). Cinema Green, Cinema Yellow and Cinema Warm are Vision3 negatives printed on Vision 2383 / 2393 and print-timed in spektrafilm itself (enlarger filters, print curves, interlayer effects; listed in each file's header); Vivid Slide is a reversal film, scanned. Rangefinder, Mono Hard, Mono Soft Sepia and Mono Pushed Street are the project's own designs (parametric targets listed in each file's header, CC BY 4.0). The black-and-white looks have no grain; the stock Hi-Contrast B&W effect adds its own.

## Developer console
DEV → *Telnet console* starts a telnet server on port 23. Turn Wi-Fi on in the camera menu, join the camera's network, and `telnet 192.168.0.1`. The prompt is `tty> `. Stock shell commands work, and their output is captured together with the TTY log; in the stock shell, `tty status` prints TTY's state.

| Command | Output |
|---|---|
| `help` | command list |
| `status` | version and build, uptime, capture state, every TTY setting |
| `log [bytes]` | the last captured output (default 2048 bytes) |
| `tail [seconds]` | new output as it arrives (default 10 s, max 300) |
| `sh <command>` | a stock shell command, its output and `result: N <text>` |
| `tasks` | RTOS tasks: id, name, state, priority, stack |
| `heap` | GlobalHeap size/free/largest block, memory pools |
| `mem <addr> [len]` | hex + ASCII dump (hex arguments, ≤ 0x400 bytes, DRAM only) |
| `wifi` | Wi-Fi state as the system controller sees it |
| `export` | DEV → *Export settings to card*, run by TTY; prints its result |
| `exit` | closes the session |
| anything else | runs as a stock shell command (e.g. `ver`) |

## Building and development
Requirements: `arm-none-eabi-gcc`, Python ≥ 3.12, `pip install -r requirements-dev.txt`. `make fw` downloads Ricoh's stock firmware (SHA-256 checked) into `fw/`; it is never committed.

```sh
make test       # lint + ISA audit + emulator tests + firmware image checks (what CI runs)
make dist       # dist/gritty-v<VERSION>.zip (release) and -debug.zip (development)
make help       # every target
```

- **The release firmware** (`make rom`) is Ricoh's 3.00 with a 132-byte loader patch and the TTY files added to the camera's internal read-only drive. Design and checks: [`loader/flash/README.md`](loader/flash/README.md).
- **For development**, the debug zip carries a loader firmware that starts TTY from the card instead, with full logging: new builds then need no reflash, only new files on the card. The debug card is the single exception to the `tty/` folder: the flashed debug loader opens `cfw/gr2_cfw.bin` (`H:/cfw/gr2_cfw.bin`), so that one file stays in `cfw/`; everything else (`tty/tty_main.bin`, `tty/fonts/`, recipes, log) is in `tty/`.
- **Tests:** a Unicorn emulator runs TTY against the stock firmware (`tests/emu/`); the firmware images are checked byte by byte and by running Ricoh's own updater code in emulation.
- **Contributing:** see [`AGENTS.md`](AGENTS.md) for the architecture, the code rules (ARMv5TE Thumb-1, big-endian, no libgcc) and the source map.

| Path | Contents |
|---|---|
| `src/`, `include/` | TTY sources; `include/gr2_hw.h` holds every firmware address used |
| `gr2_boot.ld`, `gr2_main.ld` | stage-1 window and stage-2 link scripts |
| `loader/` | firmware stubs (release and debug), rescue script, probes, development loader |
| `recipes/`, `fonts/` | shipped recipes and overlay fonts, with their licences |
| `scripts/` | packaging, firmware builders and their independent checks, font builder |
| `tests/emu/` | emulator tests |
| `tests/site/` | checks of the site's LUT converter and the recipe file rules (`node tests/site/validate.mjs`) |
| `site/` | the website (GitHub Pages, static): home page and LUT-to-recipe converter |
| `tools/gr2frm.py` | `.frm` container tool |

## Licence
MIT for the TTY code ([LICENSE](LICENSE)). Fonts: SIL OFL 1.1. Film recipes: CC BY-SA 4.0 (derived from spektrafilm); the project's own looks: CC BY 4.0 (each file's header says which). The zips' `rg2_v003.frm` contains Ricoh's firmware 3.00, which is Ricoh's property and not covered by the MIT licence. Not affiliated with Ricoh, Kodak or Fujifilm; film names are used descriptively.
