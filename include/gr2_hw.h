#ifndef GR2_HW_H
#define GR2_HW_H
#include <stdint.h>
#include <stddef.h>

/* Addresses verified statically against b01firm8 (rg2_v003, RAM = 0xA0020200 +
 * file offset); see finding 14 and 15. CPU: ARMv5TE, Thumb-1. */

/* ---- Memory ------------------------------------------------------------
 * DRAM 0xA0000000-0xBFFFFFFF (512 MiB, MPU region 5, cacheable write-through).
 * 0xA0000000-0xA001FFFF: IPL copy loaded from NAND block 0 (incl. its erased
 *   0xFF tail); referenced by the APP only through the RAM-map getter.
 * CFW window 0xA0017000-0xA001EFFF (32 KiB): erased-flash 0xFF in the live
 *   dump of a running APP (DUMP_IPL_256K) -> unused in that session. Confirm
 *   with loader/probe before loading code.
 * RTOS: code 0xA0020400..0xA05F9454, zero-init to ~0xA0797DFF, C heap
 *   0xA0798000-0xA0900000, free-memory pool 0xA0900000-0xBFE00000. */
#define GR2_CFW_WINDOW_BASE   0xA0017000u
#define GR2_CFW_WINDOW_SIZE   0x8000u
#define GR2_POOL_BUMP_PTR     ((volatile uint32_t *)0xA00C2BA4) /* next free byte of the free-memory pool */
#define GR2_POOL_GLOBALHEAP   ((volatile uint32_t *)0xA00C2BA8) /* GlobalHeap pool handle (Memory_Alloc) */

/* ---- RTOS functions (Thumb: bit 0 set) ---------------------------------- */
#define GR2_FN_SHELL_EXECUTE ((int (*)(const char *))0xA002A275)            /* run one shell command line */
#define GR2_FN_UART_PRINT    ((void (*)(const char *, ...))0xA0039CBD)      /* printf to UART */
#define GR2_FN_DELAY         ((void (*)(uint32_t))0xA0026837)               /* Task_Delay(ticks) */
#define GR2_FN_FILE_OPEN     ((int (*)(const char *, uint32_t))0xA002C70F)  /* FILE* or 0 */
#define GR2_FN_FILE_READ     ((int (*)(int, void *, uint32_t))0xA002CA99)
#define GR2_FN_FILE_WRITE    ((int (*)(int, const void *, uint32_t))0xA002CC4B) /* size or -1 */
#define GR2_FN_FILE_CLOSE    ((int (*)(int))0xA002C905)
#define GR2_FN_FILE_MKDIR    ((int (*)(const char *))0xA002E6ED)            /* 1 created, 0 exists, -1 error (finding 34) */
/* LoadFile(path, dst, max): bytes read = min(size, max) (a larger file is truncated silently) or -1.
 * On A: (the RES drive, read-only, no mount; finding 29 §3) two directory scans per call, where
 * File_Read costs two per call; on other drives File_Open/Read/Close. */
#define GR2_FN_LOAD_FILE     ((int (*)(const char *, void *, uint32_t))0xA002CD2D)
#define GR2_FN_PROP_GET      ((uint32_t (*)(uint32_t))0xA00E0E2F)           /* Prop_Get(id), runs the per-property getter hook */
#define GR2_FN_PROP_GET_RAW  ((uint32_t (*)(uint32_t))0xA00E0E81)           /* stored value, no getter hook */
#define GR2_FN_PROP_SET      ((void (*)(uint32_t, uint32_t))0xA00E0C95)     /* scalar setter, no range check */
#define GR2_FN_PROP_LOCK     ((void (*)(void))0xA00E127B)                   /* property-DB lock (efcset/nrset wrap writes) */
#define GR2_FN_PROP_UNLOCK   ((void (*)(void))0xA00E1287)
#define GR2_FN_MEMORY_ALLOC  ((void *(*)(uint32_t))0xA0024D6F)              /* GlobalHeap block */

/* Directory listing (File.c over PrFILE2; static RE, finding 19 §3).
 * FindFirst takes a directory ("H:/cfw/recipes", upper-case drive) and appends
 * the match-all wildcard itself (it cannot filter); returns a handle or 0.
 * FindNext returns 1 per entry, 0 at the end. Every non-zero handle must be
 * closed. "." and ".." are returned.
 * A: (finding 29 §3, stock code emulated): the pattern is a raw, case-sensitive
 * path prefix ("A:/cfw/recipes/"); name = the full path, sfn = its first 12 bytes,
 * attr 0x01, no directories, no "." / "..". */
