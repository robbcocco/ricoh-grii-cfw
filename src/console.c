/* Developer console: telnet server on CONSOLE_PORT (finding 21 §3.2, §5).
 *
 * Capture: the console printf (0xA0039CBD) hands every formatted line to the
 * hook in GR2_CONSOLE_HOOK_SLOT instead of the UART while one is installed
 * (stock `attach` uses the same slot). The CFW hook copies the text into a
 * 16 KiB ring; CFW log lines are mirrored into it too (cfw_log_tap). The hook
 * runs in the printing task with the console mutex held: it only copies bytes.
 *
 * Server: one task ("CFWcon", like stock telnetd: socket/bind/listen/accept,
 * then a line loop) on port 23; the stock telnetd is not started. Commands:
 * help, status, log, tail, sh, tasks, heap, mem, wifi, export, exit; anything else
 * runs as a stock shell line (Shell_Execute in this task, as stock telnetd does).
 *
 * On/off: console_set(0) closes the listening and the session socket from the
 * CFW task (the blocked accept/recv in CFWcon then fails) and puts printf back
 * on the UART; the task idles until console_set(1). */
#include <stdint.h>
#include "console.h"
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_fmt.h"
#include "gr2_hw.h"
#include "export.h"
#include "info.h"
#include "settings.h"

#define RING          0x4000u          /* bytes, power of two */
#define LINE          160
#define OUT_BUF       512
#define TASK_PRIO     0x96             /* as telnetd */
#define TASK_STACK    0x3000
#define TAIL_MAX_S    300
#define MEM_MAX       0x400u
#define POOL_WALK_MAX 128
#define EXPORT_WAIT_S 30               /* the CFW task picks a request up within 20 ms, writing takes ~1 s */
#define TSK_MAGIC     0x54534B20u      /* 'TSK ' */
#define MPL_MAGIC     0x4D504C20u      /* 'MPL ' */
#define POOL_BASE     0xA0900000u      /* free-memory pool start (FUN_a00542d0) */

static volatile char *g_ring;
static volatile uint32_t g_head;       /* bytes ever written; index = g_head & (RING - 1) */
static int g_started;                  /* CFWcon task exists */
static volatile int g_enabled;
static volatile int g_listen = -1, g_session = -1;   /* sockets, -1 = none */

/* ---- capture ----------------------------------------------------------------- */

static void ring_put(const char *s, uint32_t n) {
    volatile char *r = g_ring;
    if (!r) return;
    uint32_t h = g_head;
    for (uint32_t i = 0; i < n; i++) r[(h + i) & (RING - 1)] = s[i];
    g_head = h + n;
}

static void printf_hook(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    ring_put(s, n);
}

/* ---- output: buffered, CRLF, sent per OUT_BUF (fd >= 0) or truncated (fd < 0) ---- */

typedef struct { char *base, *p, *end; int fd, failed; } out_t;

static void out_flush(out_t *o) {
    if (o->fd < 0 || o->failed) return;
    const char *q = o->base;
    while (q < o->p) {
        int n = GR2_FN_SEND(o->fd, q, (int)(o->p - q), 0);
        if (n <= 0) { o->failed = 1; break; }
        q += n;
    }
    o->p = o->base;
}

static void out_ch(out_t *o, char c) {
    if (o->p >= o->end) {
        if (o->fd < 0) return;
        out_flush(o);
    }
    *o->p++ = c;
}

static void out_s(out_t *o, const char *s) {
    for (; *s; s++) {
        if (*s == '\n') out_ch(o, '\r');
        out_ch(o, *s);
    }
}

static void out_hex(out_t *o, uint32_t v) {
    char b[12];
    *put_hex32(put_str(b, "0x"), v) = 0;
    out_s(o, b);
}

static void out_dec(out_t *o, uint32_t v) {
    char b[12];
    *put_udec(b, v) = 0;
    out_s(o, b);
}

static void out_pad(out_t *o, const char *s, int w) {
    while (*s && w > 0) { out_ch(o, *s++); w--; }
    while (w-- > 0) out_ch(o, ' ');
}

