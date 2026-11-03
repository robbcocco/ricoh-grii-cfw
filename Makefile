# gr2_cfw: Ricoh GR II custom firmware (fw 3.00)
# Target: ARMv5TE (ARM946E-S-class, MPU), big-endian BE-32, Thumb-1 only, freestanding, no libgcc.
#
#   stage 1  gr2_cfw.bin   fixed in the window 0xA0017000-0xA001EFFF (gr2_boot.ld): loader + hook veneers;
#                          scripts/stage1_crc.py writes its length + CRC-32 self-check fields (+12/+16)
#   stage 2  gr2_main.bin  linked at 0 and at MAIN_HI; scripts/mkmain.py turns the difference into
#                          relocations, stage 1 relocates it into a heap block
# Variants from the same sources: release (CFW_DEBUG=0, ERROR log lines only), debug (CFW_DEBUG=1),
# rom (like release, plus CFW_ROM=1: the CFW in the release firmware's A: drive, M11).
#
# `make help` lists the targets.

CROSS_COMPILE ?= arm-none-eabi-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
READELF := $(CROSS_COMPILE)readelf
SIZE    := $(CROSS_COMPILE)size
PYTHON  ?= python3
RUFF    ?= $(shell command -v ruff 2>/dev/null || echo uvx ruff)

VERSION := $(shell tr -d '[:space:]' < VERSION)

CFLAGS  := -mbig-endian -march=armv5te -mthumb -mthumb-interwork \
           -nostdlib -fno-builtin -ffreestanding -fno-tree-loop-distribute-patterns -fno-jump-tables \
           -Os -Wall -Wextra $(if $(WERROR),-Werror) \
           -Iinclude -DCFW_VERSION='"$(VERSION)"' -MMD -MP
LDFLAGS := -mbig-endian -nostdlib -Wl,--build-id=none -Wl,--no-warn-rwx-segments
MAIN_HI := 0x10000000

BOOT_SRCS := src/entry.S src/veneers.S src/boot.c src/log.c src/mem.c
MAIN_SRCS := src/arm.S src/main.c src/log.c src/shell.c src/task.c src/menu.c src/hooks.c src/ui.c \
             src/osd.c src/input.c src/config.c src/settings.c src/recipes.c src/look.c src/info.c \
             src/overlay.c src/console.c src/mem.c src/store.c src/export.c
VARIANTS  := release debug rom
ELFS      := $(foreach v,$(VARIANTS),bin/$(v)/gr2_cfw.elf bin/$(v)/gr2_main.elf)

objs = $(patsubst src/%.S,build/$(1)/%.o,$(filter %.S,$(2))) \
       $(patsubst src/%.c,build/$(1)/%.o,$(filter %.c,$(2)))

.PHONY: all disasm audit dist fw emu loader rom flash-kit lint test clean help

all: $(foreach v,$(VARIANTS),bin/$(v)/gr2_cfw.bin bin/$(v)/gr2_main.bin)

disasm: $(ELFS:.elf=.asm)

# The camera CPU is ARMv5TE with Thumb-1 only: reject any object built for something else.
audit: all
	@for e in $(ELFS); do \
	  $(READELF) -A $$e | grep -q 'Tag_CPU_arch: v5TE' || { echo "$$e: not ARMv5TE"; exit 1; }; \
	  $(READELF) -A $$e | grep -q 'Tag_THUMB_ISA_use: Thumb-1' || { echo "$$e: not Thumb-1"; exit 1; }; \
	done
	@$(SIZE) $(ELFS)

# Official zip (release firmware + recipes) and the debug zip (debug loader + debug CFW), scripts/package_sd.py.
dist: audit loader rom
	$(PYTHON) scripts/package_sd.py

# Stock firmware inputs in fw/ (local rg2_v003.frm or the official download; never committed).
fw:
	$(PYTHON) scripts/fetch_firmware.py

# Unicorn tests of all variants against the stock APP (tests/emu/README.md). EMU_ARGS="--render DIR".
emu: all fw
	$(PYTHON) tests/emu/run.py $(EMU_ARGS)

# M10 debug-loader .frm: build + independent byte check and emulation (loader/flash/README.md).
loader: all fw
	$(PYTHON) scripts/build_loader_frm.py
	$(PYTHON) scripts/emu_loader.py