typedef struct {
    char     name[0x200];          /* +0x000 long name (else 8.3), NUL-terminated */
    char     sfn[13];              /* +0x200 "NAME.EXT" 8.3 name */
    uint8_t  attr;                 /* +0x20D 0x10 = directory */
    uint8_t  pad[2];
    uint32_t size_hi, size_lo;     /* +0x210 */
    uint32_t size2_hi, size2_lo;   /* +0x218 */
    uint16_t fdate, ftime;         /* +0x220 FAT date/time */
    uint8_t  rest[0x0C];           /* +0x224 */
} gr2_find_t;
_Static_assert(sizeof(gr2_find_t) == 0x230, "gr2_find_t layout");
#define GR2_FIND_ATTR_DIR    0x10
#define GR2_FN_FIND_FIRST    ((void *(*)(const char *, gr2_find_t *))0xA002EF59)
#define GR2_FN_FIND_NEXT     ((int (*)(void *, gr2_find_t *))0xA002F355)
#define GR2_FN_FIND_CLOSE    ((void (*)(void *))0xA002FA3D)

/* ---- My Settings slot files (static RE + emulation, finding 19 §1) --------
 * A slot is B:/P_MY0k.bin: 16-byte header + 899 XOR-obfuscated u32 values.
 * Edit a slot without touching the live settings: clone the live DB object,
 * load the slot file into the clone, change values, save, free. */
#define GR2_LIVE_PDB         (*(void *volatile *)0xA0121C5Cu)
#define GR2_FN_PROP_PATH     ((const char *(*)(int, int))0xA00DDB69)          /* (1 = MY, k) -> "B:/P_MY0k.bin" */
#define GR2_FN_PDB_CLONE     ((void *(*)(void *))0xA0037E15)                  /* heap copy of the DB object */
#define GR2_FN_PDB_LOAD      ((void *(*)(void *, const char *, int))0xA0037A43) /* file -> values, else defaults */
#define GR2_FN_PDB_SAVE      ((void (*)(void *, const char *))0xA0037AE7)     /* XOR + checksum, create/truncate */
#define GR2_FN_PDB_FREE      ((void (*)(void *))0xA0037479)
#define GR2_PDB_VALUES(db)   (*(uint32_t **)((uint8_t *)(db) + 0x0C))         /* u32 per property ID */
/* Property setup table {u32 default, u32 change msg} per ID (finding 25 §3.2, the DB's +4): the
 * factory values (missing slot file, factory reset, Prop_Reset). */
#define GR2_PROP_DEFAULT(id) (((const uint32_t *)0xA0122A7Cu)[2 * (id)])
#define GR2_FN_PROP_SA_WRITE ((void (*)(uint32_t, uint32_t, const void *))0xA00E100F) /* multi-word prop write */
#define GR2_FN_UI_STATE      ((uint8_t *(*)(void))0xA00E5F21)                 /* +0x274 != 0: stock MY edit active */
#define GR2_PROPID_MY_NAME   0x1A9   /* live slot name: 0x44 bytes UTF-16BE in props 0x1A9..0x1B9 */
#define GR2_PROPID_MY_NAME_CACHE 0x1BA  /* +k: pointer to slot k's 0x44-byte name buffer (menus) */
#define GR2_PROPID_MY_LINK   0x1C3   /* +k: 0 = slot not linked to a My Settings Box entry */
#define GR2_MY_NAME_BYTES    0x44

/* ---- Image-quality data (RAM copy of A:/isp/IQ.bin, loaded at boot) --------
 * ctx = *0xA015A430; IQ = *(ctx + 0x158 + 4 * (s8)ctx[0x2C]) (bank, -1 = not
 * loaded). Sections, lists and records: finding 20 §B; src/look.c. */
#define GR2_IQ_CTX           (*(volatile uint32_t *)0xA015A430u)
/* Live view reprograms the colour stages from IQ RAM only when its lookup
 * inputs change: task ISP-Pre (FUN_a013BAE6) compares them with a cache under
 * the ISP mutex. Flipping the cached tone-curve entry and EV value forces a full
 * reprogram (matrix, curves, YCbCr, hue) on the next frame (finding 20 §A). */
#define GR2_ISP_MUTEX        (*(void *volatile *)(0xA0155BBCu + 8))
#define GR2_FN_MUTEX_LOCK    ((int (*)(void *, int))0xA0027361)   /* (mutex, timeout -1) -> 0 ok, -1 */
#define GR2_FN_MUTEX_UNLOCK  ((void (*)(void *))0xA00273A5)
#define GR2_LV_CACHE_CURVE   (*(volatile uint16_t *)0xA06C8B9Au)  /* cached list pos. 12 (tone curve) */
#define GR2_LV_CACHE_EV      (*(volatile uint32_t *)0xA0155D20u)  /* cached EV compensation */

