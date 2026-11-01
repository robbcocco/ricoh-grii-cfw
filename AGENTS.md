# gr2_cfw — agent manual

Custom firmware for the Ricoh GR II (fw 3.00): SD-loaded today, flashed later (the project plan). User manual: `README.md`. Reverse-engineering evidence: the reverse-engineering notes ("finding N"). Maintainer rules: `the maintainer's notes`.

## 1. Workflow
- **Build and test** (from this directory):
  ```sh
  make            # both variants: bin/{release,debug}/gr2_{cfw,main}.bin
  make test       # ruff + ISA audit + emulator (both variants) + loader .frm check; CI runs this with WERROR=1
  make dist       # SD zips for the user (dist/)
  make emu EMU_ARGS="--render /tmp/emu_out"   # screen PNGs: look at them after UI changes
  ```
  `make fw` (pulled in by `emu`/`loader`) extracts the stock firmware into `fw/`. Host tools: `pip install -r requirements-dev.txt`.
- **Hardware loop:** the user tests the debug zip and reports results plus `H:/cfw/cfw_log.txt`. Before every zip: `make test` passes, then `make dist`. Report what changed and exactly what to check on the camera.
- **Never** touch the SD card (`/Volumes/*`), flash anything, or run code on the camera yourself. Flash steps are user actions gated by `loader/flash/README.md` §6 and `make check`.
- **Tests** (`tests/emu/`): add a check when a bug could plausibly come back (e.g. palette flash, patch restore). Prove a new check fails without the fix. No checks of wording or incidental layout.
- **Docs follow the code** in the same change: README (user-visible behaviour), the project plan (status, roadmap), this file (architecture facts, rules), `tests/emu/README.md`, `loader/flash/README.md`. Keep them current, not historical; history is in git.
- **Commits:** conventional style (`feat:`, `fix:`, `docs:`, `ci:`, `refactor:`), one topic each.

## 2. Toolchain and code rules
- `arm-none-eabi-gcc`, `-march=armv5te -mthumb -mthumb-interwork -mbig-endian -Os`, freestanding (`-nostdlib -fno-builtin -ffreestanding`), `-Wall -Wextra` clean. ARMv5TE, ARM946E-S-class, MPU, **Thumb-1 only**: no `movw/movt`, `it`, `cbz`, `.w`, `dsb/isb/dmb`. `make audit` checks the ELF attributes.
- **No libgcc:** no `/` or `%` on runtime values; use `udiv32`/`put_udec` (`include/gr2_fmt.h`) or loops.
- **Logging:** `ERR*` always (literal text, prefixed `ERROR`); `DBG*` compiles out of release and does not evaluate its arguments. Refusals and failures use `ERR`, everything else `DBG`. A release session writes nothing on success.
- **Addresses** live in `include/gr2_hw.h` with the finding that verified them. Thumb function pointers have bit 0 set; kernel (µITRON) calls are ARM-state, called through a pointer without bit 0 (finding 26 §2).
- **Tasks run in USR mode** (CPSR `0x60000010`): no CP15 (no cache maintenance), no IRQ masking. MPU region 5 allows USR writes to RTOS code and data. `Task_Create` never returns 0 (its assert parks the caller). CFW tasks block most of the time at `0x96`; CPU-heavy work belongs at `0xC0–0xD0` (finding 26 §3.3).
- **Code patches:** verify the original bytes first; each patch is one aligned halfword store, so a stale I-cache line can only hold the complete old instruction. BL retargets change only the first halfword and land on fixed-address veneers that share the stock target's low 12 bits.
- **Data patches** (tables, pointers, properties) verify first, record the stock value and restore it on STOCK / disable. Property writes go through the stock setter with values the stock menu can offer; read stock values with `Prop_Get_Raw` (`0xA00E0E81`, no getter hook). Property IDs ≥ `0x383` assert (finding 25).
- **Memory:** only stage 1 lives in the 32 KiB window; stage 2 and buffers come from `Memory_Alloc` (GlobalHeap, 2 MB shared with stock: single blocks well below ~200 KB). The boot pool is used up; `0xA0700000`+ is RTOS data.
- **Stage 2 is position-independent** apart from absolute 32-bit words: `scripts/mkmain.py` links it at 0 and `0x10000000` and fails on any other difference.
- **Files on the card:** TTL and recipe files ASCII with CRLF, 8.3 recipe names, uppercase drive letters (`package_sd.py` checks).

