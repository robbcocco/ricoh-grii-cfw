# Changelog

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