# M11 release firmware: dist/rg2_v003_cfw.frm + manifest (stock 3.00 + rom stub + bin/rom and the
# A: files in RES), then the independent byte check and stock-code emulation (loader/flash/README.md §5).
rom: all fw
	$(PYTHON) scripts/build_cfw_frm.py
	$(PYTHON) scripts/emu_release.py

# One folder per flash step (M10 F, M11 P) with the exact card contents (dist/flash/, never published).
flash-kit: loader dist rom
	$(PYTHON) scripts/flash_kit.py

lint:
	$(RUFF) check scripts tests tools

test: lint audit emu loader rom

clean:
	rm -rf build bin dist/flash
	rm -f dist/*.zip dist/*.frm dist/*.diff.txt dist/*.manifest.txt dist/SHA256SUMS.txt dist/RELEASE_NOTES.md

help:
	@echo "make            build all variants: release, debug, rom (bin/<variant>/gr2_{cfw,main}.bin)"
	@echo "make dist       ISA audit + loader + rom, then dist/gr2_cfw-v<VERSION>.zip (release) and -debug.zip,"
	@echo "                SHA256SUMS.txt, RELEASE_NOTES.md"
	@echo "make fw         stock firmware inputs into fw/"
	@echo "make emu        emulator tests (EMU_ARGS=\"--render DIR\" for screen PNGs)"
	@echo "make loader     build + check dist/rg2_v003_loader.frm (debug loader, in the debug zip)"
	@echo "make rom        build + check dist/rg2_v003_cfw.frm, the release firmware (in the official zip)"
	@echo "make flash-kit  dist/flash/: one folder per flash step (F, P) with the card contents"
	@echo "make test       lint + audit + emu + loader + rom (what CI runs)"
	@echo "make disasm     bin/<variant>/*.asm"
	@echo "WERROR=1        treat warnings as errors (CI)"

define VARIANT_RULES
build/$(1)/%.o: src/%.S VERSION
	@mkdir -p $$(@D)
	$$(CC) $$(CFLAGS) -DCFW_DEBUG=$(2) -DCFW_ROM=$(3) -c $$< -o $$@

build/$(1)/%.o: src/%.c VERSION
	@mkdir -p $$(@D)
	$$(CC) $$(CFLAGS) -DCFW_DEBUG=$(2) -DCFW_ROM=$(3) -c $$< -o $$@

bin/$(1)/gr2_cfw.elf: $(call objs,$(1),$(BOOT_SRCS)) gr2_boot.ld
	@mkdir -p $$(@D)
	$$(CC) $$(LDFLAGS) -T gr2_boot.ld $(call objs,$(1),$(BOOT_SRCS)) -o $$@

bin/$(1)/gr2_cfw.bin: bin/$(1)/gr2_cfw.elf scripts/stage1_crc.py
	$$(OBJCOPY) -O binary $$< $$@
	$$(PYTHON) scripts/stage1_crc.py $$@

bin/$(1)/gr2_main.elf: $(call objs,$(1),$(MAIN_SRCS)) gr2_main.ld
	@mkdir -p $$(@D)
	$$(CC) $$(LDFLAGS) -T gr2_main.ld $(call objs,$(1),$(MAIN_SRCS)) -o $$@

bin/$(1)/gr2_main_hi.elf: $(call objs,$(1),$(MAIN_SRCS)) gr2_main.ld
	$$(CC) $$(LDFLAGS) -T gr2_main.ld -Wl,--defsym=MAIN_BASE=$(MAIN_HI) $(call objs,$(1),$(MAIN_SRCS)) -o $$@

bin/$(1)/gr2_main.bin: bin/$(1)/gr2_main.elf bin/$(1)/gr2_main_hi.elf scripts/mkmain.py
	$$(PYTHON) scripts/mkmain.py $$< bin/$(1)/gr2_main_hi.elf $(MAIN_HI) $$@

bin/$(1)/%.asm: bin/$(1)/%.elf
	$$(OBJDUMP) -d -S $$< > $$@

-include $(patsubst %.o,%.d,$(call objs,$(1),$(BOOT_SRCS) $(MAIN_SRCS)))
endef

$(eval $(call VARIANT_RULES,release,0,0))
$(eval $(call VARIANT_RULES,debug,1,0))
$(eval $(call VARIANT_RULES,rom,0,1))
