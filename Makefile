# Makefile for Ricoh GR II Custom Firmware (gr2_cfw)
# Target Architecture: ARMv7-A Big-Endian (Thumb mode)
# Base Address: 0xA0700000

CROSS_COMPILE ?= arm-none-eabi-
CC            := $(CROSS_COMPILE)gcc
OBJCOPY       := $(CROSS_COMPILE)objcopy
OBJDUMP       := $(CROSS_COMPILE)objdump
SIZE          := $(CROSS_COMPILE)size

CFLAGS  := -mbig-endian -march=armv7-a -mthumb -mthumb-interwork \
           -nostdlib -fno-builtin -ffreestanding -O2 -Wall \
           -Iinclude

LDFLAGS := -mbig-endian -nostdlib -T gr2_cfw.ld -Wl,--build-id=none

SRCS    := src/entry.S src/main.c src/ui.c src/films.c src/hooks.c
OBJS    := $(patsubst src/%.S, build/%.o, $(filter %.S, $(SRCS))) \
           $(patsubst src/%.c, build/%.o, $(filter %.c, $(SRCS)))

OUT_ELF := bin/gr2_cfw.elf
OUT_BIN := bin/gr2_cfw.bin

.PHONY: all clean disasm

all: $(OUT_BIN)

build/%.o: src/%.S
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT_ELF): $(OBJS)
	@mkdir -p bin
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

$(OUT_BIN): $(OUT_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "Built: $@ ($$(stat -f%z $@ 2>/dev/null || stat -c%s $@) bytes)"

disasm: $(OUT_ELF)
	$(OBJDUMP) -d -M force-thumb $< > bin/gr2_cfw.asm

clean:
	rm -rf build bin
