# Makefile for Ricoh GR II Custom Firmware (gr2_cfw)
# Target: ARMv5TE (ARM946E-S-class, MPU, Thumb-1 only), big-endian (BE-32)
#
# Two stages (include/boot.h):
#   stage 1  gr2_cfw.bin   fixed in the window 0xA0017000-0xA001EFFF (gr2_boot.ld): loader + hook veneers
#   stage 2  gr2_main.bin  everything else, linked at 0 (gr2_main.ld), relocated into the heap at load;
#                          scripts/mkmain.py builds it from two links at different bases
# Two variants from the same sources:
#   release  CFW_DEBUG=0: only ERROR lines in H:/cfw/cfw_log.txt
#   debug    CFW_DEBUG=1: every log line, key trace, `cfw status` dump
# Outputs: bin/<variant>/gr2_{cfw,main}.{elf,bin,asm}; `make dist` zips both variants as SD-card trees.

CROSS_COMPILE ?= arm-none-eabi-
CC            := $(CROSS_COMPILE)gcc
OBJCOPY       := $(CROSS_COMPILE)objcopy
OBJDUMP       := $(CROSS_COMPILE)objdump
SIZE          := $(CROSS_COMPILE)size

VERSION := $(shell tr -d '[:space:]' < VERSION)

CFLAGS  := -mbig-endian -march=armv5te -mthumb -mthumb-interwork \
           -nostdlib -fno-builtin -ffreestanding -fno-tree-loop-distribute-patterns -fno-jump-tables -Os -Wall \
           -Iinclude -DCFW_VERSION='"$(VERSION)"' -MMD -MP

LDFLAGS := -mbig-endian -nostdlib -Wl,--build-id=none -Wl,--no-warn-rwx-segments
# Second link base of stage 2; scripts/mkmain.py diffs the two links into relocations.
MAIN_HI := 0x10000000

BOOT_SRCS := src/entry.S src/veneers.S src/boot.c src/log.c src/mem.c
MAIN_SRCS := src/arm.S src/main.c src/log.c src/shell.c src/task.c src/menu.c src/hooks.c src/ui.c \
             src/osd.c src/input.c src/config.c src/settings.c src/recipes.c src/look.c src/info.c \
             src/overlay.c src/console.c src/mem.c
VARIANTS  := release debug

# $(1) = variant, $(2) = source list
objs = $(patsubst src/%.S,build/$(1)/%.o,$(filter %.S,$(2))) \
       $(patsubst src/%.c,build/$(1)/%.o,$(filter %.c,$(2)))

.PHONY: all clean disasm dist check emu

all: $(foreach v,$(VARIANTS),bin/$(v)/gr2_cfw.bin bin/$(v)/gr2_main.bin)

disasm: $(foreach v,$(VARIANTS),bin/$(v)/gr2_cfw.asm bin/$(v)/gr2_main.asm)

dist: all
	python3 scripts/package_sd.py

check:
	@python3 the maintainer's safety check

# Unicorn smoke test of both variants (tests/emu/README.md); skips without the firmware image.
# EMU_ARGS: e.g. --render /tmp/emu
emu: all
	python3 tests/emu/run.py $(EMU_ARGS)

# $(1) = variant, $(2) = CFW_DEBUG value. Objects depend on VERSION (CFW_VERSION define).
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
	$$(SIZE) $$@

bin/$(1)/gr2_cfw.bin: bin/$(1)/gr2_cfw.elf
	$$(OBJCOPY) -O binary $$< $$@
	@echo "Built: $$@ ($$$$(wc -c < $$@ | tr -d ' ') bytes, stage 1, $(1), v$(VERSION))"

bin/$(1)/gr2_main.elf: $(call objs,$(1),$(MAIN_SRCS)) gr2_main.ld
	@mkdir -p $$(@D)
	$$(CC) $$(LDFLAGS) -T gr2_main.ld $(call objs,$(1),$(MAIN_SRCS)) -o $$@
	$$(SIZE) $$@

bin/$(1)/gr2_main_hi.elf: $(call objs,$(1),$(MAIN_SRCS)) gr2_main.ld
	$$(CC) $$(LDFLAGS) -T gr2_main.ld -Wl,--defsym=MAIN_BASE=$(MAIN_HI) $(call objs,$(1),$(MAIN_SRCS)) -o $$@

bin/$(1)/gr2_main.bin: bin/$(1)/gr2_main.elf bin/$(1)/gr2_main_hi.elf scripts/mkmain.py
	python3 scripts/mkmain.py $$< bin/$(1)/gr2_main_hi.elf $(MAIN_HI) $$@
	@echo "Built: $$@ ($$$$(wc -c < $$@ | tr -d ' ') bytes, stage 2, $(1), v$(VERSION))"

bin/$(1)/%.asm: bin/$(1)/%.elf
	$$(OBJDUMP) -d -S $$< > $$@

-include $(patsubst %.o,%.d,$(call objs,$(1),$(BOOT_SRCS) $(MAIN_SRCS)))
endef

$(eval $(call VARIANT_RULES,release,0))
$(eval $(call VARIANT_RULES,debug,1))

clean:
	rm -rf build bin dist/*.zip
