# Changelog

## v1.1.0

- Renamed: gr2_cfw is now GR II TTY ("Gritty", TTY for short). Repository
  [github.com/robbcocco/gritty](https://github.com/robbcocco/gritty), site
  [robbcocco.github.io/gritty](https://robbcocco.github.io/gritty/).
- The card folder is now `tty/` instead of `cfw/`, with no fallback to `cfw/`. Migration: move `cfw/recipes/` to
  `tty/recipes/` (your own recipes; the zip's `tty/` folder brings the shipped ones), `cfw/export/` to
  `tty/export/` if you want to keep your exports, `cfw/config.txt` to `tty/config.txt` (optional: the settings
  themselves are kept in the camera's internal memory), then delete `cfw/`. The log is now `tty/tty_log.txt`.
- Debug card only: `cfw/gr2_cfw.bin` stays where it is (the flashed debug loader opens that path); stage 2 is now
  `tty/tty_main.bin`, the fonts are in `tty/fonts/`.
- Zips: `gritty-v1.1.0.zip` and `gritty-v1.1.0-debug.zip`.
- The Effect button choice now reads `TTY Menu`; the stored value is unchanged, so a camera set to CFW Menu keeps it.
- Console and shell: shell command `tty` (`tty status`), console prompt `tty> `; tasks `TTY`, `TTYldr`, `TTYcon`.
- Updating from v1.0.x: flash `rg2_v003.frm` as usual; internal settings and MY-slot looks are kept.

## v1.0.1

- Settings export: DEV → *Export settings to card* (or the console command `export`) writes MY1–MY3, the My
  Settings Box entries, the live settings, Setting1/Setting2 and every changed effect as recipe files in
  `cfw/export/`, ready to copy to `cfw/recipes/`.
- New recipe keys: `iso`, `auto_hi_max`, `auto_hi_min`, `auto_hi_shutter`, `nr=custom` with `nr_weak`,
  `nr_medium`, `nr_strong`, `base=shift_crop`, and the Hi-Contrast B&W contrast `max`, `-1`, `-2`.
- WB compensation `wb_ab` / `wb_gm` now spans −8 … +8, the stock grid.
- Up to 48 recipe files (was 24).
- New recipes: Cinema Green, Cinema Yellow, Cinema Warm and Vivid Slide (spektrafilm fits, CC BY-SA 4.0);
  Rangefinder, Mono Hard, Mono Soft Sepia and Mono Pushed Street (own designs, CC BY 4.0).

## v1.0.0

- First release: the CFW built into Ricoh's firmware 3.00 (`rg2_v003.frm`), film recipes fitted to spektrafilm,
  CFW menu, safe boot and the rescue card script.
