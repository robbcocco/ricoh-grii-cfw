/* Linker script for gr2_cfw stage 2 (include/boot.h). Linked at MAIN_BASE
 * (0 for the shipped image; scripts/mkmain.py links a second copy at another
 * base and turns the words that differ into the relocation table). Stage 1
 * loads it into the heap, adds the load address to those words and zeroes .bss.
 * Code must be position-independent apart from absolute 32-bit words
 * (literal pools, pointer tables): Thumb BL/B are PC-relative. */

OUTPUT_FORMAT("elf32-bigarm", "elf32-bigarm", "elf32-bigarm")
OUTPUT_ARCH(arm)
ENTRY(cfw_main)

SECTIONS
{
    . = DEFINED(MAIN_BASE) ? MAIN_BASE : 0;
    __main_start__ = .;

    .text : {
        KEEP(*(.text.main))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    }

    .data : {
        *(.data*)
        . = ALIGN(8);   /* .bss of the -O2 object (src/halation.c) is 8-byte aligned: it must follow the image */
    }
    __image_end__ = .;

    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame)
        *(.ARM.exidx*)
    }
}
