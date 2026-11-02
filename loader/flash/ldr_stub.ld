/* Places the two flashed-loader patches (ldr_stub.S debug loader, rom_stub.S release
 * firmware: same sections and symbol) at their APP addresses.
 * Used only by scripts/build_loader_frm.py and scripts/build_cfw_frm.py. */
OUTPUT_FORMAT("elf32-bigarm", "elf32-bigarm", "elf32-bigarm")
OUTPUT_ARCH(arm)
ENTRY(ldr_stub)

SECTIONS
{
    .ldr_hook 0xA0022E5C : { KEEP(*(.ldr_hook)) }
    .ldr_stub 0xA00AFCF0 : { KEEP(*(.ldr_stub)) }
    ASSERT(SIZEOF(.ldr_hook) == 4, "hook must be one 4-byte BL")
    ASSERT(SIZEOF(.ldr_stub) == 0x84, "stub must fill exactly the 132-byte free range")

    /DISCARD/ : { *(.text) *(.data) *(.bss) *(.ARM.attributes) *(.comment) }
}
