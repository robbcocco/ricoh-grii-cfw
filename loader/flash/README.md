# M10 flashed loader: debug firmware (prepared, NOT flashed)

A 132-byte stub in the APP image that loads the CFW's stage 1 from the card early in boot. The CFW itself stays on the card. Since 2026-11-01 this is the **debug** firmware only: the release flashes the CFW itself and never loads code from the card (the project plan M11), using the same stub with an `A:` path. Nothing here has run on hardware. Nothing has been flashed and nothing is written to any SD card by these tools.

| File | Purpose |
|---|---|
| `ldr_stub.S`, `ldr_stub.ld` | Stub source and the two patch placements |
| `../../scripts/build_loader_frm.py` | Builds `dist/rg2_v003_loader.frm` and `dist/rg2_v003_loader.diff.txt` from the stock `fw/rg2_v003.frm` (`make fw`). Run by `make loader` / `make test` and CI; the output contains Ricoh's firmware and is never published. |
| `../../scripts/emu_loader.py` | Independent byte check of that `.frm` plus a Unicorn run of the patched hook path |
| `probe/Startup.ttl` + `../../scripts/loader_timing.py` | Data-only probe: boot timeline (the APP's own section-load profile) and the loader status word |
| `rescue/backup.ttl` | Rescue-card script for BAK: runs `up`, then `forcedown` |

Tags: [V] = read in code/data or observed in emulation; [H] = observed on hardware; [I] = inference. **[R 2026-11-01]** = re-verified on 2026-11-01 with an independent script (`scratch analysis`: own capstone disassembly, own checksum loop, own diff) against `fw/b01firm8.bin` and the stock `rg2_v003.frm`.

## 1. What gets patched

Only APP (`b01firm8`, type 1) changes. RES (`b01firm6`) is byte-identical. The container holds no IPL or BAK module, and the builder fails if one appears. Image offset = RAM − `0xA0020200`.

| Image offset | RAM | Bytes | Stock → patched |
|---|---|---|---|
| `0x00016C` | header | 4 | nUpdateCsum `826A5E33` → `8268CB1D` (recomputed by `gr2frm.repack`) |
| `0x002C5C` | `0xA0022E5C` | 4 | `F01C FE51` `bl 0xA003FB02` → `F08C FF48` `bl 0xA00AFCF0` |
| `0x08FAF0` | `0xA00AFCF0` | 132 | all `0xFF` → stub: 100 B code, 8 B literals, 19 B path, 1 B `0xFF` fill, status word `ldr_state` = 0 at `0xA00AFD70` |

137 bytes differ in the `.frm`, all inside the APP payload. `gr2frm.py verify` passes: round trip, header sizes and both checksums. Output SHA-256: `497e5c987f58847d85cfe0eda1ba5f376faebf98e791b6ae5e1986ca650cb514`. The full byte listing and disassembly are in `dist/rg2_v003_loader.diff.txt`.

**[R 2026-11-01]** Everything in this table, re-checked:
- `fw/b01firm8.bin` is byte-identical to the APP image unpacked from the stock `.frm` (`.frm` SHA-256 `3643850f…8331`); RES is identical in the loader `.frm`.
- Rebuilding from a clean copy reproduces the SHA-256 above, so the build is deterministic.
- The APP diff is exactly the three ranges in the table.
- Own checksum loop (finding 15 §7.1, load base = header `+0x20` = 0, `nUpdateTotalSize` `0x5D9054`): stock `0x826A5E33` and patched `0x8268CB1D`, both equal to the header word.
- Hook disassembly, stock and patched: `movs r1,#0; movs r0,r5; bl …; movs r0,r7`.
- The stub disassembles as Thumb. Every `adr` and PC-relative `ldr` resolves to the right slot: `ldr_state` `0xA00AFD70`, literals `0xA0017000`/`GRL1` at `0xA00AFD54`/`D58`, path at `D5C`. Its BL targets are the four functions in §2, each starting with `push`.
- `Port_Get(0x27)` resolves through the pin table `0xA00B9550` to entry `0x0061` = port 6 bit 1, register `0x90001018` (single-argument function).

## 2. Static verification (fw 3.00)

- **Hook site [V].** `0xA0022E5C` holds `F01C FE51` = `bl 0xA003FB02`. It sits inside `FUN_a0022e28(n)` and runs only when section n is not loaded yet (`FUN_a0040dc0(n) == 0`).
  - The BL is wrapped by the profiling thunks TCM `0x2004`/`0x2094`/`0x20A8`. These are timer bookkeeping only (`System_GetTime`, record = count, total, max, min, start). They do not mask IRQs and take no lock.
  - `0xA003FB02` takes the "Update" mutex around the NAND read `FUN_a0056722`. The stub's file I/O happens outside that mutex.
  - The caller passes `r1 = 0` (`A0022E58`), ignores `r0` and keeps `r4`–`r7`.
- **Callers [V].** `FUN_a0022e28` is called only by the root init task `0xA0020944`, the first RTOS task (kernel config `0xA00B5B98`: idle `0xA0021F34`, root `0xA0020945`, stack `0x4000`). Section 1 is always loaded first.
  - Shooting order: 1 4 2 3 5 10 11 8 9 6 12 13.
  - Playback branch (`notify(1) == 1`): 1 2 3 8 9 10 11 6 4 5 12 13.
  - `notify(1) == 2` (USB/PC boot): section 1 only.
  - Section-load is the stub's only caller path; `0xA003FB02` has exactly one caller.
- **Free space [V].** `0xA00AFCF0..0xA00AFD73` is 132 B of `0xFF`, inside section 0 (`0xA0020400` + `0xA46C4`, loaded by the IPL).
  - It lies between the vector literal pool (vectors `0xA00AFC30`, handler table `0xA00AFC50`, literals up to `0xA00AFCEF`) and the `ldr pc,[pc,#-4]` veneers at `0xA00AFD74`.
  - No 32-bit word in the image points into `0xA00AFB00..0xA00AFE00` apart from `0xA00AFB08`/`0xA00AFC1C` (held at `0xA00AF878`/`0xA00AFC20`) [R 2026-11-01].
  - The neighbourhood `0xA00AE000..0xA00B0E00` is ARM code (kernel, vectors, veneers, ARM helpers). No ARM PC-relative load or `adr` within ±4 KiB, and no ARM B/BL or Thumb BL anywhere in the image, targets the range. The only Thumb-BL decode hit was in a byte table at `0xA00B5066`.
  - MPU: region 5 (`0xA0000000`, 512 MiB) has data AP 3, so the stub can write `ldr_state` [V]. The CFW already writes code RAM from USR mode [H].
- **Call targets [V]**, all Thumb (`push` at entry):

  | Function | Address | Behaviour |
  |---|---|---|
  | `Port_Get` | `0xA00408D8` | Pure read of `0x90001000 + 4*port`, bit `bit` (pin table `0xA00B9550`). Pin `0x27` = P6.1 = "RL1" (shutter table `0xA01143C8`, active level 0). |
  | `File_MountMedia` | `0xA002D350` | Same function DCFMount uses. Card power, controller init, unmount + mount. Returns −1 on failure. |
  | `LoadFile` | `0xA002CD2C` | Non-`A:` path: fopen/fread/fclose. Returns bytes read (≤ max, **truncates silently**) or −1. |

- **What is ready at the first hook [V calls, I functional].** Stage 0 has run:
  - GlobalHeap (2 MiB) has been created (`FUN_a0024bca`), so `Memory_Alloc` works.
  - The RTOS module has been initialised (`FUN_a0026670`, "RTOS.c"). `Task_Create` allocates its TCB with `Memory_Alloc`.
  - `File_Init` (`FUN_a002c11a`) has run.
  - Stage 0 also loads `A:/fix/Sys_F.bin` with `LoadFile`, so file I/O works by then.
  - The SD driver is in section 0 (`0xA00A2058`…).
- **What is not ready at the first hook:**
  - H: is not mounted. File_MountMedia is called only by DCFMount, its caller `FUN_a029e5b8` (both section 8), the shell, card hotplug and `up` [V callers]. So the stub mounts H: itself, which is BAK's pattern [I].
  - DCFMount later calls File_MountMedia again unconditionally [V], which unmounts and remounts [I].
  - Because the DCF code lives in section 8, nothing can remount H: while section 8 is not loaded [V]. That is always true at hook(1).
  - Property values: Prop_Get is section 1 code; the property DB is loaded by JobSystem step 1 after `notify(1)` [I].
  - Menus, Effect hooks and the input handler slot `0xA04071F0` (initial value `0xA039725F` in section 10 `.data`): section 10.
  - Shell init `FUN_a0029ff8`: stage `0xC`, after the section-12 load.
- **IQ / live view [V].** `FUN_a015d458(5)` → `FUN_a012ff5a` loads `A:/isp/IQ.bin` into "IQParam" after section 5. So IQ RAM is ready at the hook after the one for section 5 (n = 10 in shooting order, 12 in playback order). Live view starts only with the JobSystem steps after `notify(5)` (finding 22 §4 [I]).
- **What the updater checks (fw 3.00) [R 2026-11-01, new].** Relevant to F1a/F2/U.
  - Header check `FUN_a0056ece` (called from both `up` search paths):
    - type `+0x10` in 0..3, `+0x11` = 0;
    - **version range** only when `+0x16` (`bcdUpdateCmpVer`) ≠ 0: the installed module's version byte must lie in [`+0x16` high byte, low byte];
    - `+0x168` = file size;
    - address range `+0x160` vs `+0x20/+0x164` or `+0x24/+0x28`;
    - 12-byte board name `+0x194` against the installed type-3 module.
  - In both stock modules `+0x10..0x17` is `01/02 00 0300 0000 0000`: **`bcdUpdateCmpVer` = 0, so `up` does no version comparison at all.** The loader patch touches none of these fields (only `+0x16C`, which the header check does not read, finding 04).
  - Menu flow (`FUN_a00eb886`, AXJobSetup):
    - it reads the installed versions (`FUN_a00e5c96` → props `0x231..0x234`) and the card's versions (`FUN_a00e5d14` → props `0x237..0x23A`, module order 3,0,2,1 from the table at `0xA00C320C`);
    - status prop `0x249` = `0x19` when `PROPID_BATTERY_LEVEL` (`0x24A`, 0..3, finding 21) is < 3 [V]. That `0x19` is the "battery too low, no update" message is [I] (the dialog code was not read), so treat a full battery indicator as required;
    - status `0x1A` when no module was found on the card.
  - The version text (`FUN_a03a3ad8`, `"%X.%02X"`) shows the APP version (`0x234` installed, `0x23A` card). It only displays the version and compares nothing.
  - **[I]** No version comparison was found anywhere on the menu path. Code not read here (the section-10 dialog handlers that consume `0x249`) could still hold one, so F1a remains the test.
  - BAK has its own copy of the header check (finding 15); it was not re-read today. That it behaves like the RTOS one is [I].

## 3. Stub behaviour (`ldr_stub.S`)

First call only (`ldr_state == 0`), in this order:

1. If `Port_Get(0x27)` is 0 (shutter half-press held): `ST_SKIP` = 1.
2. If `File_MountMedia("H:/cfw/gr2_cfw.bin")` returns −1: `ST_NOMOUNT` = 2.
3. If `LoadFile(path, 0xA0017000, 0x8000)` returns ≤ 0: `ST_NOFILE` = 3.
4. If the word at `0xA0017004` is not `0x47524C31` ("GRL1", loader ABI v1): `ST_NOABI` = 4. Stage 1 carries it since M10b (2026-11-01); an older stage 1 is loaded but never run.
5. Otherwise `ST_ARMED` = 5.

Every call then runs the stock `Section_Load(n, 0)`. If `ldr_state` is still 5, it calls `*(0xA0017008)(n, &ldr_state)` (Thumb) and returns the stock result.
- The skip key is read before anything touches the card, so it also bypasses a mount or file-load that hangs.
- `ldr_state` lives in section 0, which the IPL loads from NAND before every APP start, so it is 0 again on every boot [V cold boot; I warm `reboot app`, which also re-reads the IPL and its window tail as `0xFF`].

The 132 bytes left no room for a `DriveMounted` pre-check or a range check of the entry pointer. On a cold boot nothing has mounted H: before hook(1) (§2). The magic word is the guard that an old stage 1 is never called.

### Stage-1 loader ABI v1 (implemented 2026-11-01, M10b; emulated, not run on hardware)

- `gr2_cfw.bin` +0: `b` over the header (the `ex 0xA0017001` entry is unchanged). +4: `0x47524C31`. +8: Thumb address (odd) of `cfw_ldr_hook(uint32_t n, volatile uint32_t *ldr_state)`, inside the window (`src/entry.S`, `src/boot.c`). `package_sd.py` checks all three.
- The hook runs in the root init task, right after section n was loaded and before anything executed it. Every millisecond it spends delays boot.
  - It must return.
  - It may write `*ldr_state = 6` to stop further calls.
  - On the first call it zeroes stage 1's `.bss` (as `_start` does).
- **Implemented policy (M10b):** the CFW comes up at the same point as with `Startup.ttl`, without the devel gate or TTL.
  - **hook(1):** window check, then read `H:/cfw/gr2_main.bin` into the heap and relocate it. This is the only point where the read cannot race DCFMount, because section 8 is not loaded yet.
    - Nothing is written to H:. Log lines are muted, and the first one is kept and replayed later. In a USB/PC boot only section 1 loads and the PC then owns the card.
    - No task is started.
  - **hook(13):** `Task_Create` the `CFWldr` task and set `*ldr_state = 6`. Section 13 is last in both boot orders.
  - **`CFWldr`:** it waits until JobSystem_Boot has finished step 13, where Startup.ttl would run. The task polls the step flag `Ctrl()+0x54+4·13` set by `FUN_a00e87dc` [V decompile], every 100 ms, with a 60 s timeout. Then it calls `cfw_main` like the Startup.ttl path (`boot.loader = 1` and the hook(1) time are logged in debug builds). The task then sleeps forever: task bodies never return.
  - A failure at hook(1) disarms nothing early. `CFWldr` logs the kept line plus `stage 2 not loaded at hook(1)` once the card belongs to the camera, and does not start the CFW.
- **Next (M10c, after the F0 timing probe):** the two parts that make the loader worth more than Startup.ttl.
  - **Patch-on-load, hook(n):** code patches for section n are plain stores before that code first runs: no single-halfword constraint, no I-cache issue. Examples: the Effect press/hold BLs and the string-accessor bounds (section 10), and the input handler slot (section 10 `.data`). Never patch a section before its own hook: the load overwrites it and clears its BSS.
  - **Look before the first live-view frame:** at the hook after the one for section 5 (IQ RAM ready), write the active slot's look into IQ RAM. No ISP cache poke is needed before LV starts. It needs the active mode/MY slot, and property-DB readiness at that point is [I]: check JobSystem step 1 (`Ctrl()+0x54+4`) or read the slot file.
- **The loader-mode card must not carry the CFW's `Startup.ttl`.** Its `fload` rewrites the running window: `.data` reset, `.bss` and the veneer target words zeroed.
  - Stage 1 now refuses `ex` while `ldr_state` is 5 or 6: it logs `flashed loader active` and starts nothing.
  - The waiting `CFWldr` then finds no stage 2 and does not start the CFW either.
  - With the **same** file this ends without a crash. A **different** stage-1 build on the card would replace code under the waiting task, so never mix them.

## 4. Emulation and byte check (run 2026-10-31, case H added 2026-11-01; all re-run 2026-11-01 [R])

`make loader` runs `scripts/build_loader_frm.py` and then `python3 scripts/emu_loader.py`, which does two things:

- **Byte check.** RES is identical. APP differs only in `0x16C–0x16F`, `0x2C5C–0x2C5F` and `0x8FAF0–0x8FB73`, and every one of these ranges is patched. Every changed container byte maps onto those APP bytes. nUpdateCsum equals the plain formula sum of finding 15 §7.1, and `gr2frm.py verify` exits 0.
- **Unicorn run.** ARM946, BE-32, patched APP mapped at `0xA0020200`. The real hook site `0xA0022E58..0xA0022E60` runs once per section load, so the flashed stub runs as is.
  - Any execution outside the hook site and the stub, any write other than the stack and `ldr_state`, a changed `r4`–`r7`/SP or a lost stock result fails the run.
  - Run against the stock `.frm`, every scenario fails, which shows the check discriminates.

| Case | `ldr_state` | Mount / LoadFile | Stock loads | Stage-1 hook calls | Window |
|---|---|---|---|---|---|
| A file with loader ABI | 5 | 1 / 1 | all 12, in order | after each load, n = 1…13 | file |
| B file absent | 3 | 1 / 1 | all 12 | none | 0xFF |
| C half-press held | 1 | 0 / 0 | all 12 | none | 0xFF |
| D no card | 2 | 1 / 0 | all 12 | none | 0xFF |
| E stage 1 without loader ABI (pre-M10b bin, magic cleared) | 4 | 1 / 1 | all 12 | none | file (same as `fload`) |
| F playback order, ABI | 5 | 1 / 1 | all 12 | after each load | file |
| G stage 1 disarms at n = 5 | 6 | 1 / 1 | all 12 | 1 4 2 3 5 | file |
| H current `bin/release/gr2_cfw.bin` (ABI v1) | 5 | 1 / 1 | all 12, in order | after each load, at its real +8 entry | file |

The stage-1 side of the ABI runs for real in `make emu` (scenario `loader`, `tests/emu/run.py`):
- full boot: preload at hook(1), `CFWldr` + disarm at hook(13), `cfw_main` after the step-13 flag, Effect BL retargeted;
- USB boot (hook(1) only): no task and no card write;
- `gr2_main.bin` missing: no card write at hook(1), the error is logged by `CFWldr`;
- `Startup.ttl` while armed: refused;
- no step-13 flag: 60 s timeout, the CFW is not started.

The models return what the decompiles show: Port_Get 0/1, MountMedia −1 on failure, LoadFile bytes or −1. Real mount, card and timing behaviour can only be checked on hardware.

## 5. Time gained (measured by F0, 2026-11-01 [H])

F0 on the stock APP (`LDR_STATE` `FFFFFFFF`; data in the maintainer's hardware data), section loads in systime ms:

| Section | 1 | 4 | 2 | 3 | 5 | 10 | 11 | 8 | 9 | 6 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| start | 29 | 114 | 177 | 274 | 301 | 538 | 887 | 888 | 1281 | 1500 | 1702 | 1876 | 1907 |
| load ms | 36 | 32 | 0 | 27 | 33 | 279 | 1 | 392 | 0 | 201 | 9 | 25 | 0 |

- **Today:** `Startup.ttl` runs in JobSystem step 13; CFW task start ≈ 2.0 s systime [H, M1].
- **hook(1)** runs at **29 ms**: stage 1 in RAM ~1.97 s earlier than today.
- **IQ RAM ready** by the hook after section 10 at **817 ms**: the recipe look (M10c) can be written ~1.2 s before the SD build could, before live view starts.
- **hook(13) / step 13 (M10b as built):** all sections are loaded at 1907 ms, so starting the CFW task after step 13 gains only **~0.1 s**. M10b's value is independence from `Startup.ttl`/DEVEL_MODE; the time gain needs M10c (patch-on-load, early look).
- **Cost:** with a card inserted and no skip key, every boot also pays one early H: mount plus reading ≤ 32 KiB. In the probe that time shows up inside section 1's load time.
- **Measure without flashing (F0):**
  1. Copy `probe/Startup.ttl` to the card as `H:/Startup.ttl`, power on, wait for `H:/cfw/probe/PROF_DONE`, power off.
  2. Run `python3 scripts/loader_timing.py <dir> --task-start <systime from a debug CFW boot's cfw_log.txt>`.
  3. It prints each section's start and duration (ms), the hook(1) time, the IQ-ready hook time and the gains.

## 6. User-side test plan (updated 2026-11-01; each step only after the previous one passed)

Nothing below has been run. Steps F0–F4 are M10; M11's P-steps come after F3 and cannot be built yet (§6.4).

### 6.1 Preparation (once, on the Mac; no camera)

1. Run `make check` (= `verify_safety.py`). It must print `STATUS: ALL GATES PASSED`: the stock `rg2_v003.frm` is factory stock and the NAND dump the maintainer's hardware data is present. It never inspects the card.
2. Run `make test` (lint, ISA audit, `make emu` with `PASS release/loader` and `PASS debug/loader`, `make loader` with `ALL PASSED`). Passed on 2026-11-01 [R]. `dist/rg2_v003_loader.frm` must have SHA-256 `497e5c98…b514`.
3. Prepare the cards. `make flash-kit` writes `dist/flash/<step>/` with a `STEP.md` and the exact card-root contents per step (`card/`, or `card-L/` + `card-N/` for F2), all hashes checked. Format each card FAT32, MBR, ≤ 32 GB. Delete `._*` files after every copy from macOS. After copying, check each `.frm` on the card with `shasum -a 256`. There are four roles:

   | Card | Root contents | Notes |
   |---|---|---|
   | **R** rescue | stock `rg2_v003.frm` (`3643850f…8331`) + `loader/flash/rescue/backup.ttl` as `backup.ttl` (ASCII, CRLF) | Never left in the camera. Each BAK entry (combo, crash, broken APP header) runs `up` from it. |
   | **S** stock flash | only the stock `rg2_v003.frm` | F1a, uninstall |
   | **L** loader flash | only `dist/rg2_v003_loader.frm`, **renamed `rg2_v003.frm`** (`497e5c98…b514`) | F2. Exactly one `rg2_v*.frm`, no `backup.ttl`. |
   | **N** normal | per step: `loader/flash/probe/Startup.ttl` as `Startup.ttl`, plus the debug zip for F3 | DCIM etc. may stay |

   - Card R deliberately departs from the "no `backup.ttl`" parachute rule (AGENTS.md §3/§4). That rule exists because a `backup.ttl` replaces BAK's automatic cause-6 `up`. This script runs `up` itself, so cause 6 still reflashes, and the combo (cause 8) becomes a manual reflash path. Amend the rule only after F1b passes.
   - The combo, BAK running `backup.ttl` and shell commands from it are [H] 2026-10-31 (`bak_test`).
4. Camera prerequisites:
   - `PROPID_DEVEL_MODE = 1` (already set for the CFW), so the probe `Startup.ttl` runs.
   - The battery shows **full**. The menu flow sets a separate status below level 3 [V §2], most likely a refusal [I].
   - A second charged battery.
   - Know the combo (dial MOVIE + shutter **fully** pressed + POWER) and the skip key (shutter **half**-pressed while switching on). Never press fully with the dial on MOVIE when you mean "skip".
5. `SD.dat` is **not** part of the kit. It is loaded only by a manual `reboot fjb` in a BAK shell and is not a recovery image (findings 12, 13, 15). Never use `up ipl`, `up bak`, `fe`, `fbe`.
6. Flash nothing from `dist/` except the file named in the step.

### 6.2 Steps

**F0. Timing probe (no flash).**
- Card: N with the probe `Startup.ttl`. Any CFW `Startup.ttl` must be removed.
- Do: power on, wait until `H:/cfw/probe/PROF_DONE` exists (a few seconds after the LCD comes up), power off. Copy `H:/cfw/probe/` to the Mac. Run `python3 scripts/loader_timing.py <dir> --task-start <systime>`, taking the systime from a debug CFW boot's `cfw_log.txt` (`task: running`).
- Pass: `BOOT_PROF.BIN` (168 B) decodes into 12 section loads; `LDR_STATE.BIN` = `FF FF FF FF` (stock APP).
- Fail: no files means DEVEL_MODE is off or the TTL encoding is wrong. Nothing was written to flash.

**F1a. Same-version stock update (first flash; stock bytes).**
- Card: S. Battery full.
- Do: MENU → Fn1 → WB (Set-up) → Confirm Firmware Version → Yes (official procedure, finding 07).
- Expected: the dialog shows current 3.00 and card 3.00. The update runs ("Update … firmware", LED activity) and the camera restarts.
- Pass: the camera boots, Confirm Firmware Version shows 3.00, a shot and its playback work.
- If not offered, or an error before the update starts: nothing was erased, because header and checksum are checked in "Search Files." before "Destroy APP." [V]. **Stop: M10 and M11 need another install path.** Note the exact message. A battery message means the battery was not full.
- If interrupted (power loss, battery out) after "Destroy APP.": leave card S in and switch on. IPL → BAK cause 6 → BAK's automatic `up` from the `.frm` in the slot [V static, I hardware]. If nothing happens: card R + combo (F1b).

**F1b. Rescue path (stock bytes).**
- Card: R.
- Do: combo (cold). Then wait without touching anything until the camera switches itself off.
- Expected: IPL cause 8 → BAK → `backup.ttl` → `up` (LED, minutes) → reboot or `forcedown`.
- Pass: the off phase came after minutes of activity, not seconds. Remove R, insert N, power on: stock 3.00 boots.
- Caveat: a refused `up` followed by `forcedown` also ends with the camera off, only sooner. Duration is the only distinguishing observation (§7 risk 4).
- Fail (stays in BAK, or off within seconds): battery pull, remove R, normal boot. The APP is intact unless `up` reached "Destroy APP."; then card S or R + any power-on runs `up` again. Report it, and do not go to F2.

**F2. Flash the debug loader (first recomputed checksum).**
- Card: L. Battery full.
- Do: menu update as in F1a. After the restart, power off, swap to card N with the probe `Startup.ttl` and **no** `cfw/gr2_cfw.bin` (move the `cfw/` folder away). Power on and wait for `PROF_DONE`.
- Pass:
  - The boot looks stock.
  - `LDR_STATE.BIN` = `00 00 00 03` (`ST_NOFILE`).
  - `loader_timing.py`: section 1's time ≈ F0 + the mount/open cost.
  - Shooting, playback and card access are OK across 3 power cycles.
- Fail:
  - Update refused: nothing erased. Report the message (likely checksum or header).
  - Interrupted: leave L in and switch on. BAK `up` installs the loader `.frm` again (it is the `.frm` in the slot).
  - Boot hangs (no LCD): battery pull. Boot with the half-press held (`LDR_STATE` 1, the card is not touched), then uninstall (U).
  - No usable menu at all: card R + combo.

**F2b. Skip key, no card, USB (no flash).**
- Card: N as in F2.
- (a) Hold the half-press while switching on: `LDR_STATE` = `00 00 00 01`.
- (b) No card: the camera works normally (no probe data possible).
- (c) USB to the Mac with card N in (USB/PC boot = section 1 only): the card mounts on the Mac and its files are unchanged.
- Fail: as F2.

**F3. M10b: the loader starts the CFW (card only).**
- Card: N with the debug zip contents (`gr2_cfw-v<ver>-debug.zip`), but with its `Startup.ttl` **replaced by the probe `Startup.ttl`**. The CFW's `Startup.ttl` must not be on the card (double load, §3). The probe only dumps data and is safe with the armed loader.
- Pass:
  - The CFW is up as usual (CFW Menu, live-view features).
  - `cfw_log.txt` holds `main: started by the flashed loader (stage 2 read at, now) systime …`.
  - `LDR_STATE.BIN` = `00 00 00 06`.
  - `loader_timing.py` shows the hook(1) preload time.
- Then the half-press boot: `LDR_STATE` 1, stock camera, no CFW.
- Optional: put the CFW `Startup.ttl` back once. Expected: log `boot: flashed loader active, Startup.ttl must not load the CFW …`, no second CFW, no crash. Remove it again.
- Fail:
  - Crash (camera powers off via BAK): next boot with the half-press, fix the card.
  - Hang: battery pull + half-press.
  - Never leave card R inserted for these tests: a crash would reflash stock.

**F4. M10c (not built).** Patch-on-load and the look before the first live-view frame. Pass: the recipe look shows from the first LV frame. Recovery as F3.

**U. Uninstall (any time after F2).**
- Card: S. Boot with the half-press if the CFW misbehaves.
- Do: menu update.
- Pass: the probe on card N reads `LDR_STATE` = `FF FF FF FF` again.
- Fail: card R + combo.

### 6.3 Summary

| Step | Writes flash | Card | Pass | Recovery |
|---|---|---|---|---|
| F0 | no | N + probe | `LDR_STATE` FFFFFFFF, profile decodes | — |
| F1a | stock bytes | S | update offered, boots 3.00 | refused: stop; interrupted: S in, power on |
| F1b | stock bytes | R | minutes of `up`, then stock boot | battery pull, normal boot |
| F2 | loader | L, then N + probe | `LDR_STATE` 3, stock behaviour | half-press, U; R + combo |
| F2b | no | N + probe | 1 / normal / USB OK | as F2 |
| F3 | no | N + debug zip − CFW TTL + probe | CFW up, log line, `LDR_STATE` 6 | half-press |
| F4 | no | N | look on first frame | half-press |
| U | stock bytes | S | `LDR_STATE` FFFFFFFF | R + combo |

### 6.4 M11 P-steps (release firmware; after F3; not buildable yet)

`scripts/build_cfw_frm.py` does not exist yet, and R1a–c (A: drive directory, larger RES in `up`, `LoadFile("A:/…")`) are open (milestone M11). F1a passing settles R1d. The planned steps follow milestone M11. Each needs the §6.1 preconditions and cards R/S:
- **P1:** flash `rg2_v003_cfw.frm` from a card holding only it (renamed `rg2_v003.frm`). Pass: CFW up without a card, INFO shows the release version, release log empty. Recovery: half-press boot, then U.
- **P2:** skip key, no card, USB, playback-first boot. Pass: stock where expected, no card access in USB mode.
- **P3:** uninstall with card S. Pass: stock APP + RES back (byte-identical if a BAK/shell checksum is available).

## 7. Risks and discrepancies (ranked 2026-11-01; open until the step that covers them)

1. **Same-version install: never observed on hardware.**
   - Static evidence now favours acceptance: `bcdUpdateCmpVer` = 0 in the stock modules, so `up`'s header check does no version compare, and the menu flow compares nothing [R §2].
   - The section-10 dialog code was not read [I]. F1a decides.
2. **Hardware acceptance of a recomputed nUpdateCsum: never observed.**
   - The formula is byte-exact on all 4 stock modules and on the patched APP [R]. F2 is the first flash using it.
   - A rejection happens before "Destroy APP." [V].
3. **The early H: mount is untested on hardware [I].**
   - It is the same function DCFMount uses, about 2 s earlier, before the DCF/card-detect state exists.
   - −1 → stock boot. A hang → no LCD → battery pull + half-press. Breaking DCFMount's remount → card errors → half-press boot, then U.
4. **F1b cannot tell success from refusal except by duration.** `rescue/backup.ttl` runs `up` then `forcedown`, and both outcomes end with the camera off.
   - Suggestion (not applied; the loader files are outside this review's scope): add a `foldercreate 'H:/RESCUE_UP_RET'` between `up` and `forcedown`. Its presence would mean `up` returned.
   - Still untested: whether `up` returns or reboots on success, `forcedown`, and exFAT in BAK.
5. **Card-content mistakes.**
   - The debug zip contains the CFW `Startup.ttl`; with the loader armed it must be removed (F3), or it causes a double load (§3).
   - Two `rg2_v*.frm` on one card is an untested `up` case.
   - `dist/` also holds the loader `.frm` under its build name, which must be renamed on the card.
   - The previous plan's F3 pass criterion `LDR_STATE` 6 could not be observed without a TTL. It is now the probe `Startup.ttl` (§6.2 F3).
6. **USB/PC boots** mount H: in the stub before the PC takes the card. The stub only reads, and stage 1 must never write to H: from hook(1). Untested [I] (F2b c).
7. **Skip key and hangs.**
   - RL1 (P6.1, active 0) is read before the Button task exists [V pin decode, R]. The IPL reads P0/P14/P15 the same way even earlier, and RL2's polarity is [H] through the combo.
   - There is no watchdog for hangs [V finding 22 §2.3]: battery pull.
   - The skip key restores a stock boot without touching the card.
8. **A crashing armed stage 1** (cause 1–4) → BAK → power off. A crash with card R inserted → full stock reflash.
9. **Every boot with a card and without the skip key pays the mount + ≤ 32 KiB read**, even without a CFW (`ST_NOFILE`). F2 measures it.
10. **`LoadFile` truncates silently at 32 KiB** [V].
    - Today's stage 1 is 32,424 B, but only because the hook veneers sit at `0xA001E07C`/`0xA001EEA0`. Code + data is 1,860 B + 180 B `.bss` [R].
    - `package_sd.py` and `gr2_boot.ld` assert the limits. Release and debug `gr2_cfw.bin` are byte-identical.
11. The stub has 1 spare byte. A change needs the 316-byte tail after section 0 (section-0 size `0xA46C4` → `0xA4800`, IPL-checked). Not planned.
12. IPL and BAK are never touched. They are the only recovery.

No loader-source defect was found in this review.
