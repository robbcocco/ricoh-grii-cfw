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
(D: subdirectories via `File_Mkdir`) and directory listing, My Settings slot files, Memory_Alloc (bump), tasks,
time, mutexes, palette/layer, Msg_Post, ADC, temperatures. A: is the RES drive of the release firmware with the
files of `scripts/rom_files.py` (rom variant): read-only, case-sensitive full paths; `File_Open` mode 0 and
`File_Read`, `LoadFile` (min(size, max) bytes or -1), `FindFirst`/`FindNext` as a raw prefix match returning full
paths (sfn = the first 12 bytes, attr 0x01, no `.`/`..`), as the stock code does in emulation (finding 29 §3). Every
file-system call is logged as (API, path, task) in `Machine.io`; `card=False` models a camera without a card (every
H: open and listing fails). Firmware code in `REAL_CODE` runs for real (the
string resolver, the patched Effect, IQ-ready and power-off BL sites); executing anything else outside the CFW
raises with the address.

**Scenarios** (`run.py`), each on a fresh boot (`_start` from the window, stage 2 from `H:/cfw/gr2_main.bin`,
then one `cfw_task_poll` with every boot phase open: settings, recipes, look, fonts):

- `boot`: rc 0; both stock BLs retargeted to the veneers, veneer targets = hooks; Effect press/hold through
  the patched sites (CFW values counted, others reach the stock function); "CFW Menu"/"Recipes" via the stock
  string resolver; shell record; key hook pass-through vs. capture.
- `menu`: all tabs; Auto ISO writes AE_F +0xBE/+0xC4/+0xC2/+0x2C8/+0xCA (crop, Movie) and restores stock;
  Auto-Hi shutter and Clarity props restored; INFO values and the 1 s cell refresh.
- `snap`: 1m -> 0.7 m, 1.5m -> 1.2 m (tables, literals, code halfwords, labels), back to STOCK.
- `recipes`: previewing each recipe repoints only Setting2's IQ lists (matrix, hue, tone, YCbCr), matrix
  record value, close restores IQ RAM, apply to MY2 (slot file, name cache, config `rc2.*`), dial shows the
  slot look, reboot with that config restores it.
- `palette`: entries 224-251 only, both banks equal while open, stock after close, no shown overlay pixel
  switching between a stock and an overlay colour at a bank flip (the green flash, on opening and closing), a
  scroll reprograms at most 12 entries; ISP mutex balanced.
- `dev`: console OFF → ON with Wi-Fi off → ON reachable (listening socket, Wi-Fi on) → OFF (socket closed, printf hook
  removed) → on again, never touching Wi-Fi (no `Msg_Post`, prop `0x1E8` unchanged), no stock telnetd, one
  `CFWcon` task; IQ dump (0x2FAA0 bytes), recipe reload, screenshot BMP (921654 bytes, pixel-exact against
  LV + OSD).
- `pattern`: DEV test pattern row OFF → RUNNING (menu closed, 1.5 s pending) → ON → OFF.
- `recipe-files`: every file in `recipes/` parses; list sorted by name.
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
  H: access at all before JobSystem step 13 (hooks and the early task). First boot, D: empty, card with a config
  (Clarity 9, MY2 = Portra), a card `portra.txt` with another name, an own recipe and a broken one: Clarity stays 5
  and no look at the IQ-ready call (`settings_early` skipped); after the late phase Clarity 9, config on D: and the
  card copy, the recipe list = the A: recipes (no `lvtest`/`t_*`) with the card's Portra replacing the built-in one
  plus the own recipe, MY2's look shown, look cache on D:, and the log holds only ERROR lines (the broken recipe).
  Second boot with that D: and no card: Clarity 9 and MY2's look at the IQ-ready call, A: recipes only, no log
  write. Stage 1 with one flipped bit (in its zero padding) or truncated to 0x7000 bytes: only the first hook runs,
  `ldr_state` 6, no allocation, no task, no file access besides the stub's, the ERROR line in stage 1's RAM log
  buffer.
- Task stacks: every call runs on the stack of the task it belongs to (CFW 0x3000, Script 0x4000, boot task
  0x4000, `CFWldr` as created); the stock file API stubs fail when the remaining stack is below the stock
  worst case (finding 31).
- After every scenario: no open file/directory handles, property lock and mutexes balanced, no `Prop_Set`
  outside the lock, no `ERROR` in the debug log; release and rom write no `cfw_log.txt` at all (`rom` removes
  its one expected ERROR log after checking it).

`--render DIR` writes `<variant>_<screen>.png` (menu tabs, recipes strip, apply prompt, screenshot, VIEW
overlays) with the real palette and the full plane (layer 2) over a gradient backdrop.