/* Ring bytes [from, g_head) that are still in the ring. */
static uint32_t out_ring(out_t *o, uint32_t from) {
    uint32_t h = g_head;
    if (!g_ring) return h;
    if (h - from > RING) from = h - RING;
    for (; from != h; from++) {
        char c = g_ring[from & (RING - 1)];
        if (c == '\n') out_ch(o, '\r');
        out_ch(o, c);
    }
    return h;
}

/* ---- parsing ------------------------------------------------------------------ */

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Word match: s starts with w followed by end or space. Returns the rest or 0. */
static const char *word(const char *s, const char *w) {
    while (*w && *s == *w) { s++; w++; }
    if (*w || (*s && *s != ' ' && *s != '\t')) return 0;
    return skip_ws(s);
}

/* Hex (optional 0x) or decimal number; returns the rest or 0 if none. */
static const char *number(const char *s, uint32_t *v, int hex) {
    uint32_t r = 0;
    int n = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; hex = 1; }
    for (;; s++, n++) {
        uint32_t d;
        if (*s >= '0' && *s <= '9') d = (uint32_t)(*s - '0');
        else if (hex && *s >= 'a' && *s <= 'f') d = (uint32_t)(*s - 'a' + 10);
        else if (hex && *s >= 'A' && *s <= 'F') d = (uint32_t)(*s - 'A' + 10);
        else break;
        r = hex ? (r << 4) | d : r * 10 + d;
    }
    if (!n) return 0;
    *v = r;
    return skip_ws(s);
}

/* ---- commands --------------------------------------------------------------- */

static void cmd_help(out_t *o) {
    out_s(o, "help              this list\n"
             "status            version, uptime, CFW settings\n"
             "log [bytes]       captured printf + CFW log (default 2048)\n"
             "tail [seconds]    stream new output (default 10, max 300)\n"
             "sh <command>      run a stock shell command, show its output\n"
             "tasks             RTOS tasks: id, name, state, priority, stack\n"
             "heap              GlobalHeap free, memory pools, pool map\n"
             "mem <addr> [len]  hex dump (hex; len <= 0x400; DRAM only)\n"
             "wifi              Wi-Fi state as the system controller sees it\n"
             "export            camera presets as recipe files in cfw/export\n"
             "exit              close the session\n"
             "anything else     runs as a stock shell command (like sh)\n");
}

static void cmd_wifi(out_t *o) {
    static const char *const NAME[INFO_WIFI_N] = {
        "system mode (prop 0)", "UI mode (prop 1)", "connect mode (prop 2)", "Wi-Fi request (prop 0x1E8)",
        "Wi-Fi status (prop 0x260)", "JOBID_WIFI state (Ctrl+0x214)", "last posted (Ctrl+0x1B8)",
        "force flag (Ctrl+0x1BC)" };
    uint32_t v[INFO_WIFI_N];
    info_wifi_state(v);
    for (int i = 0; i < INFO_WIFI_N; i++) {
        out_pad(o, NAME[i], 32);
        out_hex(o, v[i]);
        out_s(o, "\n");
    }
}

static void cmd_status(out_t *o) {
    out_s(o, "gr2_cfw " CFW_VERSION);
    out_s(o, CFW_DEBUG ? " (debug)\nuptime ms " : " (release)\nuptime ms ");
    out_dec(o, GR2_FN_SYSTEM_GETTIME());
    out_s(o, ", CFW task beats ");
    out_dec(o, cfw_task_heartbeat());
    out_s(o, ", capture ");
    out_s(o, GR2_CONSOLE_HOOK_SLOT == printf_hook ? "on\n" : "off (hook slot taken, e.g. by attach)\n");
    for (int i = 0; i < settings_count(); i++) {
        out_s(o, "  ");
        out_pad(o, settings_label(i), 30);
        out_s(o, settings_value(i));
        out_ch(o, '\r');
        out_ch(o, '\n');
    }
}

static void cmd_log(out_t *o, const char *a) {
    uint32_t n = 2048;
    if (*a) number(a, &n, 0);
    if (n > RING) n = RING;
    uint32_t h = g_head;
    out_ring(o, h > n ? h - n : 0);
}