/* File_Open flags: 0x09 = append ("a"), 0x01 = write/truncate ("w") */
#define GR2_FILE_APPEND      0x09

/* Task_Create(entry, arg, priority, stack_bytes, name) -> task id ("Task_Create"/RTOS.c assert).
 * On failure its assert parks the caller forever, so it never returns 0 (finding 26 §2.5).
 * Stock callers use priority 0x96 and 0x3000-0x4000 byte stacks (telnetd: 0x96, 0x3000);
 * 0x96 shares FRM's level, CPU-heavy work belongs at 0xC0-0xD0 (finding 26 §3.3). */
#define GR2_FN_TASK_CREATE   ((int (*)(void (*)(uint32_t), uint32_t, uint32_t, uint32_t, const char *))0xA0026709)
/* List_Append(list, data) -> list head; allocates a 12-byte node {data, next, prev}. */
#define GR2_FN_LIST_APPEND   ((void *(*)(void *, void *))0xA0054497)
/* System_GetTime() -> low word of the 64-bit OS system time (get_tim; variable 0xA00C22C8).
 * Unit: ms (verified on hardware: Task_Delay(1000) advances it by ~1007). */
#define GR2_FN_SYSTEM_GETTIME ((uint32_t (*)(void))0xA0027D97)

/* ---- Info / Developer tab sources (finding 21, static) ------------------- */
#define GR2_INV_BASE         0xA06BDD08u    /* RAM copy of B:/Invent.bin */
#define GR2_INV_LOADED       (*(volatile uint32_t *)(GR2_INV_BASE + 0xE4))
#define GR2_INV_U32(id)      (*(volatile uint32_t *)(GR2_INV_BASE + 0x40 + 4 * (id)))   /* ids 3..0x25 */
#define GR2_INV_SHOTS        0x24           /* images stored */
#define GR2_INV_EXPOSURES    0x25           /* exposures with the shutter */
#define GR2_INV_POWER_ONS    0x23
#define GR2_FN_ADC_READ      ((void (*)(uint32_t, uint32_t *))0xA003A8DB)   /* (ch, &raw); task context */
#define GR2_ADC_BATTERY      6
#define GR2_FN_POWER_TEMP    ((int (*)(uint32_t))0xA00CF607)   /* 0 lens, 1 sensor, 2 Wi-Fi -> (degC + 273) * 8 */
#define GR2_PROPID_BATTERY_LEVEL 0x24A      /* 0 empty .. 3 full */
#define GR2_PROPID_APP_VERSION   0x234      /* top bytes "%X.%02X" */
#define GR2_PROPID_SERIAL        0x244      /* BCD, "%08X" */
#define GR2_PROPID_CARD_TOTAL_HI 0x2DC      /* bytes: 0x2DC/0x2DD total, 0x2DE/0x2DF free (hi, lo) */
#define GR2_PROPID_SHOTS_LEFT    0x2D9
#define GR2_PROPID_WIFI          0x1E8      /* RAM-only on/off request */
#define GR2_PROPID_WIFI_SSID     0x1E9      /* 0x21 bytes */
#define GR2_PROPID_WIFI_PASS     0x1F2      /* 0x41 bytes */
#define GR2_PROPID_WIFI_STATUS   0x260      /* 0 off, 1 AP up, 2 client connected */
#define GR2_FN_PROP_SA_READ  ((void (*)(uint32_t, uint32_t, void *))0xA00E0FFF)   /* (id, bytes, buf) */
#define GR2_FN_MSG_POST      ((void (*)(uint32_t, uint32_t, uint32_t))0xA00361E1)
#define GR2_MSG_WIFI         0x116          /* (on, 0): JOBID_WIFI */
#define GR2_TELNETD_ENTRY    0xA00504D5     /* telnetd task body; no stock guard against a second start */
#define GR2_POOL_BUMP        (*(volatile uint32_t *)0xA00C2BA4u)   /* next free pool address */
#define GR2_POOL_LIMIT       0xBFDC0000u
#define GR2_LV_DISP_ADDR     (*(volatile uint32_t *)0xA0155BE8u)   /* shown LV frame: planar YUV 4:2:2 */
#define GR2_LV_DISP_MODE     (*(volatile uint8_t *)0xA0155B44u)    /* 1 = LCD 640x480 */
#define GR2_LV_ASPECT        (*(volatile uint8_t *)0xA0155B48u)    /* 0 16:9 (LV y 60-419), 2 3:2 (y 28-451), 3 1:1, else 4:3 */
#define GR2_UNCACHED(a)      ((((uint32_t)(a)) & 0x1FFFFFFFu) | 0x20000000u)
/* Boot state (findings 26 §5, 32). JobSystem_Boot sets Ctrl()+0x54+4*step = 1 after each boot
 * step; step 13 only queues Startup.ttl. MODE_PROPS_FINAL is set once, at the end of the block
 * after step 2 that writes the mode props (5, 6) and loads the active My Settings slot. */