## 3. Load paths
- **SD loader:** `Startup.ttl` runs `fload H:/cfw/gr2_cfw.bin A0017000 8000` and `ex 0xA0017001` (needs `PROPID_DEVEL_MODE = 1`). Stage 1 checks the window tail is still `0xFF`, loads `H:/cfw/gr2_main.bin` into the heap, relocates it, zeroes its `.bss` and calls `cfw_main(const cfw_boot_t *)`. Load once per power cycle.
- **Flashed debug loader (M10, not flashed yet):** stage 1 carries the loader ABI v1 header (`b` at +0, "GRL1" at +4, `cfw_ldr_hook` at +8). hook(1) preloads stage 2 with the card log muted; hook(13) starts the `CFWldr` task, which waits for JobSystem step 13 (`Ctrl()+0x54+4*13`) and then calls `cfw_main`. Step 13 only queues Startup.ttl and does not wait for it, so a loader card must not carry the CFW `Startup.ttl`; `ex` is refused while `*0xA00AFD70` is 5/6. Details: `loader/flash/README.md`.

## 4. Source map
- `src/entry.S`, `src/boot.c`, `include/boot.h`, `gr2_boot.ld`: stage 1 (window canary, stage-2 loader/relocator, loader ABI hook). `.bss` must end below the press veneer `0xA001E07C`; the hold veneer is at `0xA001EEA0` (linker asserts).
- `src/main.c`, `gr2_main.ld`: stage-2 entry: shell command, menu/hook/input install, task start.
- `src/task.c`, `include/cfw.h`: the "CFW" task: 20 ms event/key poll, 200 ms settings poll, 1 s heartbeat.
- `src/shell.c`: `cfw` shell command, appended to the shell registry (`0xA05FC308`: +4 list, +8 count).
- `src/menu.c` (finding 16): "CFW Menu" (`0x1A6 = 0x20`, `0x1A7 = 2`) and "Recipes" (`0x21`, `3`) in the Effect menu records; the 21 language tables plus attribute/style tables are copied to the heap up to ID `0x35F` and the six string-accessor bounds raised to `0x360`.
- `src/hooks.c`, `src/veneers.S`: Effect press (`0xA03A1F92`) / hold (`0xA03A5790`) BL retargets; never patch the `cmp/bhs` at `0xA03A1F9A`. `0xA03E9984` is a guard-predicate slot and `0xA043DC32` playback data, not menu choices.
- `src/input.c` (finding 17): APP message handler pointer `0xA04071F0` swapped for a pass-through; consumes key messages only while captured, and only releases of presses it took; POWER is never taken, the shutter closes the overlay.
- `src/osd.c`, `include/osd.h` (finding 17): draw lock `0xA077D58C`, private back buffer on layer 3 (`0xA00D717F`); GFNT fonts from `H:/cfw/fonts` (ROM-font fallback), anti-aliased against the pixel below, threshold on translucent backgrounds. Palette entries 224–251 are overlay-owned while open (`osd_pal_user(k, r, g, b, alpha)`; the setter writes only the hidden bank, so `osd_pal_commit` flips and writes both). **Palette rule:** program and commit overlay entries before drawing pixels that use them; `osd_close` makes those pixels transparent before restoring the entries (otherwise they flash in the stock colours).
- `src/ui.c`: CFW Menu (tabs, settings rows, INFO/DEV pages) and the Recipes filmstrip: `STRIP_GAP` above the bottom of the live-view image (`GR2_LV_ASPECT`: 3:2 ends at y 452, 16:9 at 420), translucent film base (entry 20, alpha 11), swatch slot = recipe index mod 5 (entries 0–19), names wrapped to two lines, shadow band only while choosing a slot or showing the result. Runs only in the CFW task.
- `src/settings.c`, `include/settings.h` (findings 18, 21): property settings (Clarity `0xCE`, Auto-High Tv `0x113`, NR `0xEF`/`0xF0–0xF2`) with recorded stock values, re-applied ~1 s after a mode / MY-slot change; RAM patches (TAv cap at AE_F+0x132; Auto ISO slowest/max at AE_F+0xBE/+0xC4/+0xC2/+0x2C8, keep shutter at +0xCA and +0xCC, stock in Movie; snap tables/literals/code halfwords; getter halfwords `0xA00DC236` TAv minimum and `0xA00DC22A` Auto ISO in M), all verify-first with reverse undo. Never run the stock `aeFSave` while AE_F overrides are active.
- `src/config.c`: `H:/cfw/config.txt` (`key=XXXXXXXX` hex lines, CRLF), written only when changed.
- `src/recipes.c`, `include/recipes.h` (finding 19): scan and parse `H:/cfw/recipes`, write a recipe into a MY slot file, remember the recipe per slot (config `rcK.0..2`), choose the shown look (preview → active slot → stock).
- `src/look.c`, `include/look.h` (finding 20): first use copies the matrix/hue/tone/YCbCr sections to the heap (~170 KB); a look writes CFW records (matrix 11, hue 6, tone 30–32, YCbCr 34–51) and repoints one IQ mode's list entries. Every write holds the ISP mutex and flips the ISP-Pre change cache (`0xA06C8B9A`, `0xA0155D20`); any new IQ stage must do the same.
- `src/overlay.c`, `include/overlay.h` (finding 23): VIEW frame lines and zebras in the RGBA4444 full plane (`BITMAPS[G[1]]`, `G` = `0xA04070AC`) while LCD, plane-mask bit 1, system mode 6/9 and layer 2 on; restamped 40 ms after every flip; `wipe()` + `REC_RGB_REDRAW` (`0xA0424360`) hands the plane back to stock. Zebras poll LV luma every 100 ms in 4×4 cells.
- `src/info.c`, `include/info.h` (finding 21): INFO getters; DEV actions (screenshot in 48-row bands from the shown LV frame + OSD, IQ dump, recipe rescan, overlay diagnostics 1.5 s after the menu closes). Switch rows show OFF / ON (console, with reachability) or OFF / RUNNING / ON (test pattern). Wi-Fi is left to the camera menu: `Prop_Set(0x1E8)` + `Msg_Post(0x116)` reached JOBID_WIFI but no access point appeared; the stock menu also posts `0x10C` (finding 26 §4).
- `src/console.c`, `include/console.h` (finding 21 §3.2/§5): telnet console on port 23 (the stock telnetd is never started), task `CFWcon` created once; `console_set(0)` closes the sockets from the CFW task and removes the printf hook (slot `0xA05FCCD0+0x44`, installed only if empty; it only copies bytes under the console mutex). 16 KiB ring with `cfw_log_tap`. `console_exec` is the testable dispatcher; unknown words run as stock shell commands; `mem` is bounded to DRAM.
- `src/log.c`, `include/cfw_log.h`: append-only `H:/cfw/cfw_log.txt`. `src/mem.c`: `memset`/`memcpy` for compiler calls. `src/arm.S`: CPSR read.
- `loader/`: SD `Startup.ttl`/`Shutdown.ttl`; `probe/` window check (`scripts/check_probe.py`); `flash/` M10 stub, probe, rescue script, design + test plan.
- `scripts/`: `package_sd.py` (zips, checks), `mkmain.py`, `fetch_firmware.py`, `build_loader_frm.py`/`emu_loader.py` (loader `.frm` + independent check), `loader_timing.py` (F0 probe decoder), `make_fonts.py` (Inter / Barlow Condensed → GFNT at 13/15/18/18/22 px; `include/osd.h` FONT_*, `src/osd.c` FONT_FILE and the packager must match).
- `recipes/*.txt`: film files generated by the maintainer's recipe tool (regenerate, do not hand-edit) plus hand-written test recipes (`lvtest`, `t_*`).
- `tools/gr2frm.py`: vendored copy of the canonical gr2frm.py (container format, checksum); change the canonical file and copy it here.
- `tests/emu/`: Unicorn machine model (`machine.py`), scenarios (`run.py`), renderer (`render.py`); see its README.
