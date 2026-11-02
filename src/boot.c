/* Stage 1 (window 0xA0017000). Two entries:
 *
 * - cfw_boot: `ex` from H:/Startup.ttl, once per power cycle, after the camera
 *   has booted: check the window, load H:/cfw/gr2_main.bin into the heap,
 *   apply its relocations, zero its .bss and call its cfw_main.
 *
 * - cfw_ldr_hook(n, &ldr_state): the flashed debug loader (M10, loader ABI v1)
 *   after every section load in the root init task. hook(1) reads stage 2
 *   while nothing can race the card (DCFMount lives in section 8) and writes
 *   nothing to the card (a USB boot hands it to the PC). hook(13) starts the
 *   "CFWldr" task and disarms the loader; that task waits until JobSystem boot
 *   step 13 (which queues Startup.ttl to the Script task) has finished, then
 *   calls cfw_main exactly like the Startup.ttl path. Step 13 does not wait for
 *   the script (finding 26), so a Startup.ttl on the card may still be running
 *   then; with the loader flashed the card must not carry the CFW Startup.ttl.
 *   Patch-on-load and the look before the first live-view frame are the next
 *   step (the project plan M10c).
 *
 * The heap block has never held code, so the I-cache has no stale lines for
 * it, and DRAM is write-through: instruction fetches see the loaded bytes. */
#include "boot.h"
#include "cfw_log.h"
#include "gr2_hw.h"

extern char __image_end__[];   /* end of the hold veneer (gr2_boot.ld) */
extern uint32_t __bss_start__[], __bss_end__[];
void cfw_veneer_press(void);   /* src/veneers.S */
void cfw_veneer_hold(void);
extern volatile uint32_t cfw_veneer_press_target, cfw_veneer_hold_target;

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

static int read_all(int fd, void *buf, uint32_t size) {
    return GR2_FN_FILE_READ(fd, buf, size) == (int)size ? 0 : -1;
}

static int load_main(int fd, uint32_t *base_out, uint32_t *entry_out, uint32_t *hook_out) {
    cfw_main_hdr_t h;
    if (read_all(fd, &h, sizeof(h)) || h.magic != CFW_MAIN_MAGIC || h.image_size == 0 ||
        (h.image_size & 3) || (h.bss_size & 3) || h.entry >= h.image_size || h.hook >= h.image_size) {
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
        for (uint32_t i = 0; i < n; i++) {
            if (chunk[i] > h.image_size - 4 || (chunk[i] & 3)) {
                ERR_HEX("boot: bad relocation", chunk[i]);
                return -1;
            }
            *(uint32_t *)(base + chunk[i]) += (uint32_t)base;
        }
        done += n;
    }
    for (uint32_t i = 0; i < h.bss_size; i += 4) *(uint32_t *)(base + h.image_size + i) = 0;
    *base_out = (uint32_t)base;
    *entry_out = (uint32_t)base + h.entry;
    *hook_out = (uint32_t)base + h.hook;
    return 0;
}

/* Window check + stage 2 into the heap. 0 on success. */
static int preload(uint32_t *base, uint32_t *entry, uint32_t *hook) {
    uint32_t dirty = window_tail_not_ff();
    if (dirty) {
        ERR_HEX("window 0xA0017000-0xA001EFFF in use by someone else, bytes changed", dirty);
        return -1;
    }
    int fd = GR2_FN_FILE_OPEN(CFW_MAIN_PATH, FILE_READ);
    if (fd == 0) {
        ERR("boot: " CFW_MAIN_PATH " missing");
        return -1;
    }
    int rc = load_main(fd, base, entry, hook);
    GR2_FN_FILE_CLOSE(fd);
    return rc;
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
}

int cfw_boot(void) {
    uint32_t st = *CFW_LDR_STATE;
    if (st == CFW_LDR_ARMED || st == CFW_LDR_DISARMED) {
        /* The flashed loader already runs this stage 1: `fload` has just rewritten
         * it under the running CFW. Do not start a second one. */
        ERR("boot: flashed loader active, Startup.ttl must not load the CFW (remove it from the card)");
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

/* Runs in the CFWldr task: wait for boot step 13, then start stage 2. */
int cfw_ldr_start(void) {
    for (uint32_t t = 0; !GR2_BOOT_STEP_DONE(LDR_START_SECTION); t += LDR_POLL_MS) {
        if (t >= LDR_WAIT_MS) {
            cfw_log_release();
            ERR("boot (loader): JobSystem step 13 not done after 60 s, CFW not started");
            return -1;
        }
        GR2_FN_DELAY(LDR_POLL_MS);
    }
    cfw_log_release();                 /* the card is the camera's now (remounted after section 9) */
    if (g_ldr_err || !g_main_entry) {
        ERR("boot (loader): stage 2 not loaded at hook(1), CFW not started");
        return -1;
    }
    return ((cfw_main_fn)g_main_entry)(&g_boot);
}

static void ldr_task(uint32_t arg) {
    (void)arg;
    cfw_ldr_start();
    for (;;) GR2_FN_DELAY(60000);      /* task bodies never return */
}

/* After every section load n, in the boot task (loader/flash/README.md §3). Nothing may
 * be written to H: here: the boot task remounts the card after section 9 (finding 32), and
 * a USB-charge boot loads only section 1. Log lines are held until cfw_ldr_start. */
void cfw_ldr_hook(uint32_t n, volatile uint32_t *state) {
    if (g_ldr_first) {                 /* the loader never ran _start */
        for (uint32_t *p = __bss_start__; p < __bss_end__; p++) *p = 0;
        g_ldr_first = 0;
        cfw_log_hold();
    }
    if (n == 1 && !g_main_entry && !g_ldr_err) {
        uint32_t base = 0, t = GR2_FN_SYSTEM_GETTIME();
        g_ldr_err = preload(&base, &g_main_entry, &g_main_hook);
        if (!g_ldr_err) make_boot(base, 1, t);
    }
    if (g_main_hook && !g_ldr_err) ((cfw_hook_fn)g_main_hook)(n, &g_boot);
    /* Card reads are done: unmount and power the card down, so the boot task's mount after
     * section 9 is a fresh one as in a stock boot. Testing whether the stub's early card
     * session is what makes the lens extend, retract and extend again (finding 33, T4). */
    if (n == 1) GR2_FN_FILE_UNMOUNT("H:");
    if (n == LDR_START_SECTION) {
        if (!GR2_FN_TASK_CREATE(ldr_task, 0, LDR_TASK_PRIO, LDR_TASK_STACK, "CFWldr")) g_ldr_err = 1;
        *state = CFW_LDR_DISARMED;
    }
}
