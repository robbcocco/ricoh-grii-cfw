/* Linker script for gr2_cfw stage 1 (src/entry.S, src/boot.c, src/veneers.S;
 * include/boot.h). Stage 2 is linked by gr2_main.ld and loaded into the heap.
 * Window 0xA0017000-0xA001EFFF (32 KiB) in the IPL copy's erased 0xFF tail,
 * confirmed unused for a full session on hardware (M0 probe).
 * Architecture: ARMv5TE, big-endian (BE-32), Thumb-1 (+ ARM helpers).
 *
 * Layout:
 *   0xA0017000  .text + .data (loaded by fload)
 *   then        .bss (NOLOAD, zeroed by src/entry.S); must end below 0xA001D1D4
 *   0xA001D1D4  power-off veneer } fixed: low 12 bits match the stock BL
 *   0xA001DA80  IQ-ready veneer  } targets they replace (0x...1D4, 0x...A80,
 *   0xA001E07C  press veneer     } 0x...07C, 0x...6CE, 0x...EA0), see src/veneers.S
 *   0xA001E6CE  effect veneer    } (halation, src/halation.c; 2 mod 4)
 *   0xA001EEA0  hold veneer      }
 * The bin file runs to the end of the hold veneer (zeros in between).
 * __image_end__ = end of the hold veneer: the window canary (src/boot.c)
 * checks that the rest of the window is still 0xFF.
 */

OUTPUT_FORMAT("elf32-bigarm", "elf32-bigarm", "elf32-bigarm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0xA0017000, LENGTH = 0x8000
}

SECTIONS
{
    . = 0xA0017000;

    .text : {
        KEEP(*(.text.entry))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > RAM

    .data : {
        *(.data*)
        . = ALIGN(4);
    } > RAM

    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    ASSERT(__bss_end__ <= 0xA001D1D4, "code/data/bss overlap the hook veneers")

    .veneer_off   0xA001D1D4 : { KEEP(*(.veneer_off)) } > RAM
    .veneer_iq    0xA001DA80 : { KEEP(*(.veneer_iq)) } > RAM

    .veneer_press 0xA001E07C : { KEEP(*(.veneer_press)) } > RAM
    .veneer_fx    0xA001E6CE : { KEEP(*(.veneer_fx)) } > RAM
    .veneer_hold  0xA001EEA0 : { KEEP(*(.veneer_hold)) } > RAM
    __image_end__ = .;

    /* The effect veneer sits between press and hold, 2 mod 4, with its target word hand-aligned. */
    ASSERT((cfw_veneer_fx & ~1) == 0xA001E6CE, "effect veneer not at 0xA001E6CE")
    ASSERT(cfw_veneer_fx_target == 0xA001E6D4, "effect veneer target word not at 0xA001E6D4")
    ASSERT((cfw_veneer_fx & ~1) >= ADDR(.veneer_press) + SIZEOF(.veneer_press), "effect veneer overlaps the press veneer")
    ASSERT(cfw_veneer_fx_target + 4 <= ADDR(.veneer_hold), "effect veneer overlaps the hold veneer")

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame)
    }
}
