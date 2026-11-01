/* Key input while the CFW overlay is open (static RE: finding 17 §4).
 *
 * The APP task dispatches every system message through the function pointer
 * at 0xA04071F0 (stock 0xA039725F, signature int h(u32 *msg), msg = {id, p1, p2},
 * return non-zero = consumed). Replacing that pointer is a single aligned
 * 32-bit data store: no code patch, no I-cache concern.
 *
 * While the overlay is open, key messages (0x42NN) are queued for the CFW task
 * and consumed, except POWER and the shutter (passed through; the task closes
 * the overlay on a shutter press). A release is consumed only if its press was
 * consumed, so the Effect release belonging to the press that opened the menu
 * still reaches the stock HSM (otherwise its hold timer would fire). */
#include <stdint.h>
#include "cfw.h"
#include "cfw_log.h"
#include "gr2_hw.h"

#define MSG_HANDLER_SLOT ((volatile uint32_t *)0xA04071F0)
#define MSG_HANDLER_STOCK 0xA039725Fu
#define KEY_MSG_MASK     0xFF00u
#define KEY_MSG_BASE     0x4200u
#define KEY_POWER        0x4210u
#define KEY_SHUTTER      0x4201u

typedef int (*msg_handler_t)(uint32_t *msg, uint32_t a1, uint32_t a2, uint32_t a3);

#define QUEUE_LEN 16
static volatile uint32_t g_queue[QUEUE_LEN][2];  /* {id, p1} */
static volatile uint32_t g_head, g_tail;          /* producer: APP task, consumer: CFW task */
static volatile uint32_t g_capture;               /* set by the CFW task while the overlay is open */
static uint32_t g_pressed[2];                     /* bitmap of keys 0x4200..0x423F pressed while captured */

static int is_pressed(uint32_t k) { return (g_pressed[k >> 5] >> (k & 31)) & 1; }
static void set_pressed(uint32_t k, int on) {
    if (on) g_pressed[k >> 5] |= 1u << (k & 31);
    else g_pressed[k >> 5] &= ~(1u << (k & 31));
}

#if CFW_DEBUG
#define TRACE_LEN 32
static volatile uint32_t g_trace[TRACE_LEN][2];   /* last raw key messages {id, p1} */
static volatile uint32_t g_trace_n;
#endif

static int msg_hook(uint32_t *msg, uint32_t a1, uint32_t a2, uint32_t a3) {
    uint32_t id = msg[0];
#if CFW_DEBUG
    if ((id & ~0x3Fu) == KEY_MSG_BASE) {
        uint32_t t = g_trace_n++ % TRACE_LEN;
        g_trace[t][0] = id; g_trace[t][1] = msg[1];
    }
#endif
    if ((id & ~0x3Fu) == KEY_MSG_BASE && id != KEY_POWER) {
        uint32_t k = id & 0x3F, press = msg[1] != 0;
        /* While captured: take presses and the releases that belong to them.
         * After close: still swallow releases of presses we took. */
        int take = g_capture ? (press || is_pressed(k)) : (!press && is_pressed(k));
        if (take) {
            set_pressed(k, (int)press);
            uint32_t next = (g_head + 1) % QUEUE_LEN;
            if (g_capture && next != g_tail) {
                g_queue[g_head][0] = id;
                g_queue[g_head][1] = msg[1];
                g_head = next;
            }
            if (id != KEY_SHUTTER) return 1;   /* consumed */
        }
    }
    return ((msg_handler_t)MSG_HANDLER_STOCK)(msg, a1, a2, a3);
}

int cfw_input_install(void) {
    if (*MSG_HANDLER_SLOT != MSG_HANDLER_STOCK) {
        ERR_HEX("input: handler slot not stock, found", *MSG_HANDLER_SLOT);
        return -1;
    }
    *MSG_HANDLER_SLOT = (uint32_t)msg_hook;
    DBG_HEX("input: handler slot now", *MSG_HANDLER_SLOT);
    return 0;
}

void cfw_input_capture(int on) {
    if (on) g_tail = g_head;
    g_capture = (uint32_t)on;
}

void cfw_input_dump_trace(void) {
#if CFW_DEBUG
    uint32_t n = g_trace_n;
    cfw_log_hex("input: key messages seen", n);
    for (uint32_t i = n > TRACE_LEN ? n - TRACE_LEN : 0; i < n; i++) {
        uint32_t t = i % TRACE_LEN;
        cfw_log_hex2("  key (id, p1)", g_trace[t][0], g_trace[t][1]);
    }
#endif
}

int cfw_input_pop(uint32_t *id, uint32_t *state) {
    if (g_tail == g_head) return 0;
    *id = g_queue[g_tail][0];
    *state = g_queue[g_tail][1];
    g_tail = (g_tail + 1) % QUEUE_LEN;
    return 1;
}