#define GR2_BOOT_STEP_DONE(s) (*(volatile uint32_t *)(GR2_FN_UI_STATE() + 0x54 + 4 * (s)))
#define GR2_MODE_PROPS_FINAL (*(volatile uint32_t *)0xA012A0ECu)
#define GR2_LENS_MOTION_ID   (*(volatile uint8_t *)(0xA06EFD9C + 3))   /* lens script: 0x2B extend, 0x2E retract, 0 idle */
/* RTC user status register OPSR (finding 30 §1.3, R2a [HW]): bits 0-1 stock, bits 2-3 free and kept across power-off and
 * battery pulls. Section-0 wrappers, usable from the first loader hook; both are no-ops (get = 0) while the RTC is
 * invalid. The clean-power-off flag is OPSR bit 0 as stage 0 saved it: 1 if the last session ended through
 * JOBID_POWER_OFF (prop 0x258 holds a copy). */
#define GR2_FN_OPSR_GETB     ((int (*)(uint32_t))0xA003480D)              /* (bit 0..3) -> 0/1 */
#define GR2_FN_OPSR_SETB     ((void (*)(uint32_t, uint32_t))0xA0034531)   /* (bit 0..3, 0/1), read-modify-write */
#define GR2_LAST_OFF_CLEAN   (*(volatile uint32_t *)0xA00C22DCu)
#define GR2_PROPID_UI_MODE   0x001u                                         /* 2/3: powered on with PLAY (finding 22) */

/* ---- Developer console (finding 21 §3.2/§5; call sites in the telnetd task 0xA00504D4) ---- */
#define GR2_FN_SOCKET        ((int (*)(int, int, int))0xA0022A87)                 /* (2, 1, 0) -> fd, <0 error */
#define GR2_FN_BIND          ((int (*)(int, const void *, int))0xA00228F7)       /* (fd, sockaddr, 16) */
#define GR2_FN_LISTEN        ((int (*)(int, int))0xA0022A0F)
#define GR2_FN_ACCEPT        ((int (*)(int, void *, int *))0xA00228CF)            /* (fd, sockaddr, &len) */
#define GR2_FN_RECV          ((int (*)(int, void *, int, int))0xA0022A37)        /* bytes, 0, or <0 */
#define GR2_FN_SEND          ((int (*)(int, const void *, int, int))0xA0022A5F)
#define GR2_FN_SOCK_CLOSE    ((int (*)(int))0xA0022B91)
/* sockaddr_in as telnetd builds it: u8 len 0x10, u8 family 2, u16 port (BE), u32 addr 0, 8 zero bytes. */
#define GR2_CONSOLE_CTX      0xA05FCCD0u     /* +0 console mutex, +0x10 format buffer, +0x44 hook */
#define GR2_CONSOLE_HOOK_SLOT (*(void (*volatile *)(const char *))(GR2_CONSOLE_CTX + 0x44))
#define GR2_FN_CONSOLE_HOOK  ((void (*)(void (*)(const char *)))0xA0039CB7)      /* set the slot; printf then calls only it */
#define GR2_FN_SHELL_RESULT  ((int (*)(char *))0xA002A719)   /* (buf[0x100]): 0 = copied the last command's result text, -1 = none */
/* Kernel (ARM state: call through a pointer without bit 0; USR callers go through svc #4). */
#define GR2_FN_REF_TSK       ((int (*)(int, void *))0xA006A2A0)                   /* <0 = no such task */
#define GR2_FN_REF_MPL       ((int (*)(int, void *))0xA0067B00)                   /* variable pool status */
#define GR2_TSK_OBJ_TABLE    ((void *const volatile *)0xA05FB884u)   /* [id] -> {'TSK ', ., stack top, stack size, char *name} */
#define GR2_MPL_TABLE        0xA05FA9CCu     /* + id * 0x1C: {'MPL ', u8 type, ., blk, size, used max, s16 id, char *name} */

