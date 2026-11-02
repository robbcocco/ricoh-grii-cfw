#ifndef BOOT_H
#define BOOT_H
#include <stdint.h>

/* Two-stage payload.
 *   Stage 1 (bin/<v>/gr2_cfw.bin, H:/cfw/gr2_cfw.bin): fixed in the 32 KiB IPL-tail
 *   window 0xA0017000 (src/boot.c, src/entry.S, src/veneers.S). Started by
 *   Startup.ttl; loads stage 2 into the heap, relocates it and calls it.
 *   Stage 2 (bin/<v>/gr2_main.bin, H:/cfw/gr2_main.bin): everything else, linked at
 *   0 and relocated at load (no size limit from the window).
 *
 * gr2_main.bin = cfw_main_hdr_t, image (text + data), nrelocs u32 image offsets
 * of absolute words that need + load address (made by scripts/mkmain.py). */

#define CFW_MAIN_MAGIC   0x47524D31u   /* "GRM1" */
#define CFW_MAIN_PATH    "H:/cfw/gr2_main.bin"

typedef struct {
    uint32_t magic;
    uint32_t image_size;   /* bytes of text + data in the file */
    uint32_t bss_size;     /* zeroed bytes after the image */
    uint32_t entry;        /* offset of cfw_main (Thumb bit set) */
    uint32_t nrelocs;
    uint32_t hook;         /* offset of cfw_hook (Thumb bit set): loader section-load hook (M10c) */
    uint32_t reserved[2];
} cfw_main_hdr_t;

/* Passed to stage 2. The veneers jump through *press_target / *hold_target /
 * *iq_target / *off_target, which stage 2 fills before it retargets the stock BLs. */
typedef struct cfw_boot {
    uint32_t base;                     /* load address of stage 2 */
    uint32_t veneer_press, veneer_hold;
    volatile uint32_t *press_target, *hold_target;
    uint32_t loader;                   /* 0: `ex` from Startup.ttl, 1: flashed debug loader (M10b) */
    uint32_t t_preload;                /* loader: System_GetTime at the first hook, when stage 2 was read */
    uint32_t veneer_iq;                /* loader: IQ-ready callback (BL 0xA01B584A in section 5) */
    volatile uint32_t *iq_target;
    uint32_t veneer_off;               /* debug: power-off job's Shutdown.ttl call (BL 0xA00E9950) */
    volatile uint32_t *off_target;
} cfw_boot_t;

/* Flashed debug loader, ABI v1 (loader/flash/README.md §3): stage 1 carries
 * CFW_LDR_MAGIC at +4 and its hook entry at +8; the stub's status word. */
#define CFW_LDR_MAGIC     0x47524C31u  /* "GRL1" */
#define CFW_LDR_STATE     ((volatile uint32_t *)0xA00AFD70u)   /* 0xFFFFFFFF in stock firmware */
#define CFW_LDR_ARMED     5            /* stub: stage 1 is called after every section load */
#define CFW_LDR_DISARMED  6            /* stage 1: no further calls */

typedef int (*cfw_main_fn)(const cfw_boot_t *boot);
/* Called by stage 1 after every APP section load n (loader only, from the first hook, right
 * after the preload), in the boot task before section n has run: patch-on-load, early look. */
typedef void (*cfw_hook_fn)(uint32_t n, const cfw_boot_t *boot);

#endif /* BOOT_H */
