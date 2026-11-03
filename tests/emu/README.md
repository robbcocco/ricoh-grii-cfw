# Emulator tests

`make emu` (or `python3 tests/emu/run.py [--variant release|debug|rom] [--render OUT_DIR]` after `make fw`) runs
the built CFW (`bin/<variant>/`, all three by default) in Unicorn against the stock APP image and checks the
scenarios below: release and debug run every scenario except `rom`, the rom variant (release firmware, M11) runs
only `rom`. It
takes a few seconds; `make test` (and CI) runs it. Exit 0 = pass, 1 = failure or missing inputs.

**Inputs** come from the stock firmware (`make fw` → `fw/`, never committed):

| Input | Default | Override |
|---|---|---|
| APP image | `fw/b01firm8.bin` | `GR2_FW` |
| `afs/isp/IQ.bin`, `afs/fix/AE_F.bin` | next to the image | — |
| OSD palette | ROM table `0xA04EA83C` in the image, entries 0 and 241 as dumped on hardware | — |

Python packages: `requirements-dev.txt` (`unicorn` 2.x, `numpy`, `Pillow`).

**Model** (`machine.py`): ARM946 big-endian, USR mode. 16 MiB DRAM at `0xA0000000`, also mapped at the
uncached alias `0x20000000` (same bytes). Image at `0xA0020200`, AE_F at `0xA070FC48`, Snap tables seeded
with stock values, IQ.bin at a pool address with absolute section pointers (`*0xA015A430` = ctx, bank 0),
640x480 grey-ramp live-view frame, stock OSD double buffer (front: top and bottom bars using every palette
index), palette with two
hardware banks. Firmware entry points the CFW calls are Python stubs (`FN`): properties, H: and D: files
(D: subdirectories via `File_Mkdir`; on H: a parent must exist and no card fails) and directory listing, the B:
property files `P_COMMON.bin`, `P_MY0k.bin`, `P_BOX0b.bin` (`File_Open` read-only tells whether one exists; a missing
one loads the firmware's property defaults, setup table `0xA0122A7C`; load and save need the file API's stack),
Memory_Alloc (bump), tasks, time, mutexes, palette/layer, Msg_Post, ADC, temperatures. A: is the RES drive of the
release firmware with the files of
`scripts/rom_files.py` (rom variant; no recipes) plus a scenario's `a_extra` files: read-only, case-sensitive
full paths; `File_Open` mode 0 and `File_Read`, `LoadFile` (min(size, max) bytes or -1), `FindFirst`/`FindNext`
as a raw prefix match returning full paths (sfn = the first 12 bytes, attr 0x01, no `.`/`..`; no match: no
handle), as the stock code does in emulation (finding 29 §3). Every
file-system call is logged as (API, path, task) in `Machine.io`; `card=False` models a camera without a card (every
H: open and listing fails). With `on_delay` set, a `Task_Delay` stops the call, runs other tasks' code
(`call_in_task`: its own stack area, the caller's registers restored) and resumes it. Firmware code in `REAL_CODE`
runs for real (the string resolver, the patched Effect, IQ-ready, power-off and still effect-step BL sites, and the
effect step's YCC plane-address arithmetic `0xA02AD260`); executing anything else outside the CFW raises with the
address. `map_dram(base, size)` maps extra DRAM (cached and uncached alias, one host buffer) for frame buffers
larger than the 16 MiB model; the stock still effect step `0xA02AF6CE` is a stub that records the call and runs
`on_fx_step(machine, ctx)`.

**Scenarios** (`run.py`), each on a fresh boot (`_start` from the window, stage 2 from `H:/cfw/gr2_main.bin`,
then one `cfw_task_poll` with every boot phase open: settings, recipes, look, fonts):

- `boot`: rc 0; both stock BLs retargeted to the veneers, veneer targets = hooks; Effect press/hold through
  the patched sites (CFW values counted, others reach the stock function); "CFW Menu"/"Recipes" via the stock
  string resolver; shell record; key hook pass-through vs. capture.
- `menu`: all tabs; Auto ISO writes AE_F +0xBE/+0xC4/+0xC2/+0x2C8/+0xCA (crop, Movie) and restores stock;
  Auto-Hi shutter and Clarity props restored; INFO values and the 1 s cell refresh.
- `snap`: 1m -> 0.7 m, 1.5m -> 1.2 m (tables, literals, code halfwords, labels), back to STOCK.
- `recipes`: previewing each recipe repoints only Setting2's IQ lists (matrix, hue, tone, YCbCr), matrix
  record value, Stock after the last recipe (no look, Right stops there), close restores IQ RAM, apply to MY2
  (slot file, name cache, config `rc2.*`), dial shows the slot look, reboot with that config restores it, also
  from the look cache without the file. Stock on MY2 with the dial on it and every recipe property of the slot
  file off its factory value: previewing Stock shows no look although MY2 has one; applying it puts every
  property a recipe can set (EFFECT, IMAGE_SETTINGS, the image-setting arrays of all colour modes, NR and its
  thresholds, EV comp, WB mode, WB comp, Kelvin, ISO, ISO AUTO-HI max/min/shutter, DR) and the name back to the
  firmware defaults in the file and live, changes no other slot property, clears the name cache, box link and
  `rc2.*`, shows no look, and none after a reboot. The defaults table is spot-checked against finding 19 §1.3.
- `palette`: entries 224-251 only, both banks equal while open, stock after close, no shown overlay pixel
  switching between a stock and an overlay colour at a bank flip (the green flash, on opening and closing), a
  scroll reprograms at most 12 entries; ISP mutex balanced.
- `dev`: console OFF → ON with Wi-Fi off → ON reachable (listening socket, Wi-Fi on) → OFF (socket closed, printf hook
  removed) → on again, never touching Wi-Fi (no `Msg_Post`, prop `0x1E8` unchanged), no stock telnetd, one
  `CFWcon` task; Wi-Fi name and password in one row; IQ dump (0x2FAA0 bytes), recipe reload, screenshot BMP (921654
  bytes, pixel-exact against LV + OSD).
- `pattern`: DEV test pattern row OFF → RUNNING (menu closed, 1.5 s pending) → ON → OFF.
- `halation-probe` (debug; release: no row, no effect veneer, BLs stock): DEV "Halation probe" is the 11th row, so
  the page scrolls to it (DOWN past the last row wraps, UP scrolls back). OK arms: both still effect-step BLs
  (`0xA02AEDC0`, `0xA02AEF30`, stock `F000 FC85`/`F000 FBCD`) become `F56F` + the same low halfword (veneer
  `0xA001E6CE`, bytes checked; its target word = `fxprobe_hook`). Against a synthetic YCC frame in a 34 MB mapped
  region (the stock buffer layout of `FUN_a02AE7EA`, 1 MiB guards, every byte patterned), each frame size of
  3:2, 1:1, 16:9, a crop and one smaller than the square, run through the patched BL in a `ProcessStill` task
  (both callers): the stock step is called once and paints the Cr frame first; afterwards exactly a 512x512 square
  (clipped to 300x200 in the small frame) at the frame centre holds paint + 64 (saturating) and every other byte
  of the region is unchanged (Y included); no file I/O in that task; the CFW task's log has the shot, caller,
  geometry, buffer, plane addresses (= stock layout), square and a Y word sum equal to the frame's. Contexts with
  the Cr plane across the buffer end, a frame wider than the Y pitch, more rows than the plane, a huge pitch, a
  handle without "BUFF" or no chroma flag change nothing and log their status. OK disarms (BLs stock); the hook
  reached anyway (stale I-cache) only calls the stock step. A foreign halfword at one site: arming refused
  ("OFF: call sites not stock", one ERROR) with no write to either site (write hook); after the fix two writes.
- `recipe-files`: every file in `recipes/` and the look-stage test recipes built in `run.py` (matrix, `tone`,
  `tone_r`, hue sectors, post matrix) parse; list sorted by name, then Stock, also after card recipes named
  "zzz" and "Stock" (and the `~Test` ones); each entry previews its look stages in Setting2, the last none.
- `view`: Frame lines XPan from the VIEW tab stamped into the shown full-plane buffer (mask/line rows exact),
  restamped after a stock flip to the other buffer; zebras (Y >= 252 on the LV ramp) stripe only the hot
  cells, cold cells restored from the stock canvas + frame; OFF clears and requests a stock redraw; nothing
  drawn while layer 2 is off; 1:1 frame; DEV test pattern on/off (off wipes and the frame is restamped);
  state log.
- `ae-hooks`: "TAv: Auto-Hi minimum too" and "Auto ISO in M" write exactly their code halfwords and restore
  them (the EV-comp getter stays stock); config keys; re-applied from the config at boot; a foreign guard
  halfword blocks the patch with one logged ERROR (the hook code itself is not executed).
- `console`: the DEV row starts the `CFWcon` task once and installs the printf hook (off/on reinstalls it); captured printf and CFW
  log lines in `log`; `sh` output + result text; unknown words run as stock commands; `wifi`; `tasks` names
  from the TSK table; `heap` walk bounded; `mem` bytes and DRAM bounds; `status`; truncation; one telnet
  session through `session()` with scripted `recv` (option bytes skipped, DEL, empty lines, `exit`).
  Socket setup/accept are not modelled.
- `export`: MY1 (Setting2, Kelvin WB with compensation, +0.7 EV, custom NR, ISO, AUTO-HI; linked to Portra by the
  config), MY2 (Cross Process, a name with `#`), Box 1 (Hi-Contrast B&W contrast −2, manual WB, a 1/2-step EV) and
  Box 3 (Shift Crop, no name) as slot files, MY3 and the other boxes never saved, live Setting1 with changed
  sliders, a changed Positive Film and the CFW Clarity strength 9 in force. DEV "Export settings to card" writes
  exactly my1/my2/box01/box03/live/set1/set2/fx09 with the expected keys (manual WB and the EV as comments, the `#`
  replaced, no `fx16` because the camera's Clarity is the factory one), ASCII CRLF ≤ 4094 bytes, nothing at boot,
  no slot file or property changed, every file call in the CFW task within its stack; my1.txt carries Portra's look
  keys (same look as portra.txt), no other file any. No card: "no card", nothing written. `export_run` on an 8 KB
  stack trips the stack check. Round trip: every file from `cfw/recipes` applied to MY3 sets each property its keys
  name to the source value, its name, and nothing else; the CFW parses my1.txt to Portra's look and MY3 then holds
  `MY1.TXT`. Console `export` with the dial on MY1 (CFWcon waits in `Task_Delay`, the CFW task runs it): 7 files,
  written by the CFW task, live.txt with Portra's look, set1.txt from `B:/P_COMMON.bin`; without the CFW task the
  console gives up after its wait.
- `loader`: stage-1 loader ABI v1 header (`b`, "GRL1", hook entry); a full boot through `cfw_ldr_hook(n,
  &ldr_state)` over the shooting section order with Portra remembered for MY2, Clarity 9 configured and the dial
  on MY2 (slot Clarity 5): AE_F is zero until CapInit(5); the IQ-ready BL is patched at hook(5) and the patched
  stock BL applies the settings (Clarity 9) and the MY2 look, not before; the Effect BL is patched at hook(10);
  the CFW task is created once at hook(10) and polled between hooks: nothing before JobSystem step 10, fonts
  only after the hook after hook(9), the late phase (settings_init, saves) only after `cfw_main`, which then
  records MY2's Clarity 5 as the stock value; nothing is written to H: before step 13; `cfw_main` installs and
  starts nothing twice; the debug log starts with the held banner and has the hook timeline. USB boot (section
  1 only) starts and writes nothing; missing `gr2_main.bin` stays off the card until the task logs it; `ex`
  refused while the loader is active; no step-13 flag gives the 60 s timeout and the task never reaches its
  late phase. The stub itself is checked by `scripts/emu_loader.py`.
- `store`: config on D: in two generations with the card copy (finding 34): import, alternation, a torn newest
  file, D: over a stale card copy, defaults with nothing.
- `rom` (rom variant only): `bin/rom/gr2_cfw.bin` carries valid length/CRC fields (`scripts/stage1_crc.py`). Boots
  through a model of the release stub (`loader/flash/rom_stub.S`: at section 3 `LoadFile(A:/cfw/gr2_cfw.bin)` into
  the window, ABI magic, then the hook after every section load; the stub itself runs for real in
  `scripts/emu_release.py`) with the shooting order and the CapInit(5) world of `loader`. Every boot: stage 1 from A:
  by the stub, stage 2 by two `LoadFile` calls of stage 1, fonts from `A:/cfw/fonts` by the early CFW task, and no
  H: access at all before JobSystem step 13 (hooks and the early task). First boot, D: empty, no A: recipes, card
  with a config (Clarity 9, MY2 = Portra), a card `portra.txt` with another name, an own recipe and a broken one:
  Clarity stays 5 and no look at the IQ-ready call (`settings_early` skipped); after the late phase Clarity 9,
  config on D: and the card copy, `A:/cfw/recipes/` listed, the recipe list = the card's two plus Stock, MY2's
  look shown, look cache on D:, and the log holds only ERROR lines about the broken recipe. Second boot with that
  D: and no card: Clarity 9 and MY2's look at the IQ-ready call (look cache), only Stock listed, no log write.
  Third boot with two recipe files added to A: and the card's `portra.txt`: the card file replaces the A: one
  (both read). Stage 1 with one flipped bit (in its zero padding) or truncated to 0x7000 bytes: only the first hook runs,
  `ldr_state` 6, no allocation, no task, no file access besides the stub's, the ERROR line in stage 1's RAM log
  buffer.
- Task stacks: every call runs on the stack of the task it belongs to (CFW 0x3000, Script 0x4000, boot task
  0x4000, `CFWldr` as created); the stock file API stubs fail when the remaining stack is below the stock
  worst case (finding 31).
- After every scenario: no open file/directory handles, property lock and mutexes balanced, no `Prop_Set`
  outside the lock, no `ERROR` in the debug log; release and rom write no `cfw_log.txt` at all (`rom` removes
  its one expected ERROR log after checking it).

`--render DIR` writes `<variant>_<screen>.png` (menu tabs, the scrolled DEV page, recipes strip, apply prompt,
screenshot, VIEW overlays) with the real palette and the full plane (layer 2) over a gradient backdrop.
