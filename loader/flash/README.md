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
| `0x00016C` | header | 4 | nUpdateCsum `826A5E33` → `8268D223` (recomputed by `gr2frm.repack`) |
| `0x002C5C` | `0xA0022E5C` | 4 | `F01C FE51` `bl 0xA003FB02` → `F08C FF48` `bl 0xA00AFCF0` |
| `0x08FAF0` | `0xA00AFCF0` | 132 | all `0xFF` → stub: code, literals, the path, `0xFF` fill, status word `ldr_state` = 0 at `0xA00AFD70` |

136 bytes differ in the `.frm`, all inside the APP payload. `gr2frm.py verify` passes: round trip, header sizes and both checksums. Output SHA-256: `f8c4f9b68aec10fc63bb9e290445294e0f689acb002e9ae4ed7976537058a7d5` (stub fixed 2026-11-01, finding 28; the earlier build `497e5c98…b514` mounted the card before section 1 was loaded and must never be flashed). The full byte listing and disassembly are in `dist/rg2_v003_loader.diff.txt`.

**[R 2026-11-01]** Everything in this table, re-checked (for the first stub; the fixed stub changes only the stub bytes and the checksum, both re-checked by `make loader`):
- `fw/b01firm8.bin` is byte-identical to the APP image unpacked from the stock `.frm` (`.frm` SHA-256 `3643850f…8331`); RES is identical in the loader `.frm`.
- Rebuilding from a clean copy reproduces the SHA-256 above, so the build is deterministic.
- The APP diff is exactly the three ranges in the table.
- Own checksum loop (finding 15 §7.1, load base = header `+0x20` = 0, `nUpdateTotalSize` `0x5D9054`): stock `0x826A5E33` and patched equal to the header word.
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
  - Stage 0 also loads `A:/fix/Sys_F.bin` with `LoadFile`, so file I/O on the **NAND** drives works by then.
  - The SD driver is in section 0 (`0xA00A2058`…), **but every SD sector read calls `FUN_a00f9888` in section 1** (`FUN_a00a219c` → `FUN_a00ae8a2`, finding 28 [V]). The card must not be touched before `Section_Load(1)` has returned; everything else the mount needs exists after stage 0.
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

Every call first runs the stock `Section_Load(n, 0)` (so section 1 is resident before the card is touched, finding 28). Then, on the first call only (`ldr_state == 0`), in this order:

1. If `Port_Get(0x27)` is 0 (shutter half-press held): `ST_SKIP` = 1.
2. If `File_MountMedia("H:/cfw/gr2_cfw.bin")` returns −1: `ST_NOMOUNT` = 2.
3. If `LoadFile(path, 0xA0017000, 0x8000)` returns ≤ 0: `ST_NOFILE` = 3.
4. If the word at `0xA0017004` is not `0x47524C31` ("GRL1", loader ABI v1): `ST_NOABI` = 4. Stage 1 carries it since M10b (2026-11-01); an older stage 1 is loaded but never run.
5. Otherwise `ST_ARMED` = 5.

If `ldr_state` is 5, it then calls `*(0xA0017008)(n, &ldr_state)` (Thumb) and returns the stock result.
- The skip key is read before anything touches the card, so it also bypasses a mount or file-load that hangs.
- `emu_loader.py` fails any card access before `Section_Load(1)`; the first stub (built until 2026-11-01) failed 7 of 8 cases that way.
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
2. Run `make test` (lint, ISA audit, `make emu` with `PASS release/loader` and `PASS debug/loader`, `make loader` with `ALL PASSED`). `dist/rg2_v003_loader.frm` must have SHA-256 `f8c4f9b6…a7d5`.
3. Prepare the cards. `make flash-kit` writes `dist/flash/<step>/` with a `STEP.md` and the exact card-root contents per step (`card/`, or `card-L/` + `card-N/` for F2), all hashes checked. **Format each card in the camera** (MENU → format): for H: the firmware checks the cluster size and FS type against the SD-card specification table and refuses other layouts (BAK has the same table), so a Mac-formatted card can fail to mount (finding 28 [V]). FAT32 for cards ≤ 32 GB. Delete `._*` files after every copy from macOS. After copying, check each `.frm` on the card with `shasum -a 256`. There are four roles:

   | Card | Root contents | Notes |
   |---|---|---|
   | **R** rescue | stock `rg2_v003.frm` (`3643850f…8331`) + `loader/flash/rescue/backup.ttl` as `backup.ttl` (ASCII, CRLF) | Never left in the camera. Each BAK entry (combo, crash, broken APP header) runs `up` from it. |
   | **S** stock flash | only the stock `rg2_v003.frm` | F1a, uninstall |
   | **L** loader flash | only `dist/rg2_v003_loader.frm`, **renamed `rg2_v003.frm`** (`f8c4f9b6…a7d5`) | F2. Exactly one `rg2_v*.frm`, no `backup.ttl`. |
   | **N** normal | per step: `loader/flash/probe/Startup.ttl` as `Startup.ttl`, plus the debug zip for F3 | DCIM etc. may stay |

   - Card R has a `backup.ttl` on purpose: it runs `up` itself, so BAK's automatic cause-6 reflash is kept and the combo (cause 8) becomes a manual reflash path. Confirmed by F1b (2026-11-01); the maintainer's rescue rule describes this card.
   - The combo, BAK running `backup.ttl` and shell commands from it are [H] 2026-10-31 (`bak_test`).
