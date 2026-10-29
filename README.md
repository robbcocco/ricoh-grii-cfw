# Ricoh GR II Custom Firmware (gr2_cfw)

A native, in-memory custom firmware and resident OS extension for the Ricoh GR II (firmware `3.00`).  
Built with `arm-none-eabi-gcc` in bare-metal C and Assembly. Loaded at boot from SD card into high DRAM (`0xA0700000`) with **0% brick risk**.

---

## Design Principles

1. **Clean Neutral Boot by Default**:
   - The camera boots into a **100% stock, factory-original state**.
   - No film profiles, tone curves, or settings overrides are applied blindly at boot.
   - All custom features and film looks are controlled exclusively through the **Custom OS UI**.
2. **Feature Toggles (Off by Default)**:
   - Every custom feature starts in the `[OFF]` state.
   - Enabling a feature in our UI activates its parameter hook.
   - When the **In-Camera Clarity Slider** is toggled `[ON]`, it patches Row 4 of the stock Fn menu from `"Vignetting"` to `"Clarity"`. When toggled `[OFF]`, it restores `"Vignetting"`.
3. **Selective Film Slot Assignment**:
   - The user selects a film look from the 8-film catalog and assigns it to a target slot:
     `Positive Film`, `Bleach Bypass`, `Retro`, `Cross Process`, `MY1`, `MY2`, or `MY3`.
   - Only the chosen slot is overridden, and only that specific slot's on-screen menu label is updated.
4. **Native Menu Trigger ("Effect Button Hold Setting")**:
   - Adds an option directly into the camera's stock menu:
     `[Key Custom Options]` $\rightarrow$ `[Effect Button Hold Setting]` $\rightarrow$ **`Open Custom OS`**.
   - When selected, pressing and holding the **Effect button** on the camera body opens the Custom OS UI.
   - If set to `Wi-Fi` or `Off`, the camera retains its stock behavior.

---
## Custom OS UI Architecture

The resident overlay provides two primary interactive management tabs:

### Tab 1: Film Simulation Selector
- **Film Catalog**:
  - Kodak Portra 400 (Spektrafilm fit)
  - Fujifilm Classic Negative
  - Kodak Gold 200
  - Kodak Ektar 100
  - Fujifilm Pro 400H
  - Kodak Vision3 250D
  - Kodak Tri-X 400 (High-Contrast B&W)
  - CineStill 800T
- **Target Slot Assignment**:
  - `Positive Film`, `Bleach Bypass`, `Retro`, `Cross Process`, `MY1`, `MY2`, `MY3`
- **Action**:
  - `[Apply Film to Slot]` applies the $3 \times 3$ look matrix and tone curve, and updates the on-screen menu name for that specific slot.

### Tab 2: Custom Settings & Feature Toggles
All toggles start `[OFF]` by default:
- **`In-Camera Clarity Slider`** (`[OFF]` / `[ON]`):
  When enabled, patches Row 4 of the Fn customization dialog from `"Vignetting"` to `"Clarity"` (`Off`, `Weak`, `Strong`).
- **`Smart S-Curve Contrast`** (`[OFF]` / `[ON]`):
  When enabled, applies $D=6$ highlight protection curve to the Contrast slider.
- **`Auto-ISO 1/125s Floor`** (`[OFF]` / `[ON]`):
  When enabled, sets minimum shutter speed to 1/125s (`svautoset 100 6400 1.125`).
- **`High-ISO Detail NR`** (`[OFF]` / `[ON]`):
  When enabled, customizes noise reduction thresholds (`nrset wms 100 800 1600`) to preserve grain.
- **`65:24 XPan Frame Lines`** (`[OFF]` / `[ON]`):
  When enabled, draws cinematic widescreen guidelines ($y=122$ and $y=358$) over the display.

---

## Building from Source

### Prerequisites
- `arm-none-eabi-gcc` (bare-metal ARM toolchain, version 10+)
- `make`

### Build Command
```sh
cd ricoh-grii-cfw
make
```

Outputs:
- `bin/gr2_cfw.elf`: Unstripped ELF binary.
- `bin/gr2_cfw.bin`: Raw Big-Endian binary payload (5,112 bytes = `0x13F8` hex).
- `bin/gr2_cfw.asm`: Annotated Thumb disassembly.

---

## SD Card Installation & Deployment

1. Format an SD card as **FAT32** (MBR, $\le 32\text{ GB}$).
2. Copy `bin/gr2_cfw.bin` to `H:/cfw/gr2_cfw.bin`.
3. Place `Startup.ttl` on the SD card root:
   ```ttl
   foldercreate 'H:/GR2_CFW_BOOT'
   sprintf "telnetd start"
   sendln inputstr

   ; Stream CFW binary into DRAM base 0xA0700000 (5,112 bytes = 0x13F8 hex)
   sprintf "fload H:/cfw/gr2_cfw.bin A0700000 13F8"
   sendln inputstr

   ; Execute CFW entrypoint (Thumb mode: 0xA0700001)
   sprintf "ex A0700001"
   sendln inputstr

   foldercreate 'H:/GR2_CFW_READY'
   end
   ```
4. Insert into the camera and power on normally. The camera starts in a completely clean factory state, with the resident Custom OS loaded and ready.
