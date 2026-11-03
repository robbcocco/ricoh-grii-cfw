/* Stage 1 (window 0xA0017000). Two entries:
 *
 * - cfw_boot: `ex` from H:/Startup.ttl, once per power cycle, after the camera
 *   has booted: check the window, load H:/tty/tty_main.bin into the heap,
 *   apply its relocations, zero its .bss and call its cfw_main.
 *
 * - cfw_ldr_hook(n, &ldr_state): a flashed loader stub (loader ABI v1; the debug
 *   loader from H:/cfw, M10; the release firmware from A:/cfw, M11: the `rom`
 *   build, CFW_ROM) after every section load in the root init task, from the
 *   section where the stub decided (section 3, after JobSystem step 4 has started
 *   the lens; debug stub v1: section 1). The first hook checks this image's
 *   length and CRC-32 (the stub's LoadFile truncates silently) and disarms on a
 *   mismatch; it then reads stage 2 while nothing can race the card (DCFMount lives
 *   in section 8) and writes nothing to the card; the card stays mounted, so the
 *   boot task's remount after section 9 is a short one. The rom build reads stage 2
 *   from A: with LoadFile and never touches the card in the hooks.
 *   hook(13) starts the "TTYldr" task and disarms the loader; that task waits
 *   until JobSystem boot step 13 (which queues Startup.ttl to the Script task) has
 *   finished, then calls cfw_main exactly like the Startup.ttl path. Step 13 does
 *   not wait for the script (finding 26), so a Startup.ttl on the card may still be
 *   running then; with the loader flashed the card must not carry TTY
 *   Startup.ttl. Stage 2's cfw_hook does patch-on-load and the early look (M10c).
 *
 * The heap block has never held code, so the I-cache has no stale lines for
 * it, and DRAM is write-through: instruction fetches see the loaded bytes. */
#include "boot.h"
#include "cfw_log.h"
#include "gr2_hw.h"
#include "safeboot.h"

extern char __image_end__[];   /* end of the hold veneer (gr2_boot.ld) */
extern uint32_t __bss_start__[], __bss_end__[];
void cfw_veneer_press(void);   /* src/veneers.S */
void cfw_veneer_hold(void);
void cfw_veneer_iq(void);
void cfw_veneer_off(void);
extern volatile uint32_t cfw_veneer_press_target, cfw_veneer_hold_target, cfw_veneer_iq_target,
    cfw_veneer_off_target;

#define FILE_READ   0
#define RELOC_CHUNK 64
#define LDR_START_SECTION 13           /* last section in both boot orders (finding 22 §3.1) */
#define LDR_TASK_PRIO     0x96
/* cfw_main runs here and does file I/O: the stock file API alone needs up to ~9.3 KB of the
 * caller's stack (finding 31). 0x1000 overflowed on hardware (F3, 2026-11-01: crash, garbled
 * labels); 0x4000 matches the Script task that runs cfw_main in the Startup.ttl path. */
#define LDR_TASK_STACK    0x4000
#define LDR_WAIT_MS       60000
#define LDR_POLL_MS       100

/* Bytes of the window after our image must still hold the erased-flash 0xFF
 * pattern: fload wrote only the file, so anything else means another writer. */
static uint32_t window_tail_not_ff(void) {
    const volatile uint8_t *p = (const volatile uint8_t *)__image_end__;
    const volatile uint8_t *end = (const volatile uint8_t *)(GR2_CFW_WINDOW_BASE + GR2_CFW_WINDOW_SIZE);
    uint32_t bad = 0;
    for (; p < end; p++) bad += (*p != 0xFF);
    return bad;
}

/* 1 if the window holds this image as linked: the length field (+12) is the linked size and
 * the CRC-32 (+16, IEEE as zlib.crc32) over [0, 16) + [20, length) matches (include/boot.h,
 * scripts/stage1_crc.py). Must run before anything in the window is written (.data, .bss).
 * ~32 K table steps (~1 ms [I]); the table lives on the caller's stack only meanwhile. */
