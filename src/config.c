/* CFW configuration: H:/cfw/config.txt, one "key=XXXXXXXX" line per entry
 * (hex values). Owned by the CFW task. File_Open flags (File.c, 0xA002C70F):
 * 0x00 = "r", 0x01 = "w" (create/truncate), 0x09 = "a". */
#include <stdint.h>
#include "config.h"
#include "cfw_log.h"
#include "gr2_fmt.h"
#include "gr2_hw.h"

#define CFG_PATH     "H:/cfw/config.txt"
#define CFG_MAX      48   /* settings + recorded camera values + recipe matrices (6 per MY slot) */
#define CFG_KEY_LEN  16
#define FILE_READ    0x00
#define FILE_WRITE   0x01

typedef struct { char key[CFG_KEY_LEN]; uint32_t value; } entry_t;

/* Entries and the file image live in one GlobalHeap block (the CFW window is small). */
typedef struct {
    entry_t cfg[CFG_MAX];
    char io[CFG_MAX * (CFG_KEY_LEN + 12)];   /* file image; load and save both run in the CFW task */
} store_t;
static store_t *S;
#define g_cfg (S->cfg)
#define g_io  (S->io)
static int g_n;
static int g_dirty;

static int key_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int find(const char *key) {
    for (int i = 0; i < g_n; i++) if (key_eq(g_cfg[i].key, key)) return i;
    return -1;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void parse_line(const char *s, int len) {
    int eq = -1;
    for (int i = 0; i < len; i++) if (s[i] == '=') { eq = i; break; }
    if (eq <= 0 || eq >= CFG_KEY_LEN || g_n >= CFG_MAX) return;
    uint32_t v = 0;
    for (int i = eq + 1; i < len; i++) {
        int d = hexval(s[i]);
        if (d < 0) break;
        v = (v << 4) | (uint32_t)d;
    }
    entry_t *e = &g_cfg[g_n++];
    for (int i = 0; i < eq; i++) e->key[i] = s[i];
    e->key[eq] = 0;
    e->value = v;
}

void cfg_load(void) {
    g_n = 0;
    g_dirty = 0;
    if (!S) S = GR2_FN_MEMORY_ALLOC(sizeof(store_t));
    if (!S) {
        ERR("config: Memory_Alloc failed, settings will not be saved");
        return;
    }
    char *buf = g_io;
    int fd = GR2_FN_FILE_OPEN(CFG_PATH, FILE_READ);
    if (fd == 0) { DBG("config: none"); return; }
    int n = GR2_FN_FILE_READ(fd, buf, sizeof(g_io));
    GR2_FN_FILE_CLOSE(fd);
    int start = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            if (i > start) parse_line(buf + start, i - start);
            start = i + 1;
        }
    }
    if (n > start) parse_line(buf + start, n - start);
    DBG_HEX("config: entries", (uint32_t)g_n);
}

int cfg_has(const char *key) {
    return find(key) >= 0;
}

uint32_t cfg_get(const char *key, uint32_t def) {
    int i = find(key);
    return i < 0 ? def : g_cfg[i].value;
}

void cfg_set(const char *key, uint32_t value) {
    if (!S) return;
    int i = find(key);
    if (i < 0) {
        if (g_n >= CFG_MAX) return;
        i = g_n++;
        int k = 0;
        while (key[k] && k < CFG_KEY_LEN - 1) { g_cfg[i].key[k] = key[k]; k++; }
        g_cfg[i].key[k] = 0;
    } else if (g_cfg[i].value == value) {
        return;
    }
    g_cfg[i].value = value;
    g_dirty = 1;
}

void cfg_save(void) {
    if (!S || !g_dirty) return;
    char *buf = g_io;
    char *p = buf;
    for (int i = 0; i < g_n; i++) {
        p = put_str(p, g_cfg[i].key);
        *p++ = '=';
        p = put_hex32(p, g_cfg[i].value);
        *p++ = '\r';
        *p++ = '\n';
    }
    int fd = GR2_FN_FILE_OPEN(CFG_PATH, FILE_WRITE);
    if (fd == 0) { ERR("config: cannot write H:/cfw/config.txt"); return; }
    GR2_FN_FILE_WRITE(fd, buf, (uint32_t)(p - buf));
    GR2_FN_FILE_CLOSE(fd);
    g_dirty = 0;
    DBG_HEX("config: saved entries", (uint32_t)g_n);
}