/* ---- Shell command registry -------------------------------------------
 * Context 0xA05FC308: +0x04 list of command records, +0x08 command count
 * (lookup in 0xA0029C90 walks `count` records, strncmp 32 on the name).
 * Record: char name[32] + handler at +0x20, called as handler(argc, argv). */
#define GR2_SHELL_CTX        0xA05FC308u
#define GR2_SHELL_CTX_LIST   ((void * volatile *)(GR2_SHELL_CTX + 0x04))
#define GR2_SHELL_CTX_COUNT  ((volatile int32_t *)(GR2_SHELL_CTX + 0x08))
typedef struct {
    char name[32];
    int (*handler)(int argc, char **argv);
} gr2_shell_cmd_t;

/* ---- Properties (name table 0xA0121C6C) --------------------------------- */
#define GR2_PROPID_SHOOTING_MODE      0x005  /* 1 Auto, 2 P, 3 Av, 4 Tv, 5 TAv, 6 M, 7 Movie */
#define GR2_PROPID_MYSETTING          0x006  /* active My Settings slot, 0 = none */
#define GR2_PROPID_SNAP_FOCAL_LEN     0x029  /* snap distance, 1..6 = 1m,1.5m,2m,2.5m,5m,inf */
#define GR2_PROPID_COLOR_MODE         0x02D  /* getter: EFFECT (0x2B) if set, else IMAGE_SETTINGS (0x2C) */
#define GR2_PROPID_COLOR_MODE_CLARITY 0x0BE  /* array[24] by colour mode: element i is prop 0xBE+i */
#define GR2_PROPID_CLARITY_16         0x0CE  /* Effect "Clarity" strength 1..9, 5 = none, default 7 */
#define GR2_PROPID_NOISE_REDUCTION    0x0EF  /* 0 Off, 1 Weak, 2 Medium, 3 Strong, 4 Auto, 5 Manual */
#define GR2_PROPID_NR_ISO             0x0F0  /* Manual NR: Sv where weak / medium / strong start (0xF0..0xF2) */
#define GR2_PROPID_ISO_AUTO_SV_MAX    0x111  /* ISO Auto-High max, Sv 0x120..0x270 */
#define GR2_PROPID_ISO_AUTO_CHANGE_TV 0x113  /* ISO Auto-High slowest shutter: 0x989681 Auto or Tv 0x30..0x180 */
#define GR2_PROPID_EFX_BTN            0x1A6  /* Effect Button Setting, 32 stock values */
#define GR2_PROPID_EFX_BTN_CONT       0x1A7  /* Effect Button Hold Setting: 0 Wi-Fi, 1 Aperture Preview */
#define GR2_PROPID_LANGUAGE           0x1E4  /* 0 = Japanese */
#define GR2_PROPID_DEVEL_MODE         0x22D  /* Startup.ttl boot gate */

/* Sv (ISO) encoding: 0xF0 + 16 per 1/3 stop (ISO 100 = 0xF0, 25600 = 0x270);
 * 0x2D0 = OFF in NR thresholds. Tv: 48 per stop, 0 = 1 s. */
#define GR2_SV_ISO100   0xF0u
#define GR2_SV_OFF      0x2D0u
#define GR2_TV_AUTO     0x989681u

/* ---- Strings: per-language tables of UTF-16BE pointers, index = ID - 1 ---- */
#define GR2_LANG_TABLES ((volatile uint32_t *)0xA05909EC)  /* 21 tables, by Prop(0x1E4) */

/* ---- Menu records (0x40 B each, array 0xA04330D8, record = base + idx*0x40) --
 * +0x00 title strID, +0x04 property ID, +0x08 row-availability callback,
 * +0x0C u16 count1, +0x10 list1 (12 B {value, 0, strID}),
 * +0x14 u16 count2, +0x18 list2 (24 B rows {strID, 0, 0x04000000, 0, enabled, 1}),
 * +0x1C u16 cursor, +0x30/+0x34/+0x38 optional get/set/display hooks,
 * +0x3C optional per-value predicate f(prop, value).
 * Fn1 idx 0xFA (0x1A4), Fn2 0xFB (0x1A5), Effect 0xFC (0x1A6), Flash 0xFD (0x37E),
 * Effect hold 0xFE (0x1A7). String resolver: 1..0x34F -> language table
 * 0xA05909EC[Prop(0x1E4)] (English 0xA04F6D58), 0x70000..0x70067 -> RAM 0xA077EFFC. */

#endif /* GR2_HW_H */