static void cmd_tail(out_t *o, const char *a) {
    uint32_t s = 10, from = g_head;
    if (*a) number(a, &s, 0);
    if (s > TAIL_MAX_S) s = TAIL_MAX_S;
    for (uint32_t t = 0; t < s * 5 && !o->failed && g_enabled; t++) {
        GR2_FN_DELAY(200);
        from = out_ring(o, from);
        out_flush(o);
    }
}

/* The export runs in the CFW task (src/export.c): ask for it and wait for its result. */
static void cmd_export(out_t *o) {
    uint32_t ticket = export_request();
    for (int t = 0; t < EXPORT_WAIT_S * 10 && !export_served(ticket); t++) GR2_FN_DELAY(100);
    if (!export_served(ticket)) {
        out_s(o, "export: requested, the CFW task has not run it yet (result on the DEV tab)\n");
        return;
    }
    out_s(o, "export: ");
    out_s(o, export_result());
    out_s(o, "\n");
}

static void cmd_sh(out_t *o, const char *a) {
    static char result[0x104];
    if (!*a) { out_s(o, "usage: sh <command>\n"); return; }
    uint32_t from = g_head;
    int rc = GR2_FN_SHELL_EXECUTE(a);
    out_ring(o, from);
    out_s(o, "result: ");
    if (rc < 0) { out_ch(o, '-'); rc = -rc; }
    out_dec(o, (uint32_t)rc);
    if (GR2_FN_SHELL_RESULT(result) == 0) {
        result[0x100] = 0;
        out_s(o, " <");
        out_s(o, result);
        out_ch(o, '>');
    }
    out_s(o, "\n");
}

static void cmd_tasks(out_t *o) {
    static const char *const ST[] = { "-", "RUN", "READY", "?", "WAIT", "?", "?", "?", "SUSP" };
    out_s(o, "id   name             state  pri  stack top   size\n");
    for (int id = 1; id < 0x60; id++) {
        uint32_t pk[16];
        if (GR2_FN_REF_TSK(id, pk) < 0) continue;
        const volatile uint32_t *t = (const volatile uint32_t *)GR2_TSK_OBJ_TABLE[id];
        int named = t && t[0] == TSK_MAGIC && (t[4] & 0xE0000000u) == 0xA0000000u;
        uint32_t stat = pk[0] >> 16, pri = pk[0] & 0xFFFF;
        out_hex(o, (uint32_t)id);
        out_s(o, " ");
        out_pad(o, named ? (const char *)t[4] : "?", 16);
        out_s(o, " ");
        out_pad(o, stat == 0xC ? "WAIT+S" : stat < 9 ? ST[stat] : "?", 6);
        out_s(o, " ");
        out_dec(o, pri);
        if (named) {
            out_s(o, "  ");
            out_hex(o, t[2]);
            out_s(o, "  ");
            out_hex(o, t[3]);
        }
        out_s(o, "\n");
    }
}

static int dram(uint32_t a) {
    return (a & 0xE0000000u) == 0xA0000000u;
}

static void cmd_heap(out_t *o) {
    uint32_t gh = *GR2_POOL_GLOBALHEAP;
    if (dram(gh)) {
        uint32_t pk[8] = { 0 };
        int id = *(const volatile int16_t *)(gh + 0x14);
        out_s(o, "GlobalHeap: size ");
        out_hex(o, *(const volatile uint32_t *)(gh + 0x0C));
        out_s(o, ", high water ");
        out_hex(o, *(const volatile uint32_t *)(gh + 0x10));
        if (GR2_FN_REF_MPL(id, pk) >= 0) {
            out_s(o, ", free ");
            out_hex(o, pk[1]);
            out_s(o, ", largest block ");
            out_hex(o, pk[2]);
        }
        out_s(o, "\n");
    }
    out_s(o, "memory pools (id name size used-max):\n");
    for (int id = 1; id < 0x40; id++) {
        const volatile uint32_t *m = (const volatile uint32_t *)(GR2_MPL_TABLE + 0x1Cu * (uint32_t)id);
        if (m[0] != MPL_MAGIC) continue;
        out_s(o, "  ");
        out_hex(o, (uint32_t)id);
        out_s(o, " ");
        out_pad(o, dram(m[6]) ? (const char *)m[6] : "?", 16);
        out_s(o, " ");
        out_hex(o, m[3]);
        out_s(o, " ");
        out_hex(o, m[4]);
        out_s(o, "\n");
    }
    out_s(o, "pool map (name data size), bump ");
    out_hex(o, GR2_POOL_BUMP);
    out_s(o, ":\n");
    uint32_t p = POOL_BASE, end = GR2_POOL_BUMP;
    for (int n = 0; p < end && n < POOL_WALK_MAX; n++) {
        const volatile uint32_t *h = (const volatile uint32_t *)p;
        uint32_t nm = h[0], d = h[1], sz = h[2];
        if (d != ((p + 0x1B) & ~0xFu) || sz > end - d || !dram(nm)) {
            out_s(o, "  (walk stopped at ");
            out_hex(o, p);
            out_s(o, ")\n");
            return;
        }
        out_s(o, "  ");
        out_pad(o, (const char *)nm, 16);
        out_s(o, " ");
        out_hex(o, d);
        out_s(o, " ");
        out_hex(o, sz);
        out_s(o, "\n");
        p = (d + sz + 3) & ~3u;
    }
}

