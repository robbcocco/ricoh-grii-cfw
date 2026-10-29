#ifndef GR2_HW_H
#define GR2_HW_H
#include <stdint.h>
#include <stddef.h>

/* Display & Framebuffer Geometry */
#define GR2_SCREEN_WIDTH   640
#define GR2_SCREEN_HEIGHT  480

/* Memory Map Constants */
#define GR2_DRAM_BASE      0xA0000000
#define GR2_RTOS_BASE      0xA0020200
#define GR2_CFW_BASE       0xA0A00000

/* Known RTOS Function Pointers (All Thumb mode: bit 0 MUST be set!) */
#define GR2_FN_DISPATCH_CMD ((int (*)(const char *))0xA002A275)
#define GR2_FN_UART_PRINT   ((void (*)(const char *, ...))0xA0039CBD)
#define GR2_FN_DELAY_MS     ((void (*)(uint32_t))0xA0026837)
#define GR2_FN_FILE_OPEN    ((int (*)(const char *, uint32_t))0xA002C70F)
#define GR2_FN_FILE_WRITE   ((int (*)(int, const void *, uint32_t))0xA002CC4B)
#define GR2_FN_FILE_CLOSE   ((int (*)(int))0xA002C905)

/* On-Screen Menu Text Targets (UTF-16 BE) */
#define GR2_ADDR_NAME_CROSS   ((uint16_t *)0xA04F06A6)  /* 28 bytes */
#define GR2_ADDR_NAME_POSI    ((uint16_t *)0xA04F06DE)  /* 28 bytes */
#define GR2_ADDR_NAME_BLEACH  ((uint16_t *)0xA04F06FA)  /* 28 bytes */
#define GR2_ADDR_NAME_RETRO   ((uint16_t *)0xA04F0732)  /* 12 bytes */
#define GR2_ADDR_NAME_VIGN    ((uint16_t *)0xA04F089E)  /* 22 bytes */
#define GR2_ADDR_EFX_HOLD_TEXT ((uint16_t *)0xA04F3522) /* 34 bytes (was Aperture Preview) */

/* Button Dispatcher & State Machine Hooks */
#define GR2_BUTTON_TABLE_BASE 0xA038A4A0
#define GR2_ADDR_EFX_HOLD_HOOK ((uint32_t *)0xA03E9984) /* State action vector in DRAM */
#define GR2_ORIG_EFX_HOLD_FN  ((void (*)(void))0xA0391793)
#define GR2_FN_PROP_GET       ((uint32_t (*)(uint32_t, uint32_t))0xA00E1151)
#endif /* GR2_HW_H */