4. Camera prerequisites:
   - `PROPID_DEVEL_MODE = 1` (already set for the CFW), so the probe `Startup.ttl` runs.
   - The battery shows **full**. The menu flow sets a separate status below level 3 [V §2], most likely a refusal [I].
   - A second charged battery.
   - Know the combo (dial MOVIE + shutter **fully** pressed + POWER) and the skip key (shutter **half**-pressed while switching on). Never press fully with the dial on MOVIE when you mean "skip".
5. `SD.dat` is **not** part of the kit. It is loaded only by a manual `reboot fjb` in a BAK shell and is not a recovery image (findings 12, 13, 15). Never use `up ipl`, `up bak`, `fe`, `fbe`.
6. Flash nothing from `dist/` except the file named in the step.

### 6.2 Steps

The exact instructions for each step (what to put on the card, what the screens show, pass/fail, recovery) are in `scripts/flash_kit.py`, written to `dist/flash/<step>/STEP.md` by `make flash-kit`. That file is the single source; the summary below follows it.

- **F0** timing probe — passed 2026-11-01 (§5).
- **F1a** menu update with the stock `.frm` — **passed 2026-11-01 [H]**: "Execute?" with *New Version 3.00* → Yes → "Firmware update complete." → OK switches the camera **off** (no automatic restart, finding 27). Refusal shows "Confirm Firmware Version" without No/Yes (no valid file), "Battery depleted", or a blank *New Version*; nothing is erased in those cases.
- **F1b** rescue card + combo — **passed 2026-11-01 [H]** (`RESCUE_RAN` only: `up` reflashed stock from BAK and did not return): `backup.ttl` marks `RESCUE_RAN`, runs `up`; success powers off with a 3 s RTC wake into the reflashed APP and never returns; a refusal returns and leaves `RESCUE_UP_RET` before `forcedown` (finding 27). No LCD or LED meanwhile. Release the shutter and leave MOVIE after POWER so the self-restart does not re-enter BAK.
- **F2** menu update with the loader `.frm` — **passed 2026-11-01 [H]**: the camera accepted the recomputed checksum and boots stock with the loader; probe `LDR_STATE` = 3 (`ST_NOFILE`: loader ran, H: mounted, no stage 1). Section 1 (stock load + mount + failed open) took 229 ms vs 36 ms in F0; all sections were done at 1839 ms (F0: 1907 ms; section 8's DCFMount took 299 vs 392 ms). Data: the maintainer's hardware data.
- **F2b** — **passed 2026-11-01 [H]**: skip key (`LDR_STATE` 1, section 1 back to 36 ms: the card is not touched), no card (normal), USB (card mounts on the Mac and charges; files unchanged).
- **F3** debug CFW files + probe `Startup.ttl`: CFW up, `main: started by the flashed loader`, `LDR_STATE` 6; then the skip-key boot. Never with card R inserted (a crash would reflash stock). Optional: the CFW `Startup.ttl` back once → logged refusal, no second CFW.
  - Attempt 1 (2026-11-01) **failed**: the loader started the CFW, but boots crashed or came up with garbled labels; the skip key gave a clean stock boot. Cause (finding 31): the `CFWldr` task had a 0x1000 stack, and `cfw_main`'s file I/O needs up to ~9.3 KB in the stock file API, so it overflowed into the heap. Fixed in stage 1 (stack 0x4000, like the Script task); the loader `.frm` is unchanged. The emulator now checks the stock file API's stack need against each task's stack. Data: the maintainer's hardware data.
- **F4** (M10c, not built): patch-on-load and the look before the first live-view frame.
- **U** menu update with the stock `.frm`: `LDR_STATE` `FFFFFFFF` again.

### 6.3 Summary

| Step | Writes flash | Card | Pass | Recovery |
|---|---|---|---|---|
| F0 | no | N + probe | `LDR_STATE` FFFFFFFF, profile decodes | — (**passed 2026-11-01**) |
| F1a | stock bytes | S | "Firmware update complete.", boots | — (**passed 2026-11-01**) |
| F1b | stock bytes | R | auto power-on ~3 s after off; `RESCUE_RAN`, no `RESCUE_UP_RET` | — (**passed 2026-11-01**) |
| F2 | loader | L, then N + probe | "Firmware update complete."; `LDR_STATE` 3, stock behaviour | — (**passed 2026-11-01**) |
| F2b | no | N + probe | 1 / normal / USB OK | — (**passed 2026-11-01**) |
| F3 | no | N + debug zip − CFW TTL + probe | CFW up, log line, `LDR_STATE` 6 | half-press |
| F4 | no | N | look on first frame | half-press |
| U | stock bytes | S | `LDR_STATE` FFFFFFFF | R + combo |

### 6.4 M11 P-steps (release firmware; after F3; not buildable yet)

`scripts/build_cfw_frm.py` does not exist yet. R1a–c are answered statically and in emulation by finding 29 (A: drive append with a new directory, RES ≤ `0x7E0000`, `LoadFile("A:/…")`, plan B APP section 7); F1a settles R1d. The planned steps follow milestone M11. Each needs the §6.1 preconditions and cards R/S:
- **P1:** flash `rg2_v003_cfw.frm` from a card holding only it (renamed `rg2_v003.frm`). Pass: CFW up without a card, INFO shows the release version, release log empty. Recovery: half-press boot, then U.
- **P2:** skip key, no card, USB, playback-first boot. Pass: stock where expected, no card access in USB mode.
- **P3:** uninstall with card S. Pass: stock APP + RES back (byte-identical if a BAK/shell checksum is available).

## 7. Risks and discrepancies (ranked 2026-11-01; open until the step that covers them)

1. **Same-version install: resolved.** F1a passed on hardware 2026-11-01: the menu installs 3.00 over 3.00, as the static evidence predicted (finding 27). M11 can keep the version "3.00".
2. **Recomputed nUpdateCsum: resolved.** F2 installed the patched APP through the menu (2026-11-01 [H]).
3. **The early H: mount (after `Section_Load(1)`): works on hardware** (F2, `LDR_STATE` 3, normal boots). Open: UHS-I cards, other card sizes/formats.
   - Fixed 2026-11-01 before flashing: the first stub mounted before section 1 was loaded; that build must never be flashed (§1).
4. **BAK specifics:** F1b showed on hardware that BAK's `up` from `backup.ttl` reflashes and does not return on success. Still untested: `forcedown` after a refusal, exFAT in BAK (keep FAT32).
5. **Card-content mistakes.**
   - The debug zip contains the CFW `Startup.ttl`; with the loader armed it must be removed (F3), or it causes a double load (§3).
   - BAK/`up` take the **highest** `rg2_vNNN.frm` in the root, fall back to `gr_vNNN.frm`, and open loose `b01firm0…9` files first (finding 27): cards hold exactly one `.frm` and nothing else of that kind.
   - `dist/` holds the loader `.frm` under its build name; `make flash-kit` renames it for card L.
   - Cards must be formatted in the camera: H: mounts only SD-spec layouts (finding 28).
6. **The `notify(1) == 2` boot (section 1 only)** is most likely USB charging, not a PC session (MSC lives in section 8, finding 28 [I]). The stub's mount there only reads. F2b c.
7. **Skip key and hangs.**
   - RL1 (P6.1, active 0) is read before the Button task exists [V pin decode, R]. The IPL reads P0/P14/P15 the same way even earlier, and RL2's polarity is [H] through the combo.
   - There is no watchdog for hangs [V finding 22 §2.3]: battery pull.
   - The skip key restores a stock boot without touching the card.
8. **A crashing armed stage 1** (cause 1–4) → BAK → power off. A crash with card R inserted → full stock reflash.
9. **Every boot with a card and without the skip key pays the mount + ≤ 32 KiB read**, even without a CFW (`ST_NOFILE`): ~190 ms more in section 1 (F2), offset by a faster DCFMount later; the boot finished 70 ms earlier than F0.
10. **`LoadFile` truncates silently at 32 KiB** [V].
    - Today's stage 1 is 32,424 B, but only because the hook veneers sit at `0xA001E07C`/`0xA001EEA0`. Code + data is 1,860 B + 180 B `.bss` [R].
    - `package_sd.py` and `gr2_boot.ld` assert the limits. Release and debug `gr2_cfw.bin` are byte-identical.
11. The stub has 1 spare byte. A change needs the 316-byte tail after section 0 (section-0 size `0xA46C4` → `0xA4800`, IPL-checked). Not planned.
12. IPL and BAK are never touched. They are the only recovery.

One loader-source defect was found and fixed on 2026-11-01 (card access before section 1, finding 28).