static void cmd_mem(out_t *o, const char *a) {
    uint32_t addr, len = 0x100;
    a = number(a, &addr, 1);
    if (!a) { out_s(o, "usage: mem <addr> [len]\n"); return; }
    if (*a) number(a, &len, 1);
    if (len > MEM_MAX) len = MEM_MAX;
    uint32_t phys = addr & 0x1FFFFFFFu;
    if (!len || !(dram(addr) || (addr & 0xE0000000u) == 0x20000000u) || phys + len > 0x20000000u) {
        out_s(o, "only DRAM 0xA0000000-0xBFFFFFFF (or its 0x2... alias)\n");
        return;
    }
    for (uint32_t off = 0; off < len; off += 16) {
        const volatile uint8_t *b = (const volatile uint8_t *)(addr + off);
        char line[80], *q = put_hex32(line, addr + off);
        *q++ = ' ';
        uint32_t n = len - off < 16 ? len - off : 16;
        for (uint32_t i = 0; i < 16; i++) {
            if (i < n) {
                static const char HX[] = "0123456789ABCDEF";
                *q++ = ' ';
                *q++ = HX[b[i] >> 4];
                *q++ = HX[b[i] & 15];
            } else {
                q = put_str(q, "   ");
            }
        }
        q = put_str(q, "  ");
        for (uint32_t i = 0; i < n; i++) *q++ = (b[i] >= 0x20 && b[i] < 0x7F) ? (char)b[i] : '.';
        *q++ = '\n';
        *q = 0;
        out_s(o, line);
    }
}

/* Returns 0, or -1 for exit. */
static int run(const char *line, out_t *o) {
    const char *s = skip_ws(line), *a;
    if (!*s) return 0;
    if ((a = word(s, "help"))) cmd_help(o);
    else if ((a = word(s, "status"))) cmd_status(o);
    else if ((a = word(s, "log"))) cmd_log(o, a);
    else if ((a = word(s, "tail"))) cmd_tail(o, a);
    else if ((a = word(s, "sh"))) cmd_sh(o, a);
    else if ((a = word(s, "tasks"))) cmd_tasks(o);
    else if ((a = word(s, "heap"))) cmd_heap(o);
    else if ((a = word(s, "mem"))) cmd_mem(o, a);
    else if ((a = word(s, "wifi"))) cmd_wifi(o);
    else if ((a = word(s, "export"))) cmd_export(o);
    else if ((a = word(s, "exit"))) return -1;
    else cmd_sh(o, s);                          /* like stock telnet on port 23 */
    return 0;
}

int console_exec(const char *line, char *out, int max) {
    if (max < 1) return 0;
    out_t o = { out, out, out + max - 1, -1, 0 };
    int rc = run(line, &o);
    *o.p = 0;
    return rc < 0 ? -1 : (int)(o.p - out);
}

/* ---- server ------------------------------------------------------------------ */

