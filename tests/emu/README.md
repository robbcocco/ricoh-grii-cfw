# Emulator tests

`make emu` (or `python3 tests/emu/run.py [--variant release|debug] [--render OUT_DIR]` after `make fw`) runs
the built CFW (`bin/<variant>/`) in Unicorn against the stock APP image and checks the scenarios below. It
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
hardware banks. Firmware entry points the CFW calls are Python stubs (`FN`): properties, H: files and
directory listing, My Settings slot files, Memory_Alloc (bump), tasks, time, mutexes, palette/layer,
Msg_Post, ADC, temperatures. Firmware code in `REAL_CODE` runs for real (the string resolver, the two
patched Effect BL sites); executing anything else outside the CFW raises with the address.

**Scenarios** (`run.py`), each on a fresh boot (`_start` from the window, stage 2 from `H:/cfw/gr2_main.bin`,
then the task's `settings_init`, `recipes_init`, `recipes_poll`, `osd_fonts_load`):

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
  &ldr_state)` over the shooting section order with Portra remembered for MY2 and the dial on MY2: the Effect
  BL is stock at hook(5) and patched at hook(10) (M10c); the MY2 look is not shown before the mode-props flag
  `0xA012A0EC` and is shown at the next hook after it; nothing is written to H: before step 13; `cfw_main`
  does not install twice; the debug log starts with the held banner and has the hook timeline. USB boot
  (section 1 only) starts and writes nothing; missing `gr2_main.bin` stays off the card until the task logs
  it; `ex` refused while the loader is active; no step-13 flag gives the 60 s timeout. The stub itself is
  checked by `scripts/emu_loader.py`.
- Task stacks: every call runs on the stack of the task it belongs to (CFW 0x3000, Script 0x4000, boot task
  0x4000, `CFWldr` as created); the stock file API stubs fail when the remaining stack is below the stock
  worst case (finding 31).
- After every scenario: no open file/directory handles, property lock and mutexes balanced, no `Prop_Set`
  outside the lock, no `ERROR` in the debug log; release writes no `cfw_log.txt` at all.

`--render DIR` writes `<variant>_<screen>.png` (menu tabs, recipes strip, apply prompt, screenshot, VIEW
overlays) with the real palette and the full plane (layer 2) over a gradient backdrop.
