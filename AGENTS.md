# Ricoh GR II Custom Firmware (gr2_cfw) — Agent Operating Manual

**Repository:** `ricoh-grii-cfw`  
**Purpose:** Magic-Lantern-style CFW for Ricoh GR II. Debug build: loaded from the SD card (`Startup.ttl`, later the M10 debug loader). Release: fully flashed firmware (M11, planned). Design and flash policy: the project plan
**Architecture:** Bare-metal ARMv5TE Big-Endian (ARM946E-S-class, MPU, no MMU), Thumb-1 only  
**Payload window:** `0xA0017000–0xA001EFFF` (32 KiB, IPL copy's erased tail; confirm with the M0 probe) holds stage 1 only; stage 2 lives in a heap block.

> **2026-10-30:** M0–M5 passed on hardware. Two Effect-button menus (CFW Menu, Recipes); release/debug builds packaged as SD zips by CI.

---

## 1. Build System & Toolchain

- **Compiler:** `arm-none-eabi-gcc` (tested with GCC 16.2.0 on darwin arm64; CI uses Ubuntu's `gcc-arm-none-eabi`).
- **Build both variants** (`release`: `CFW_DEBUG=0`, only `ERROR` log lines; `debug`: `CFW_DEBUG=1`, every log line, key trace, `cfw status` dump):
  ```sh
  make -C ricoh-grii-cfw          # bin/release/gr2_cfw.bin, bin/debug/gr2_cfw.bin
  make -C ricoh-grii-cfw disasm   # bin/<variant>/gr2_cfw.asm
  make -C ricoh-grii-cfw dist     # dist/gr2_cfw-v<VERSION>-{release,debug}.zip (SD-card trees)
  ```
- `VERSION` is compiled in (`CFW_VERSION`, shown in the CFW Menu footer and the debug banner); objects rebuild when it changes.
- **Logging:** `ERR*` always writes (literal messages, prefixed `ERROR`); `DBG*` compiles to nothing in release and does not evaluate its arguments. Log failures and refusals with `ERR`, everything else with `DBG`.
- **CI** (`.github/workflows/build.yml`): builds both variants, audits ISA, runs `scripts/package_sd.py`, uploads the zips. `release.yml` bumps `VERSION`, publishes the zips as a GitHub release.
- **Emulator smoke test:** run `make emu` before every hardware zip (`tests/emu/`: both variants in Unicorn against `fw/b01firm8.bin`; skips when the image is absent; `EMU_ARGS="--render DIR"` writes screen PNGs).

---

## 2. Memory Architecture & Calling Conventions

- **Memory Layout:**
  - `0xA0017000`: stage-1 link base, `_start` (`src/entry.S`). `.text`+`.data` (loaded by `fload`) are followed by `.bss` (NOLOAD), which must end below the press veneer `0xA001E07C`; the hold veneer sits at `0xA001EEA0` (linker asserts). The bin runs to the end of the hold veneer (≈ 32 KiB); the window canary checks that the rest of the window is still 0xFF. The window is the erased 0xFF tail of the IPL copy (`0xA0016324–0xA001FBFF` was untouched in a live APP dump); `loader/probe` + `scripts/check_probe.py` confirm it per camera.
  - Stage 2 and large buffers: `Memory_Alloc(size)` at `0xA0024D6F` (GlobalHeap, 2 MB shared with stock code; keep single blocks well below ~200 KB). The boot pool (bump pointer `0xA00C2BA4`) is used up on hardware: do not carve from it.
  - `0xA0700000` (RTOS zero-init data) and `0xA0900000+` (RTOS pool) are never used for code.
- **Calling the RTOS from CFW** (addresses in `include/gr2_hw.h`):
  - `int Shell_Execute(const char *)` at `0xA002A275` (runs synchronously in the caller's task; re-entrancy from inside `ex` unverified).
  - `uint32_t Prop_Get(uint32_t id)` at `0xA00E0E2F`.
  - File API `0xA002C70F/CA99/CC4B/C905` (open returns FILE* or 0; append flags `0x09`).
  - **Tasks run in USR mode** (CPSR `0x60000010`, verified on hardware): no CP15 access (no cache maintenance) and no IRQ masking. MPU region 5 still allows USR writes to RTOS code and data.
- **Camera Execution Protocol:**
  - `fload H:/cfw/gr2_cfw.bin A0017000 8000` (`fload <file> <addr_hex> <maxsize_hex>`; fread stops at EOF), then `ex 0xA0017001` (`0x` prefix required; bit 0 = Thumb; called via `blx`). That is stage 1; it loads stage 2 (`H:/cfw/gr2_main.bin`) into a `Memory_Alloc` block, adds the load address to the words in its relocation table, zeroes its `.bss` and calls `cfw_main(const cfw_boot_t *)`.
  - Stage 2 must stay position-independent apart from absolute 32-bit words: `scripts/mkmain.py` links it at 0 and 0x10000000 and fails the build on any other difference. No code may assume a fixed stage-2 address; fixed addresses (veneers, window canary) belong to stage 1.
  - DRAM is write-through: a first load needs no cache maintenance (the heap block never held code). Load once per power cycle (re-loading over executed code could run stale I-cache lines).
  - **Code patches** must be one aligned 16-bit store each, so a stale I-cache line can only hold the complete old instruction. Thumb BL retargets change only the first halfword: the target veneer shares the low 12 address bits of the stock target (`src/veneers.S`, fixed addresses in `gr2_boot.ld`); the veneer jumps through a target word that stage 2 fills before patching.

---

## 3. Source Organization

- `include/gr2_hw.h`: verified RTOS addresses, property IDs, menu record layout.
- `include/gr2_fmt.h`: division-free formatting (`put_str`, `put_hex32`).
- `include/cfw_log.h`, `src/log.c`: append-only log to `H:/cfw/cfw_log.txt`.
- `include/cfw.h`, `src/task.c`: resident "CFW" task (`Task_Create` `0xA0026709`).
- `src/shell.c`: `cfw` shell command, registered by appending a record to the shell registry (`0xA05FC308`: +4 list, +8 count).
- `src/entry.S`, `src/boot.c`, `include/boot.h`, `gr2_boot.ld`: stage 1 (window canary, stage-2 loader/relocator). Two entries: `_start` (`ex` from Startup.ttl; refuses while the flashed loader's `ldr_state` 0xA00AFD70 is 5/6) and the loader ABI v1 header (`b` at +0, "GRL1" at +4, `cfw_ldr_hook` at +8): hook(1) preloads stage 2 with the card log muted, hook(13) starts the `CFWldr` task (waits for JobSystem step 13, then `cfw_main`) and disarms (M10b; loader/flash/README.md §3). `gr2_main.ld`, `scripts/mkmain.py`: stage-2 link and `gr2_main.bin` (header + image + relocation offsets).
- `src/main.c`: stage-2 entry (`cfw_main`): shell registration, menu/hook/input install, task start.
- `src/menu.c`: "CFW Menu" (`0x1A6 = 0x20`, `0x1A7 = 2`) and "Recipes" (`0x21`, `3`) in the Effect Button / Effect Button Hold menu records. Labels are string IDs `0x354`/`0x35E`: the 21 language pointer tables plus the per-ID attribute and style tables are copied to the heap with IDs up to `0x35F`, then the six string-accessor bounds are raised from `0x350` to `0x360` (finding 16 §6). Raising only the pointer-table bounds gave empty labels and double-height rows on hardware.
- `src/hooks.c` + `src/veneers.S`: Effect press (`0xA03A1F92`) / hold (`0xA03A5790`) BL retargets; per-menu request counters. Original bytes are verified first; each patch is one halfword store (veneers at `0xA001E07C`/`0xA001EEA0`; until 2026-10-31 `0xA001B07C`/`0xA001BEA0`). `src/arm.S`: CPSR read for logging.
- `src/recipes.c`, `include/recipes.h`: film recipes (findings 19, 20): scan `H:/cfw/recipes`, parse/validate (look keys into `look_t`), write a recipe into a MY slot file, remember the recipe file per slot (config `rcK.0..2`), decide which look is shown: the previewed recipe while the Recipes overlay is open (in the current mode if Standard/Vivid/Setting1/Setting2), otherwise the active slot's recipe while its base mode is active, otherwise stock.
- `src/look.c`, `include/look.h`: looks in IQ RAM (finding 20 §B). First use copies the matrix/hue/tone/YCbCr sections into heap buffers (~170 KB) and publishes them by pointer; a look writes CFW records (matrix 11, hue 6, tone 30–32, YCbCr 34–51) and repoints only one IQ mode's list entries; stock entries for modes 0–3 are verified at install. Every write holds the ISP mutex and flips the ISP-Pre change cache (`0xA06C8B9A`, `0xA0155D20`) so live view reprograms on the next frame; any new IQ-RAM stage must do the same.
- `src/info.c`, `include/info.h`: INFO/DEV tabs (finding 21). Read-only getters; battery ADC shown as a running average. The test-pattern row shows OFF / RUNNING (pending until drawn after the menu closed) / ON; the console row shows OFF or ON with its reachability (`telnet 192.168.0.1` / `Wi-Fi off (camera menu)`). The console row toggles `console_set` and does **not** touch Wi-Fi: `Prop_Set(0x1E8)` + `Msg_Post(0x116)` reached JOBID_WIFI, yet no access point appeared on hardware, so Wi-Fi comes from the camera menu (`info_wifi_state` feeds the console `wifi` command). The stock telnetd is no longer started. Screenshot copied in 48-row bands (one 61 KB heap block) from the shown LV frame through the uncached alias, OSD read in place (`osd_shown_layer`), hardware-verified.
- `src/console.c`, `include/console.h`: developer console (finding 21 §3.2/§5), hardware-verified 2026-11-01 (then on port 2323). Since then it serves port 23 instead of the stock telnetd and has on/off.
  - Server task `CFWcon` (prio 0x96, stack 0x3000), created once, using the telnetd socket calls (`GR2_FN_SOCKET`…, `gr2_hw.h`).
  - `console_set(0)` closes the listening and session sockets from the CFW task, so the blocked accept/recv fail; whether that wakes them on hardware is untested. It also removes the printf hook. The task then idles.
  - Printf capture goes through the console hook slot `0xA05FCCD0+0x44` (installed only if empty; the hook only copies bytes and runs under the console mutex in any task), plus `cfw_log_tap` (log.c), into a 16 KiB heap ring.
  - `console_exec(line, out, max)` is the testable dispatcher. Unknown words run as stock shell commands; only that path and `sh` have side effects (`Shell_Execute`, like stock telnetd). `mem` is bounded to DRAM.
  - Kernel `ref_tsk`/`ref_mpl` are ARM-state calls through pointers without bit 0.
- `src/overlay.c`, `include/overlay.h`: VIEW tab overlays (finding 23, Tier A, no code patch; frame lines and zebras hardware-verified 2026-11-01). Draws RGBA4444 into the full-plane buffer stock shows (`BITMAPS[G[1]]`, `G` = `0xA04070AC`, pitch 640) only while `G[3]` = LCD, plane-mask bit 1, system mode 6/9 and layer 2 is on (`0xA00D7149`); restamps 40 ms after every flip (stock copies its canvas over the buffer, which can take a whole session). `wipe()` clears the front buffer and sets `REC_RGB_REDRAW` (`0xA0424360`) so stock rebuilds its grid/masks: on a setting change and when the DEV test pattern is toggled off. Zebra cells (4×4) poll the LV luma (`*0xA0155BE8`) every 100 ms; hot cells are redrawn every pass (animated), cold cells restored from the stock canvas (`*0xA043EA44`, pitch 720) and `frame_px`. DEV "Overlay state to log" / "Overlay test pattern" run 1.5 s after the menu closed (`info_poll`). Focus peaking was built and dropped (2026-11-01).
- `src/mem.c`: `memset`/`memcpy` for compiler-generated calls (built with `-fno-tree-loop-distribute-patterns`).
- `src/ui.c`: both overlays, fullscreen (`design/ui-proposals/`: `cfw-menu.jpg`, `recipes-B-filmstrip.jpg` without the name line): tabbed CFW Menu over the settings engine plus the INFO/DEV pages; Recipes filmstrip `STRIP_GAP` (24 px) above the bottom of the live-view image (`GR2_LV_ASPECT`: 3:2 image ends at y 452, 16:9 at 420, else the screen bottom), translucent film base (user palette entry 20, alpha 11) with solid frames/sprocket holes, 4-band swatches per recipe (entries 0–19, slot = recipe index mod 5 so a scroll reprograms ≤ 3 recipes), names wrapped to two lines when too wide; a shadow band above the strip only while choosing a slot or showing the apply result. Palette rule: `recipes_palette()` programs and commits all overlay entries **before** drawing (pixels drawn first show the stock colour until the commit: the green flash on opening, 2026-11-01); `osd_close` does the reverse. Runs only in the CFW task; the hooks only count events and queue keys.
- `src/settings.c`, `include/settings.h`: settings (findings 18, 21). Property settings (Clarity `0xCE`, Auto-High Tv `0x113`, NR `0xEF`/`0xF0–0xF2`) record the camera value before writing and restore it on STOCK; re-applied ~1 s after a shooting-mode / MY-slot change. RAM patches (TAv cap at AE_F+0x132 `0xA070FD7A`; Auto ISO slowest shutter / max at AE_F+0xBE/+0xC4/+0xC2/+0x2C8 and "keep shutter" at +0xCA and +0xCC, stock in Movie; Snap 0.7/1.2 tables/literals/code halfwords (one switch); SV getter-hook halfwords for "TAv: Auto-Hi minimum too" `0xA00DC236` and "Auto ISO in M" `0xA00DC22A`, `hpatch_t` tables with guard halfwords) verify every original first and undo in reverse. Never run the stock `aeFSave` while AE_F overrides are active.
- `src/config.c`, `include/config.h`: `H:/cfw/config.txt` (`key=XXXXXXXX` hex lines, CRLF), loaded at task start, written only when changed (UI close, post-mode-change re-apply).
- `src/osd.c`: draw lock `0xA077D58C`, private OSD back buffer + layer 3 switch (`0xA00D717F`) (finding 17). Text from GFNT fonts in `H:/cfw/fonts` (heap, loaded at task start; ROM-font fallback); 4-bit coverage blended in YCbCr against the pixel underneath and mapped to the nearest opaque palette entry (palette read with `0xA0393991` at open, ramps cached per colour pair); on translucent backgrounds a threshold is used instead (no dark halo). Palette entries 224–251 are overlay-owned while open (`osd_pal_user(k, r, g, b, alpha)`, set with `0xA03939EF`; that setter only writes the hidden hardware bank, so `osd_pal_commit` flips the bank and writes the other one too; never chosen by the nearest-colour search). `osd_close` first turns pixels using those entries transparent (the restore takes a bank flip while the CFW buffer is still shown: the film base flashed in the stock green, 2026-11-01), then restores the entries, then returns the stock buffer. `osd_capture` copies the shown OSD + palette for screenshots.
- `src/input.c`: APP system-message handler pointer `0xA04071F0` swapped for a pass-through (data store, verify-first). It consumes key messages only while captured, and only releases of presses it took.
- `loader/Startup.ttl` + `loader/Shutdown.ttl`: payload loader and power-off `cfw status`; `loader/probe/`: data-only M0 probe.
- `scripts/package_sd.py`: SD zips (`Startup.ttl`, `Shutdown.ttl`, `cfw/gr2_cfw.bin`, `cfw/gr2_main.bin`, `cfw/fonts/`, `cfw/recipes/`), `SHA256SUMS.txt`, `RELEASE_NOTES.md`; checks CRLF/ASCII, 8.3 recipe names, GFNT fonts, window size, entry point, stage-2 header, version.
- `recipes/*.txt`: film files generated by the maintainer's recipe tool (do not hand-edit; regenerate), plus hand-written test recipes (`lvtest`, `t_*`).
- `scripts/make_fonts.py` + `fonts/`: Inter / Barlow Condensed (OFL) rasterised to GFNT at 13/15/18/18/22 px; regenerate with the TTF directories when sizes or glyphs change (`include/osd.h` FONT_*, `src/osd.c` FONT_FILE, packager FONT_FILES must match).
- `scripts/check_probe.py`: offline probe check. `scripts/bump_version.py`: SemVer bump of `VERSION`.
- `scripts/build_release.py`: boot-logo `.frm` builder (flashing experiment, outside the CFW; not run by make or CI).
- `loader/flash/` (M10, prepared, never flashed): `ldr_stub.S`/`.ld` = 132-byte APP stub (section-load hook `0xA0022E5C`, free range `0xA00AFCF0`, status word `0xA00AFD70`), `probe/` = data-only boot-timing/loader-state probe, `rescue/backup.ttl`, README = design, loader ABI v1, user test plan, risks. `scripts/build_loader_frm.py` builds `dist/rg2_v003_loader.frm` (APP only, stock bytes asserted, gr2frm verify); `scripts/emu_loader.py` = byte check + Unicorn run of the hook path; `scripts/loader_timing.py` decodes the probe. None of them is run by make, `make dist` or CI.
- `tools/`: synced copies of the reverse-engineering notes (canonical) used by `scripts/build_release.py` and the loader scripts. Edit the canonical copy and resync.

---

## 4. Operational Invariants & Pre-Flight Rules (HARD INVARIANTS)

Before building, packaging, or deploying ANY custom firmware:
1. **MANDATORY PRE-FLIGHT VERIFICATION**:
   - Future agents **MUST** execute the pre-flight safety audit before distributing or releasing any `.frm`:
     ```sh
     python3 the maintainer's safety check
     ```
   - If safety gates fail, **HALT IMMEDIATELY**.
2. **NO LIBC / FREESTANDING ONLY**:
   - Built with `-nostdlib -fno-builtin -ffreestanding`; no libgcc (Homebrew has no big-endian libgcc) — avoid `/` and `%` on runtime values.
   - Use `<stdint.h>`, `<stddef.h>`, and `<stdbool.h>` only.
3. **ARMv5TE THUMB-1 ONLY**:
   - `-march=armv5te -mthumb -mthumb-interwork -mbig-endian`. No `movw/movt`, `it`, `cbz`, `.w` encodings, `dsb/isb/dmb`.
4. **RAM PATCHES VERIFY FIRST**:
   - Every RTOS RAM patch (menu records, string table, code) checks the expected original bytes and is skipped + logged on mismatch. Code patches are single atomic halfword stores (see §2).
   - `0xA03E9984` is a guard-predicate slot, not an Effect-hold action; `0xA043DC32` is playback data, not a menu choice list.
5. **PROPERTY VALUES**:
   - Only write non-stock property values whose stock handling is documented (finding 16: `0x1A6 = 0x20/0x21` act as "Off", `0x1A7 = 2/3` act as "Wi-Fi"). Persisted values survive without the CFW.
6. **RECORD-AND-RESTORE SETTINGS**:
   - `efcset`/`svautoset`/`nrset`/`pset` may persist; save the stock value before the first change and restore it on disable.
7. **MEMORY**:
   - Only stage 1 lives in the 32 KiB window; stage 2 and buffers come from `Memory_Alloc` (or a limit-checked pool carve for permanent large buffers).
8. **PARACHUTE VERIFICATION**:
   - Rescue card (manual checklist; no script inspects the card): exactly one `rg2_v*.frm` in the root and it is the stock `rg2_v003.frm`; **no** `H:/backup.ttl` (it disables BAK's automatic `up`); `SD.dat` is not a recovery image; no `._*` files.
   - Any `.frm` must have its `nUpdateCsum` recomputed (`gr2frm.py repack`). Flash it only from a separate card (renamed `rg2_v003.frm`) and restore the stock file afterwards. Never include IPL or BAK modules.
   - Residual risk: BAK auto-reflashes only on APP header/load failure; a CFW that crashes → BAK → power off.