static void session(int fd) {
    char obuf[OUT_BUF], line[LINE];
    uint8_t in[64];
    int n = 0, iac = 0;
    out_t o = { obuf, obuf, obuf + OUT_BUF, fd, 0 };
    out_s(&o, "gr2_cfw " CFW_VERSION " developer console (help for commands)\n> ");
    out_flush(&o);
    while (!o.failed && g_enabled) {
        int got = GR2_FN_RECV(fd, in, sizeof(in), 0);
        if (got <= 0) return;
        for (int i = 0; i < got; i++) {
            uint8_t c = in[i];
            if (iac) { iac--; continue; }            /* telnet option negotiation: skip */
            if (c == 0xFF) { iac = 2; continue; }
            if (c == '\r' || c == '\n') {
                if (!n) continue;
                line[n] = 0;
                n = 0;
                if (run(line, &o) < 0) { out_flush(&o); return; }
                out_s(&o, "> ");
                out_flush(&o);
            } else if ((c == 8 || c == 0x7F) && n) {
                n--;
            } else if (c >= 0x20 && n < LINE - 1) {
                line[n++] = (char)c;
            }
        }
    }
}

/* Close fd unless console_set(0) already did (it swaps the slot to -1 first). */
static void close_slot(volatile int *slot, int fd) {
    if (*slot == fd) {
        *slot = -1;
        GR2_FN_SOCK_CLOSE(fd);
    }
}

static void server(uint32_t arg) {
    (void)arg;
    for (;;) {
        if (!g_enabled) { GR2_FN_DELAY(200); continue; }
        int s = GR2_FN_SOCKET(2, 1, 0);
        if (s < 0) { GR2_FN_DELAY(1000); continue; }
        uint8_t sa[16] = { 0x10, 2, CONSOLE_PORT >> 8, CONSOLE_PORT & 0xFF };
        if (GR2_FN_BIND(s, sa, 16) < 0 || GR2_FN_LISTEN(s, 2) < 0) {
            GR2_FN_SOCK_CLOSE(s);
            GR2_FN_DELAY(1000);                      /* network not up yet: retry */
            continue;
        }
        g_listen = s;
        while (g_enabled) {
            uint8_t ca[16];
            int len = 16;
            int c = GR2_FN_ACCEPT(s, ca, &len);
            if (c < 0) break;
            g_session = c;
            session(c);
            close_slot(&g_session, c);
        }
        close_slot(&g_listen, s);
        GR2_FN_DELAY(1000);                          /* Wi-Fi not up yet or dropped: retry */
    }
}

static void log_tap(const char *s, uint32_t n) {
    ring_put(s, n);
}

int console_set(int on) {
    if (!on) {
        if (!g_enabled) return 0;
        g_enabled = 0;
        int fd = g_session;
        g_session = -1;
        if (fd >= 0) GR2_FN_SOCK_CLOSE(fd);
        fd = g_listen;
        g_listen = -1;
        if (fd >= 0) GR2_FN_SOCK_CLOSE(fd);
        if (GR2_CONSOLE_HOOK_SLOT == printf_hook) GR2_FN_CONSOLE_HOOK(0);   /* printf back to the UART */
        cfw_log_tap = 0;
        DBG("console: off");
        return 0;
    }
    if (g_enabled) return 0;
    if (!g_ring) {
        g_ring = (volatile char *)GR2_FN_MEMORY_ALLOC(RING);
        if (!g_ring) {
            ERR("console: no memory for the capture ring");
            return -1;
        }
    }
    if (!g_started) {
        if (!GR2_FN_TASK_CREATE(server, 0, TASK_PRIO, TASK_STACK, "CFWcon")) {
            ERR("console: Task_Create failed");
            return -1;
        }
        g_started = 1;
    }
    cfw_log_tap = log_tap;
    if (GR2_CONSOLE_HOOK_SLOT == 0) GR2_FN_CONSOLE_HOOK(printf_hook);
    else ERR("console: printf hook slot in use (attach?), capture off");
    g_enabled = 1;
    DBG("console: on");
    return 0;
}

int console_state(void) {
    return !g_enabled ? CONSOLE_OFF : g_listen >= 0 ? CONSOLE_LISTENING : CONSOLE_WAITING;
}
