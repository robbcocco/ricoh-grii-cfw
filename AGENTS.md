# Ricoh GR II Custom Firmware (gr2_cfw) — Agent Operating Manual

**Repository:** `ricoh-grii-cfw`  
**Purpose:** Official Custom Firmware Codebase for Ricoh GR II  
**Architecture:** Bare-metal ARMv7-A Big-Endian (`ARM:BE:32`), Thumb mode  
**Execution Base:** `0xA0700000` (High DRAM pool)

---

## 1. Build System & Toolchain

- **Compiler:** `arm-none-eabi-gcc` (tested with GCC 16.2.0 on darwin arm64).
- **Compilation Command:**
  ```sh
  make -C ricoh-grii-cfw
  ```
- **Disassembly Inspection:**
  ```sh
  make -C ricoh-grii-cfw disasm
  ```
- **Build Artifacts:**
  - `bin/gr2_cfw.elf`: Unstripped ELF with debug symbols.
  - `bin/gr2_cfw.bin`: Raw binary payload ready for camera execution.
  - `bin/gr2_cfw.asm`: Full Thumb disassembly with annotated address maps.

---

## 2. Memory Architecture & Calling Conventions

- **Memory Layout:**
  - `0xA0700000`: Linker entry `_start` (`src/entry.S`).
  - `.text` / `.rodata`: Code and read-only data.
  - `.data`: Initialized globals.
  - `.bss`: Zero-initialized on entry by `_start`.
- **Calling the RTOS from CFW:**
  - CFW executes in ARM Supervisor Mode (SVC) with full privileges.
  - To dispatch shell commands:
    ```c
    GR2_FN_DISPATCH_CMD("efcset efc_posiFilm 65617 warm");
    ```
  - Address: `0xA002A274` (`int (*)(const char *)`).
- **Camera Execution Protocol:**
  - Load into DRAM: `fload H:/cfw/gr2_cfw.bin A0700000 <size_hex>`
  - Execute: `ex A0700001` (Thumb mode: bit 0 set).

---

## 3. Source Organization

- `include/gr2_hw.h`: Hardware register definitions, screen dimensions (640x480), RTOS syscall addresses.
- `include/films.h`: Film simulation profile structures and matrix math.
- `include/gr2_ui.h`: UI state machine, menu layout, and event handlers.
- `src/entry.S`: Assembly startup, register preservation (`push/pop`), BSS clearing.
- `src/main.c`: `cfw_main()` initialization.
- `src/films.c`: Color matrix serializer (`mycmtx`) and effect parameter injector (`efcset`).
- `src/ui.c`: OSD graphics renderer (`gdevice`, `gpen`, `grect`, `gprint`).
- `src/hooks.c`: In-DRAM menu label patcher and modern JPEG settings.

---

## 4. Operational Invariants for Future Agents

1. **NO LIBC / FREESTANDING ONLY**:
   - The CFW binary is built with `-nostdlib -ffreestanding`.
   - Do **NOT** include standard C library headers (`<stdio.h>`, `<stdlib.h>`) that pull in glibc/newlib syscall stubs.
   - Use `<stdint.h>`, `<stddef.h>`, and `<stdbool.h>` only.
2. **MAINTAIN BIG-ENDIAN ALIGNMENT**:
   - The CPU is Big-Endian. Strings intended for on-screen menus must be formatted as UTF-16 Big-Endian (`str_to_utf16_be`).
3. **DO NOT OVERFLOW HARDCODED ARRAYS**:
- Replacement menu labels must not exceed original buffer allocations in `b01firm8` DRAM.
4. **CLEAN BOOT & TOGGLE-DRIVEN ARCHITECTURE (HARD INVARIANT)**:
   - All custom features, tweaks, and film simulations **MUST** start in an `[OFF]` / Stock state by default.
   - Never apply settings overrides or patch on-screen text blindly at boot time.
   - All modifications must be controlled exclusively via user toggles inside the Custom OS UI.
   - When the In-Camera Clarity Slider is toggled `[ON]`, it patches Row 4 of the stock Fn menu from `"Vignetting"` to `"Clarity"`; when toggled `[OFF]`, it restores `"Vignetting"`.
   - Film simulations are only applied to slots when explicitly chosen by the user in the UI.