static int image_ok(void) {
    const uint8_t *p = (const uint8_t *)GR2_CFW_WINDOW_BASE;
    const uint32_t *w = (const uint32_t *)GR2_CFW_WINDOW_BASE;
    uint32_t len = w[CFW_S1_LEN_OFF / 4];
    if (len != (uint32_t)(__image_end__ - (char *)GR2_CFW_WINDOW_BASE)) return 0;
    uint32_t t[256];
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        t[i] = c;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        if (i == CFW_S1_CRC_OFF) {
            i += 3;                    /* the CRC field itself */
            continue;
        }
        c = t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return ~c == w[CFW_S1_CRC_OFF / 4];
}

static int hdr_ok(const cfw_main_hdr_t *h) {
    return h->magic == CFW_MAIN_MAGIC && h->image_size != 0 && !(h->image_size & 3) && !(h->bss_size & 3) &&
           h->entry < h->image_size && h->hook < h->image_size;
}

static int relocate(uint8_t *base, uint32_t image_size, uint32_t off) {
    if (off > image_size - 4 || (off & 3)) {
        ERR_HEX("boot: bad relocation", off);
        return -1;
    }
    *(uint32_t *)(base + off) += (uint32_t)base;
    return 0;
}

static void finish_main(uint8_t *base, const cfw_main_hdr_t *h, uint32_t *base_out, uint32_t *entry_out,
                        uint32_t *hook_out) {
    for (uint32_t i = 0; i < h->bss_size; i += 4) *(uint32_t *)(base + h->image_size + i) = 0;
    *base_out = (uint32_t)base;
    *entry_out = (uint32_t)base + h->entry;
    *hook_out = (uint32_t)base + h->hook;
}

#if CFW_ROM
/* A: (finding 29 §9.3): LoadFile scans the directory twice per call, File_Read twice per read. Two calls:
 * the header, then header + image + relocations into one block with room for the larger of .bss and the
 * relocations; stage 2 runs 32 bytes into the block, the relocation words become its .bss. */
static int load_main(uint32_t *base_out, uint32_t *entry_out, uint32_t *hook_out) {
    cfw_main_hdr_t h;
    int n = GR2_FN_LOAD_FILE(CFW_MAIN_PATH, &h, sizeof(h));
    if (n < 0) {
        ERR("boot: " CFW_MAIN_PATH " missing");
        return -1;
    }
    if (n != (int)sizeof(h) || !hdr_ok(&h) || h.nrelocs > h.image_size / 4) {
        ERR("boot: " CFW_MAIN_PATH " invalid");
        return -1;
    }
    uint32_t rel = 4 * h.nrelocs, want = sizeof(h) + h.image_size + rel;
    uint32_t size = sizeof(h) + h.image_size + (h.bss_size > rel ? h.bss_size : rel);
    uint8_t *blk = GR2_FN_MEMORY_ALLOC(size);
    if (!blk || ((uint32_t)blk & 3)) {
        ERR_HEX("boot: Memory_Alloc failed, bytes", size);
        return -1;
    }
    if (GR2_FN_LOAD_FILE(CFW_MAIN_PATH, blk, want) != (int)want) {
        ERR("boot: " CFW_MAIN_PATH " truncated");
        return -1;
    }
    uint8_t *base = blk + sizeof(h);
    const uint32_t *r = (const uint32_t *)(base + h.image_size);
    for (uint32_t i = 0; i < h.nrelocs; i++)
        if (relocate(base, h.image_size, r[i])) return -1;
    finish_main(base, &h, base_out, entry_out, hook_out);
    return 0;
}
#else
static int read_all(int fd, void *buf, uint32_t size) {
    return GR2_FN_FILE_READ(fd, buf, size) == (int)size ? 0 : -1;
}

