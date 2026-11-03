/* TTY configuration: one "key=XXXXXXXX" line per entry (hex values), kept on the
 * internal D: drive in two generations (src/store.c, finding 34), with a backup
 * copy in H:/tty/config.txt whenever the card is there. Stock formats D: on the
 * first boot after a battery pull or crash (finding 34 §11); when D: holds no
 * valid copy (that, Reset Settings, a first start) the card copy is imported and
 * the next cfg_save writes it back to D:. Owned by the TTY task; the loader's
 * first hook only loads (rom: from D: only, the card is read in the task's late phase). File_Open flags (File.c, 0xA002C70F): 0x00 = "r",
 * 0x01 = "w" (create/truncate). */
#include <stdint.h>
#include "config.h"
#include "cfw_log.h"
#include "gr2_fmt.h"
#include "gr2_hw.h"
#include "store.h"

#define CARD_PATH    "H:/tty/config.txt"
#define CFG_MAX      48   /* settings + recorded camera values + recipe matrices (6 per MY slot) */
#define CFG_KEY_LEN  16
#define FILE_READ    0x00
#define FILE_WRITE   0x01

typedef struct { char key[CFG_KEY_LEN]; uint32_t value; } entry_t;

/* Entries and the file image live in one GlobalHeap block (the TTY window is small). */
typedef struct {
    entry_t cfg[CFG_MAX];
    char io[CFG_MAX * (CFG_KEY_LEN + 12)];   /* text image; load and save both run in the TTY task */
    char card[CFG_MAX * (CFG_KEY_LEN + 12)]; /* the card copy as read, compared by cfg_sync_card */
} cfg_store_t;
static cfg_store_t *S;
#define g_cfg (S->cfg)
#define g_io  (S->io)
static int g_n;
static int g_dirty;
static store_t g_file = { { "D:/CFW/CFG_A.DAT", "D:/CFW/CFG_B.DAT" }, 0x47524331u /* "GRC1" */, 0, -1 };

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

static void parse_text(const char *buf, int n) {
    int start = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            if (i > start) parse_line(buf + start, i - start);
            start = i + 1;
        }
    }
    if (n > start) parse_line(buf + start, n - start);
}

/* The M10 card file; -1 if it cannot be read (no card, not mounted, absent). */
static int read_card(void) {
    int fd = GR2_FN_FILE_OPEN(CARD_PATH, FILE_READ);
    if (fd == 0) return -1;
    int n = GR2_FN_FILE_READ(fd, g_io, sizeof(g_io));
    GR2_FN_FILE_CLOSE(fd);
    return n;
}

int cfg_load(int card) {
    g_n = 0;
    g_dirty = 0;
    if (!S) S = GR2_FN_MEMORY_ALLOC(sizeof(cfg_store_t));
    if (!S) {
        ERR("config: Memory_Alloc failed, settings will not be saved");
        return 0;
    }
    int n = store_load(&g_file, g_io, sizeof(g_io));
    if (n >= 0) {
        parse_text(g_io, n);
        DBG_HEX2("config: D: (generation, entries)", g_file.gen, (uint32_t)g_n);
        return 1;
    }
    if (!card) { DBG("config: none on D:"); return 0; }
    n = read_card();
    if (n < 0) { DBG("config: none"); return 0; }
    parse_text(g_io, n);
    g_dirty = 1;                       /* the next cfg_save moves it to D: */
    DBG_HEX("config: imported " CARD_PATH ", entries", (uint32_t)g_n);
    return 1;
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

/* The config as text into g_io; returns its length. */
static uint32_t render(void) {
    char *p = g_io;
    for (int i = 0; i < g_n; i++) {
        p = put_str(p, g_cfg[i].key);
        *p++ = '=';
        p = put_hex32(p, g_cfg[i].value);
        *p++ = '\r';
        *p++ = '\n';
    }
    return (uint32_t)(p - g_io);
}

/* Backup for a wiped D:; best effort: no card (release) is not an error. */
static void write_card(uint32_t n) {
    int fd = GR2_FN_FILE_OPEN(CARD_PATH, FILE_WRITE);
    if (fd) {
        GR2_FN_FILE_WRITE(fd, g_io, n);
        GR2_FN_FILE_CLOSE(fd);
    }
}

void cfg_save(void) {
    if (!S || !g_dirty) return;
    uint32_t n = render();
    write_card(n);
    if (store_save(&g_file, g_io, n) != 0) {
        ERR("config: cannot write D:/CFW");
        return;
    }
    g_dirty = 0;
    DBG_HEX2("config: saved to D: (generation, entries)", g_file.gen, (uint32_t)g_n);
}

/* The task's late phase: rewrite the card copy if it is missing (e.g. a fresh tty/ folder) or differs from the
 * loaded config, so a wiped D: can always be restored from it. One small read per boot; no card: nothing. */
void cfg_sync_card(void) {
    if (!S || !g_n) return;
    uint32_t n = render();
    int m = -1, fd = GR2_FN_FILE_OPEN(CARD_PATH, FILE_READ);
    if (fd) {
        m = GR2_FN_FILE_READ(fd, S->card, sizeof(S->card));
        GR2_FN_FILE_CLOSE(fd);
    }
    int same = m == (int)n;
    for (uint32_t i = 0; same && i < n; i++) same = S->card[i] == g_io[i];
    if (same) return;
    write_card(n);
    DBG("config: card copy rewritten");
}
