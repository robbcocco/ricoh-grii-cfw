# Flashed loader and release firmware: design and checks

Two firmware builds, both stock 3.00 plus a 132-byte stub in the APP image that hands each section load to the CFW's stage 1. The **release firmware** (`rom_stub.S`, §5) loads stage 1 from the camera's internal `A:` drive, where the CFW files are flashed with it; it never loads code from the card. The **debug loader** (`ldr_stub.S`, §1–§4) loads stage 1 from the card's `cfw/` folder, so development builds need no reflash. The tools here never write to an SD card. Every hardware test step (F0–F10, P1–P3) passed; the test record is kept outside this repository.

| File | Purpose |
|---|---|
| `ldr_stub.S`, `ldr_stub.ld` | Debug-loader stub source and the two patch placements (also used for `rom_stub.S`) |
| `rom_stub.S` | Release-firmware stub (M11): stage 1 from `A:/cfw/gr2_cfw.bin`, no mount (§5) |
| `../../scripts/build_cfw_frm.py`, `../../scripts/emu_release.py`, `../../scripts/rom_files.py` | M11 release firmware `dist/rg2_v003_cfw.frm` + manifest, its independent check (byte checks + stock `up`/A: code on a simulated NAND), the A: file set (`make rom`, §5) |
| `../../scripts/build_loader_frm.py` | Builds `dist/rg2_v003_loader.frm` and `dist/rg2_v003_loader.diff.txt` from the stock `fw/rg2_v003.frm` (`make fw`). Run by `make loader` / `make test` and CI; ships only in the developer (debug) zip. |
| `../../scripts/emu_loader.py` | Independent byte check of that `.frm` plus a Unicorn run of the patched hook path |
| `probe/Startup.ttl` + `../../scripts/loader_timing.py` | Data-only probe: boot timeline (the APP's own section-load profile) and the loader status word |
| `rescue/backup.ttl` | Rescue-card script for BAK: runs `up`, then `forcedown` |

Tags: [V] = read in code/data or observed in emulation; [H] = observed on hardware; [I] = inference. **[R 2026-11-01]** = re-verified on 2026-11-01 with an independent script (own capstone disassembly, own checksum loop, own diff) against the APP image `fw/b01firm8.bin` and the stock `rg2_v003.frm`.

## 1. What gets patched

Only APP (`b01firm8`, type 1) changes. RES (`b01firm6`) is byte-identical. The container holds no IPL or BAK module, and the builder fails if one appears. Image offset = RAM − `0xA0020200`.

| Image offset | RAM | Bytes | Stock → patched |
|---|---|---|---|
| `0x00016C` | header | 4 | nUpdateCsum `826A5E33` → `8268E3E9` (stub v2; recomputed by `gr2frm.repack`) |
| `0x002C5C` | `0xA0022E5C` | 4 | `F01C FE51` `bl 0xA003FB02` → `F08C FF48` `bl 0xA00AFCF0` |
| `0x08FAF0` | `0xA00AFCF0` | 132 | all `0xFF` → stub: code, literals, the path, `0xFF` fill, status word `ldr_state` = 0 at `0xA00AFD70` |

136 bytes differ in the `.frm`, all inside the APP payload. `gr2frm.py verify` passes: round trip, header sizes and both checksums. Output SHA-256 (stub v2, 2026-11-02): `1914ac9069cb5e9803d348bea999258884d0d8199f7ff2e257161ec44c1f91f1`. Earlier builds: v1 `f8c4f9b6…a7d5` (flashed F2, 2026-11-01; reads the card before the lens starts, which makes the lens bounce, finding 33) and `497e5c98…b514` (mounted the card before section 1 was loaded, finding 28; must never be flashed). The full byte listing and disassembly are in `dist/rg2_v003_loader.diff.txt`.

**[R 2026-11-01]** Everything in this table, re-checked (for the first stub; the fixed stub changes only the stub bytes and the checksum, both re-checked by `make loader`):
- `fw/b01firm8.bin` (`make fw`) is byte-identical to the APP image unpacked from the stock `.frm` (`.frm` SHA-256 `3643850f…8331`); RES is identical in the loader `.frm`.
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
  - **Lens start [H F4, finding 33].** JobSystem step 4 starts the lens between the section-4 and section-2 loads (stock ~150 ms). A card session before it (v1: ~190 ms right after section 1) delays the lens start, and the lens then extends, retracts and extends again; unmounting the card afterwards does not help. Stub v2 therefore touches the card only after `Section_Load(3)`, the third load in both boot orders and still before DCFMount (section 8/9).
- **What is not ready at the first hook:**
  - H: is not mounted. File_MountMedia is called only by DCFMount, its caller `FUN_a029e5b8` (both section 8), the shell, card hotplug and `up` [V callers]. So the stub mounts H: itself, which is BAK's pattern [I].
  - DCFMount later calls File_MountMedia again unconditionally [V], which unmounts and remounts [I].
  - Because the DCF code lives in section 8, nothing can remount H: while section 8 is not loaded [V]. That is true at the first hook (section 3) in both boot orders.
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

Every call first runs the stock `Section_Load(n, 0)` (so section 1 is resident before the card is touched, finding 28). Then, on the first call with `n == 3` while `ldr_state == 0` (stub v2; v1 decided on the first call, n = 1), in this order:

1. If `Port_Get(0x27)` is 0 (shutter half-press held): `ST_SKIP` = 1.
2. If `File_MountMedia("H:/cfw/gr2_cfw.bin")` returns −1: `ST_NOMOUNT` = 2.
3. If `LoadFile(path, 0xA0017000, 0x8000)` returns ≤ 0: `ST_NOFILE` = 3.
4. If the word at `0xA0017004` is not `0x47524C31` ("GRL1", loader ABI v1): `ST_NOABI` = 4. Stage 1 carries it since M10b (2026-11-01); an older stage 1 is loaded but never run.
5. Otherwise `ST_ARMED` = 5.

If `ldr_state` is 5, it then calls `*(0xA0017008)(n, &ldr_state)` (Thumb); the first call is n = 3. The stock result is not kept: the caller overwrites `r0` right after (`0xA0022E60 movs r0,r7`), and `r1` is already 0 at the hook site (`0xA0022E58`) [V]; dropping those three instructions paid for the `n == 3` test.
- The skip key is read before anything touches the card, so it also bypasses a mount or file-load that hangs. It is now read at section 3 (~0.3 s after power-on), so it has to be held until the LCD comes on.
- A USB-charge boot loads only section 1, so the stub never decides and the card is never touched.
- `emu_loader.py` fails any card access before `Section_Load(3)`; the v1 stub fails 8 of 9 cases that way, the first stub (card before section 1) failed the same way.
- `ldr_state` lives in section 0, which the IPL loads from NAND before every APP start, so it is 0 again on every boot [V cold boot; I warm `reboot app`, which also re-reads the IPL and its window tail as `0xFF`].

The 132 bytes left no room for a `DriveMounted` pre-check or a range check of the entry pointer. On a cold boot nothing has mounted H: before section 3 (§2). The magic word is the guard that an old stage 1 is never called.

### Stage-1 loader ABI v1

- `gr2_cfw.bin` +0: `b` over the header (the `ex 0xA0017001` entry is unchanged). +4: `0x47524C31`. +8: Thumb address (odd) of `cfw_ldr_hook(uint32_t n, volatile uint32_t *ldr_state)`, inside the window (`src/entry.S`, `src/boot.c`). +12: image length (= file size). +16: CRC-32 (zlib) over `[0, 16)` + `[20, length)`. `scripts/stage1_crc.py` writes +12/+16 after `objcopy` for every variant; `package_sd.py` (and `rom_files.py` for the release firmware) checks all five.
- Self-check: stage 1 verifies +12/+16 over the window at its first hook, before `.bss`/`.data` are touched; on a mismatch (e.g. a file truncated by `LoadFile` at `0x8000`) it logs ERROR into its RAM hold buffer, writes `*ldr_state = 6` and runs nothing else.
- The hook runs in the boot task (root init task, stack 0x4000), right after section n was loaded and before anything executed it. Every millisecond it spends delays boot.
  - It must return.
  - It may write `*ldr_state = 6` to stop further calls.
  - On the first call it zeroes stage 1's `.bss` (as `_start` does) and holds its log lines in RAM.
- **Policy:**
  - **First hook (section 3 with stub v2, section 1 with v1):** self-check and window check, then the M12 safe-boot decision (`include/safeboot.h`; RTC OPSR bits 2–3: a disarmed or "stock once" state writes `*ldr_state = 6` and runs nothing else; PLAY power-on re-arms), then read `gr2_main.bin` into the heap (debug/release: `H:/cfw/gr2_main.bin`; `rom` variant: `A:/cfw/gr2_main.bin` with two `LoadFile` calls, the 32-byte header and then header + image + relocations, stage 2 at block + 32), relocate it and call stage 2's `cfw_hook` (second header entry). The card stays mounted: unmounting it after the reads did not help the lens and made the boot task's own mount after section 9 ~0.2 s slower. Stage 1 then calls `cfw_hook(n, boot)` after every later section load.
  - **Nothing is written to H: before step 13.** The boot task remounts the card right after hook(9) (finding 32), and a USB-charge boot loads only section 1. Both stages hold their log lines in RAM (`cfw_log_hold`; stage 2: one 2 KiB buffer for the boot task, one for the early CFW task) and write them once from the `CFWldr` task. Every file opened in a hook is closed before it returns; the early CFW task only reads the card (fonts) until its late phase.
  - **Stage 2, first hook:** the config from `D:/CFW` (card copy if D: has none) and the MY1–MY3 looks from the D: look cache (a recipe file on the card for a slot the cache lacks).
  - **Stage 2, hook(10) (patch-on-load):** menus (records, language/attribute/font tables, string bounds), Effect press/hold BLs, the APP message-handler slot and (debug) the power-off BL are installed into section 10 before it first runs. No stock code rewrites any of them later, and no later section load covers them (finding 32). Then the Snap labels and `Task_Create` of the CFW task (finding 35 §3): it waits for JobSystem step 10, then runs its 20 ms loop (Effect requests, input, INFO, VIEW overlays), reads the fonts at the hook after hook(9), and takes over settings, looks and saves only after `cfw_main`.
  - **Stage 2, hook(5) (IQ-ready hook):** retarget the BL at `0xA01B584A` (section 5, `FUN_a01b5828`, stock `bl 0xA01C2A80`) to the stage-1 veneer `0xA001DA80` (same low 12 bits, one halfword). CapInit(5) calls it once right after IQ.bin, AF_F and AE_F (with the C: overlay) are loaded and before `notify(5)` lets live view start; the hook applies the CFW settings (`settings_early`, if the mode props are final) and the active slot's look, then calls the stock function (findings 32 §3.2, 35).
  - **Stage 2, every hook (fallback):** the same look once `*0xA012A0EC == 1` (set after JobSystem step 2 has written the mode props and loaded the active MY slot) and the ISP holds IQ.bin, if the IQ-ready call missed it. F5 [H]: applied at hook(10) only, live view was already running (first frame before ~1.1 s), the colours came a moment late and the write held the boot task 0.1–0.4 s (hook 10 → 11). A debug build logs a per-hook timeline (n = 0: the IQ-ready call; time, props flag, steps 2/5, lens script, shown LV frame).
  - **hook(13):** `Task_Create` the `CFWldr` task (stack 0x4000) and set `*ldr_state = 6`. Section 13 is last in both boot orders.
  - **`CFWldr`:** waits until JobSystem_Boot has finished step 13 (`Ctrl()+0x54+4·13`, 100 ms polls, 60 s timeout), writes the held log lines, then calls `cfw_main`: timeline, shell command, anything the hooks did not install or start; it then opens the CFW task's late phase. The task then sleeps forever.
  - A failure at the first hook disarms nothing early. `CFWldr` logs `stage 2 not loaded at the first hook` once the card belongs to the camera, and does not start the CFW.

## 4. Emulation and byte check (run 2026-10-31, case H added 2026-11-01; all re-run 2026-11-01 [R])

`make loader` runs `scripts/build_loader_frm.py` and then `python3 scripts/emu_loader.py`, which does two things:

- **Byte check.** RES is identical. APP differs only in `0x16C–0x16F`, `0x2C5C–0x2C5F` and `0x8FAF0–0x8FB73`, and every one of these ranges is patched. Every changed container byte maps onto those APP bytes. nUpdateCsum equals the plain formula sum of finding 15 §7.1, and `gr2frm.py verify` exits 0.
- **Unicorn run.** ARM946, BE-32, patched APP mapped at `0xA0020200`. The real hook site `0xA0022E58..0xA0022E62` runs once per section load, so the flashed stub runs as is, followed by the caller's own `movs r0,r7`.
  - Any execution outside the hook site and the stub, any write other than the stack and `ldr_state`, a changed `r4`–`r7`/SP, or a card access before `Section_Load(3)` fails the run.
  - Run against the stock `.frm`, every scenario fails, which shows the check discriminates.

| Case | `ldr_state` | Mount / LoadFile | Stock loads | Stage-1 hook calls | Window |
|---|---|---|---|---|---|
| A file with loader ABI | 5 | 1 / 1 | all 12, in order | after each load from n = 3 (3 5 10 11 8 9 6 12 13) | file |
| B file absent | 3 | 1 / 1 | all 12 | none | 0xFF |
| C half-press held | 1 | 0 / 0 | all 12 | none | 0xFF |
| D no card | 2 | 1 / 0 | all 12 | none | 0xFF |
| E stage 1 without loader ABI (pre-M10b bin, magic cleared) | 4 | 1 / 1 | all 12 | none | file (same as `fload`) |
| F playback order, ABI | 5 | 1 / 1 | all 12 | after each load from n = 3 (3 8 9 10 11 6 4 5 12 13) | file |
| G stage 1 disarms at n = 5 | 6 | 1 / 1 | all 12 | 3 5 | file |
| I USB-charge boot (section 1 only) | 0 | 0 / 0 (no Port_Get) | 1 | none | 0xFF |
| H current `bin/release/gr2_cfw.bin` (ABI v1) | 5 | 1 / 1 | all 12, in order | from n = 3, at its real +8 entry | file |

The stage-1 side of the ABI runs for real in `make emu` (scenario `loader`, `tests/emu/run.py`):
- full boot: preload and stage 2's first hook at section 3, the IQ-ready BL patched at hook(5) and the look shown by the patched stock BL itself (IQ hidden until then), `CFWldr` + disarm at hook(13), `cfw_main` after the step-13 flag, Effect BL retargeted;
- USB boot (section 1 only): no CFW code runs;
- `gr2_main.bin` missing: no card write at the first hook, the error is logged by `CFWldr`;
- `Startup.ttl` while armed: refused;
- no step-13 flag: 60 s timeout, the CFW is not started.

The models return what the decompiles show: Port_Get 0/1, MountMedia −1 on failure, LoadFile bytes or −1. Real mount, card and timing behaviour can only be checked on hardware.

## 5. Release firmware (M11)

`make rom` builds `dist/rg2_v003_cfw.frm` with `scripts/build_cfw_frm.py` and checks it with `scripts/emu_release.py` (also part of `make test`): stock 3.00 + `rom_stub.S` in APP + the CFW files appended to the A: drive in RES (finding 29 §8). The CFW binaries come from `bin/rom/` (variant `rom`: release logging plus `CFW_ROM=1`). `dist/rg2_v003_cfw.manifest.txt` records the output SHA-256, the container layout, the header fields, every A: entry (payload offset, size, SHA-256), the APP diff runs and the stub listing. `make dist` ships it as `rg2_v003.frm` in the official zip. The build is deterministic for a given `bin/rom/`.

### 5.1 What changes

| Where | Change |
|---|---|
| APP `b01firm8` | Exactly as §1 (hook BL `0xA0022E5C`, the 132-byte range `0xA00AFCF0`, `ldr_state` 0, nUpdateCsum recomputed), with `rom_stub.S` in the range; length and sizes stock |
| RES header | `+0x164` nUpdateTotalSize, `+0x168` nUpdateFileSize = `0x200 + +0x164`, `+0x16C` nUpdateCsum; `+0x170..+0x17C` stay 1 (`bCheckRomSize` bounds the write, finding 29 §5); every other byte stock |
| RES payload `0x0` | the u32 directory pointer, `0x785E00` → the new directory |
| RES payload `0x4–0x787C7F` | stock bytes, including the old directory (unused) |
| RES payload `0x787C80–0x787DFF` | `0x00` |
| RES payload from `0x787E00` | the CFW files, each 0x200-aligned, `0x00` padding |
| new directory | the 121 stock entries verbatim, then one 64-byte entry per CFW file (full path, offset, size, FAT date/time 2026-01-01 00:00 ×3, attribute `0x01`), then a 64-byte zero terminator ending exactly at nUpdateTotalSize. Stock entries come first, so stock lookups and stock `FindFirst` indices are unchanged |
| container | RES entry length; the nested `rg2_v001up` container (byte-identical apart from APP) moves to the next sector; total length. `gr2frm.py repack` lays the containers out the way Ricoh's are (sector 4, then the next sector, `0xFF` padding) |

**A: file set** (`scripts/rom_files.py`, read by the builder, the check and `tests/emu`): `A:/cfw/gr2_cfw.bin` (first, so the stub's lookup scans the fewest directory sectors), `A:/cfw/gr2_main.bin`, `A:/cfw/fonts/*.fnt` (every `fonts/*.fnt`). No recipe files: the films ship on the card; `src/recipes.c` still scans `A:/cfw/recipes/` first (silently empty), so a later build can flash recipes without code changes. A: names are full paths of at most 31 characters, case-sensitive, never empty (size 0 ends the directory), and none collides with a stock name or prefix (finding 29 §2). Stage 1 must fit `0x8000` and carry valid length/CRC fields (`scripts/stage1_crc.py`).

**Sizes (v1.0.0 build):** RES payload `0x787C80` → `0x7A3C40` (7 CFW files), data blocks from `0x62`; cap `0x7E0000` (blocks `0x62–0xA0`, finding 29 §5) leaves 246,720 B for more A: files. APP unchanged in size.

### 5.2 Release stub (`rom_stub.S`)

Same hook site, free range, status word and `ST_*` values as stub v2 (§3), decided on the first call with n == 3:
1. `Port_Get(0x27) == 0` (half-press held): `ST_SKIP` = 1.
2. `LoadFile("A:/cfw/gr2_cfw.bin", 0xA0017000, 0x8000) <= 0`: `ST_NOFILE` = 3.
3. word `+4` ≠ "GRL1": `ST_NOABI` = 4.
4. Otherwise `ST_ARMED` = 5, and stage 1's hook `*(0xA0017008)(n, &ldr_state)` runs after this and every later section load.

No `File_MountMedia` and no `ST_NOMOUNT` (2): A: is the RES partition read raw from NAND with no mount, cache or content checksum, and it works in stage 0 (finding 29 §1, finding 30). The stub never touches the card, so boots without a card, over USB-charge (section 1 only: never decides) and in playback order load the CFW the same way. 17 bytes of the range are spare. `LoadFile` truncates silently, so stage 1 checks its own length (`+12`) and CRC-32 (`+16`) over the window at its first hook and disarms (`*ldr_state = 6`, nothing runs) on a mismatch. The emulated A: lookup of stage 1 takes 40 NAND read calls, 100 sectors.

### 5.3 Check (`scripts/emu_release.py`, run 2026-11-02, ALL PASSED)

- **Bytes, independent of the builder** (own directory parser and plain checksum loop): RES = stock up to `0x787C80` apart from the pointer and `+0x164..+0x16F`; `0x00` gap; the new directory = stock entries verbatim + the `rom_files()` entries + terminator at the payload end; every stock and CFW file byte-identical through it; `+0x168` = entry length = `0x200 + +0x164`; `bCheckRomSize` 1; both nUpdateCsum = the plain sum; APP differs only in the §1 ranges; the nested container only in APP; the outer header only in its three length/sector words; `gr2frm.py verify` passes.
- **Stock code in Unicorn on a simulated NAND** (finding 29 §8.4: the APP's sections 0–14 at their load addresses; only block → sector, sector read/erase/program, the container file stream, allocators, semaphores, task id, logs and ctype are modelled):

| Step | Result |
|---|---|
| `up` search `0xA004003A` for types 3/0/2/1 on a stock NAND (header check, full nUpdateCsum, ROM size) | only RES (`b01firm6`) and APP (`b01firm8`) found; one flipped payload bit is refused |
| Destroy APP `0xA00402B0`, install `0xA0040286(6)`, `(8)` | header block first, then data blocks `0x62–0x9F` (RES) / from `0x21` (APP); the NAND holds both new images |
| Boot reads with the installed APP | stock `LoadFile` of all 121 stock + 7 CFW A: files byte-identical; `FindFirst`/`FindNext` list `A:/cfw/fonts/` as full paths in directory order and `A:/cfw/recipes/` as empty; `fopen`/`fread` of a font in 1000-byte chunks |
| Stub A: armed, shooting order | `ldr_state` 5, one `LoadFile` at section 3, stage 1's real `+8` entry called after 3 5 10 11 8 9 6 12 13, window = the file |
| Stub F: playback order | 5, hooks 3 8 9 10 11 6 4 5 12 13 |
| Stub G: stage 1 disarms at section 5 | 6, hooks 3 5 |
| Stub C: half-press held | 1, A: not read, window `0xFF` |
| Stub B: A: without CFW files (stock RES) | 3 (`LoadFile` −1), window `0xFF` |
| Stub E: stage 1 without "GRL1" | 4, loaded, never called |
| Stub I: USB-charge boot (section 1 only) | 0, nothing read |
| Stock reinstall from the CFW camera (stock `.frm`, same `up` sequence) | passes; NAND byte-identical to stock except block `0x9F` past the stock RES end (unreachable CFW bytes; `up` never erases blocks after the new end); every stock A: file reads back, `A:/cfw/*` → −1 |

The stub runs as flashed at the real hook site (as in §4), with the stock `LoadFile` and A: code underneath. Any card access (`File_MountMedia`, the FAT layer), a `LoadFile` other than `A:/cfw/gr2_cfw.bin` at section 3, a write into APP code or into the window outside `LoadFile`, or changed `r4`–`r7`/SP fails the run. The debug-loader APP fails it (card access), and so does a stub that decides at section 1. Not covered: real NAND timing and ECC, bad-block remapping, and the stage-1/stage-2 code itself on A: (that runs in `make emu`, `tests/emu/README.md`).

### 5.4 Risks
- A RES that passes `up` but has a broken directory makes stage 0's `LoadFile("A:/fix/Sys_F.bin")` assert → BAK → power off; only the rescue card recovers (finding 29 §7). The builder and the check parse the directory exactly like the camera and read every stock file back through it.
- Every flash needs a working rescue card (stock `.frm` + `rescue/backup.ttl`, MOVIE + full press + POWER); verified on hardware.
- IPL and BAK are never touched; uninstall is the stock `.frm` through the same menu (U), emulated above.
