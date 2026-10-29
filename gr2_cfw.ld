/* Linker script for Ricoh GR II Custom Firmware (gr2_cfw)
 * Target memory base: 0xA0A00000 (Safe high SDRAM, above Main Prog 0xA0900000)
 * Architecture: ARM Big-Endian (armv7-a)
 */

OUTPUT_FORMAT("elf32-bigarm", "elf32-bigarm", "elf32-bigarm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0xA0A00000, LENGTH = 2M
}

SECTIONS
{
    . = 0xA0A00000;

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
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame)
    }
}