static int load_fd(int fd, uint32_t *base_out, uint32_t *entry_out, uint32_t *hook_out) {
    cfw_main_hdr_t h;
    if (read_all(fd, &h, sizeof(h)) || !hdr_ok(&h)) {
        ERR("boot: " CFW_MAIN_PATH " invalid");
        return -1;
    }
    uint8_t *base = GR2_FN_MEMORY_ALLOC(h.image_size + h.bss_size);
    if (!base || ((uint32_t)base & 3)) {
        ERR_HEX("boot: Memory_Alloc failed, bytes", h.image_size + h.bss_size);
        return -1;
    }
    if (read_all(fd, base, h.image_size)) {
        ERR("boot: " CFW_MAIN_PATH " truncated");
        return -1;
    }
    uint32_t chunk[RELOC_CHUNK];
    for (uint32_t done = 0; done < h.nrelocs;) {
        uint32_t n = h.nrelocs - done < RELOC_CHUNK ? h.nrelocs - done : RELOC_CHUNK;
        if (read_all(fd, chunk, n * 4)) {
            ERR("boot: relocations truncated");
            return -1;
        }
        for (uint32_t i = 0; i < n; i++)
            if (relocate(base, h.image_size, chunk[i])) return -1;
        done += n;
    }
    finish_main(base, &h, base_out, entry_out, hook_out);
    return 0;
}

static int load_main(uint32_t *base_out, uint32_t *entry_out, uint32_t *hook_out) {
    int fd = GR2_FN_FILE_OPEN(CFW_MAIN_PATH, FILE_READ);
    if (fd == 0) {
        ERR("boot: " CFW_MAIN_PATH " missing");
        return -1;
    }
    int rc = load_fd(fd, base_out, entry_out, hook_out);
    GR2_FN_FILE_CLOSE(fd);
    return rc;
}
#endif

/* Window check + stage 2 into the heap. 0 on success. */
static int preload(uint32_t *base, uint32_t *entry, uint32_t *hook) {
    uint32_t dirty = window_tail_not_ff();
    if (dirty) {
        ERR_HEX("window 0xA0017000-0xA001EFFF in use by someone else, bytes changed", dirty);
        return -1;
    }
    return load_main(base, entry, hook);
}

static cfw_boot_t g_boot;

static void make_boot(uint32_t base, uint32_t loader, uint32_t t_preload) {
    g_boot.base = base;
    g_boot.veneer_press = (uint32_t)cfw_veneer_press;
    g_boot.veneer_hold = (uint32_t)cfw_veneer_hold;
    g_boot.press_target = &cfw_veneer_press_target;
    g_boot.hold_target = &cfw_veneer_hold_target;
    g_boot.loader = loader;
    g_boot.t_preload = t_preload;
    g_boot.veneer_iq = (uint32_t)cfw_veneer_iq;
    g_boot.iq_target = &cfw_veneer_iq_target;
    g_boot.veneer_off = (uint32_t)cfw_veneer_off;
    g_boot.off_target = &cfw_veneer_off_target;
}

int cfw_boot(void) {
    uint32_t st = *CFW_LDR_STATE;
    if (st == CFW_LDR_ARMED || st == CFW_LDR_DISARMED) {
        /* The flashed loader already runs this stage 1: `fload` has just rewritten
         * it under the running TTY. Do not start a second one. */
        ERR("boot: flashed loader active, Startup.ttl must not load TTY (remove it from the card)");
        return -1;
    }
    if (!image_ok()) {
        ERR("boot: stage 1 length/CRC mismatch (gr2_cfw.bin truncated or corrupt), TTY not started");
        return -1;
    }
    uint32_t base = 0, entry = 0, hook = 0;
    if (preload(&base, &entry, &hook)) return -1;
    make_boot(base, 0, 0);
    return ((cfw_main_fn)entry)(&g_boot);
}

/* ---- loader ABI v1 ------------------------------------------------------------- */

