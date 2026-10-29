# Ricoh GR II Custom Firmware v0.0.1 (Custom Boot Screen)

**Release Date:** 2026-10-29  
**Target:** Ricoh GR II (16.2MP APS-C, Milbeaut RC2015)  
**Safety Classification:** **Tier 2 (Near-Zero Risk — Data Only)**

## What's Changed
- **Replaced Boot Splash Screen (`A:/img/dvf_TDSt.brp`)**:
  Replaces the factory white screen with a custom dark high-contrast boot screen.
- **RTOS Binary (`b01firm8`)**: **100% UNTOUCHED stock factory code**.
  Carries zero machine-code or CPU crash risk.

## Checksums
```
a466ec2ca6232d8c1ce45152fbb3e3b54fc510854b84626419c1be4765cab82f  rg2_cfw_v001_bootlogo.frm
```

## Flashing Instructions
1. Ensure your SD card has the Emergency Rescue Kit (`SD.dat`, `backup.ttl`, `rg2_v003.frm`).
2. Copy `rg2_cfw_v001_bootlogo.frm` to your SD card root and rename it to `rg2_v003.frm` (or official update name).
3. Insert fully charged battery (>= 80%).
4. Power on while holding the **Playback** button or official firmware update button combination.
5. Follow on-screen confirmation prompts. Do NOT turn off the camera while updating.
