# gr2_cfw: Ricoh GR II custom firmware (fw 3.00)
# Target: ARMv5TE (ARM946E-S-class, MPU), big-endian BE-32, Thumb-1 only, freestanding, no libgcc.
#
#   stage 1  gr2_cfw.bin   fixed in the window 0xA0017000-0xA001EFFF (gr2_boot.ld): loader + hook veneers
#   stage 2  gr2_main.bin  linked at 0 and at MAIN_HI; scripts/mkmain.py turns the difference into
#                          relocations, stage 1 relocates it into a heap block
# Variants from the same sources: release (CFW_DEBUG=0, ERROR log lines only), debug (CFW_DEBUG=1).
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
             src/overlay.c src/console.c src/mem.c
VARIANTS  := release debug
ELFS      := $(foreach v,$(VARIANTS),bin/$(v)/gr2_cfw.elf bin/$(v)/gr2_main.elf)

objs = $(patsubst src/%.S,build/$(1)/%.o,$(filter %.S,$(2))) \
       $(patsubst src/%.c,build/$(1)/%.o,$(filter %.c,$(2)))

.PHONY: all disasm audit dist fw emu loader flash-kit lint test check clean help

all: $(foreach v,$(VARIANTS),bin/$(v)/gr2_cfw.bin bin/$(v)/gr2_main.bin)

disasm: $(ELFS:.elf=.asm)

# The camera CPU is ARMv5TE with Thumb-1 only: reject any object built for something else.
audit: all
	@for e in $(ELFS); do \
	  $(READELF) -A $$e | grep -q 'Tag_CPU_arch: v5TE' || { echo "$$e: not ARMv5TE"; exit 1; }; \
	  $(READELF) -A $$e | grep -q 'Tag_THUMB_ISA_use: Thumb-1' || { echo "$$e: not Thumb-1"; exit 1; }; \
	done
	@$(SIZE) $(ELFS)

dist: audit loader
	$(PYTHON) scripts/package_sd.py

# Stock firmware inputs in fw/ (local rg2_v003.frm or the official download; never committed).
fw:
	$(PYTHON) scripts/fetch_firmware.py

# Unicorn tests of both variants against the stock APP (tests/emu/README.md). EMU_ARGS="--render DIR".
emu: all fw
	$(PYTHON) tests/emu/run.py $(EMU_ARGS)

# M10 debug-loader .frm: build + independent byte check and emulation (loader/flash/README.md).
loader: all fw
	$(PYTHON) scripts/build_loader_frm.py
	$(PYTHON) scripts/emu_loader.py

# One folder per M10 flash step with the exact card contents (dist/flash/, never published).
flash-kit: loader dist
	$(PYTHON) scripts/flash_kit.py

lint:
	$(RUFF) check scripts tests tools

test: lint audit emu loader

# pre-flight safety gates (stock .frm, NAND backup): required before any flash, see AGENTS.md.
check:
	$(PYTHON) the maintainer's safety check

clean:
	rm -rf build bin dist/flash
	rm -f dist/*.zip dist/*.frm dist/*.diff.txt dist/SHA256SUMS.txt dist/RELEASE_NOTES.md

help:
	@echo "make            build both variants (bin/<variant>/gr2_{cfw,main}.bin)"
	@echo "make dist       ISA audit + SD-card zips, SHA256SUMS.txt, RELEASE_NOTES.md in dist/"
	@echo "make fw         stock firmware inputs into fw/"
	@echo "make emu        emulator tests (EMU_ARGS=\"--render DIR\" for screen PNGs)"
	@echo "make loader     build + check dist/rg2_v003_loader.frm (never publish it)"
	@echo "make flash-kit  dist/flash/: one folder per M10 flash step with the card contents"
	@echo "make test       lint + audit + emu + loader (what CI runs)"
	@echo "make check      pre-flight safety gates before flashing"
	@echo "make disasm     bin/<variant>/*.asm"
	@echo "WERROR=1        treat warnings as errors (CI)"

define VARIANT_RULES
build/$(1)/%.o: src/%.S VERSION
	@mkdir -p $$(@D)
	$$(CC) $$(CFLAGS) -DCFW_DEBUG=$(2) -c $$< -o $$@

build/$(1)/%.o: src/%.c VERSION
	@mkdir -p $$(@D)
	$$(CC) $$(CFLAGS) -DCFW_DEBUG=$(2) -c $$< -o $$@

bin/$(1)/gr2_cfw.elf: $(call objs,$(1),$(BOOT_SRCS)) gr2_boot.ld
	@mkdir -p $$(@D)
	$$(CC) $$(LDFLAGS) -T gr2_boot.ld $(call objs,$(1),$(BOOT_SRCS)) -o $$@

bin/$(1)/gr2_cfw.bin: bin/$(1)/gr2_cfw.elf
	$$(OBJCOPY) -O binary $$< $$@

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

$(eval $(call VARIANT_RULES,release,0))
$(eval $(call VARIANT_RULES,debug,1))