static uint32_t g_ldr_first = 1;       /* .data: 1 until the first hook call zeroed .bss */
static uint32_t g_main_entry, g_main_hook;
static int g_ldr_err;

/* Runs in the TTYldr task: wait for boot step 13, then start stage 2. */
int cfw_ldr_start(void) {
    for (uint32_t t = 0; !GR2_BOOT_STEP_DONE(LDR_START_SECTION); t += LDR_POLL_MS) {
        if (t >= LDR_WAIT_MS) {
            cfw_log_release();
            ERR("boot (loader): JobSystem step 13 not done after 60 s, TTY not started");
            return -1;
        }
        GR2_FN_DELAY(LDR_POLL_MS);
    }
    cfw_log_release();                 /* the card is the camera's now (remounted after section 9) */
    if (g_ldr_err || !g_main_entry) {
        ERR("boot (loader): stage 2 not loaded at the first hook, TTY not started");
        return -1;
    }
    return ((cfw_main_fn)g_main_entry)(&g_boot);
}

static void ldr_task(uint32_t arg) {
    (void)arg;
    cfw_ldr_start();
    for (;;) GR2_FN_DELAY(60000);      /* task bodies never return */
}

/* M12 safe boot (include/safeboot.h): 1 = run TTY this boot. The state moves on before anything of
 * stage 2 runs, so a crash or hang in it counts. */
static int safeboot(void) {
    uint32_t s = sb_get(), was = s;
    if (s == SB_ONE_FAIL && GR2_LAST_OFF_CLEAN == 1) s = SB_ARMED;   /* that session ended by the power-off job */
    int run = 0;
    if (s == SB_STOCK_ONCE) {
        s = SB_ARMED;
    } else if (s == SB_DISARMED) {
        uint32_t ui = GR2_FN_PROP_GET_RAW(GR2_PROPID_UI_MODE);
        if (ui == 2 || ui == 3) s = SB_ARMED;                          /* PLAY power-on: TTY again next boot */
    } else {
        s = s == SB_ARMED ? SB_ONE_FAIL : SB_DISARMED;
        run = 1;
    }
    if (s != was) sb_set(s);
    DBG_HEX2("safeboot: state (before, after)", was, s);
    return run;
}

/* After every section load n, in the boot task (loader/flash/README.md §3). Nothing may
 * be written to H: here: the boot task remounts the card after section 9 (finding 32).
 * Log lines are held until cfw_ldr_start. */
void cfw_ldr_hook(uint32_t n, volatile uint32_t *state) {
    if (g_ldr_first) {                 /* the loader never ran _start */
        int bad = !image_ok();         /* the window exactly as the stub loaded it */
        for (uint32_t *p = __bss_start__; p < __bss_end__; p++) *p = 0;
        g_ldr_first = 0;
        cfw_log_hold();
        if (bad) {
            /* Truncated or corrupt: nothing else of this image runs (no stage 2, no TTYldr task), so the
             * line stays in stage 1's hold buffer in RAM. */
            ERR("boot (loader): stage 1 length/CRC mismatch, TTY disarmed");
            *state = CFW_LDR_DISARMED;
            return;
        }
        if (!safeboot()) {             /* disarmed or "boot stock once": a stock boot, nothing else runs */
            *state = CFW_LDR_DISARMED;
            return;
        }
        uint32_t base = 0, t = GR2_FN_SYSTEM_GETTIME();
        g_ldr_err = preload(&base, &g_main_entry, &g_main_hook);
        if (!g_ldr_err) make_boot(base, 1, t);
    }
    if (g_main_hook && !g_ldr_err) ((cfw_hook_fn)g_main_hook)(n, &g_boot);
    if (n == LDR_START_SECTION) {
        if (!GR2_FN_TASK_CREATE(ldr_task, 0, LDR_TASK_PRIO, LDR_TASK_STACK, "TTYldr")) g_ldr_err = 1;
        *state = CFW_LDR_DISARMED;
    }
}
