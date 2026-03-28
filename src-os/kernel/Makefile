KERNEL_ELF ?= ../build/kernel.elf
KERNEL_RAW ?= ../build/kernel.raw

BUILD_DIR := ../build

CLANG := clang++
LD := $(or $(shell command -v ld.lld 2>/dev/null),$(shell command -v lld 2>/dev/null),$(shell command -v ld 2>/dev/null))
OBJCOPY := llvm-objcopy

KERNEL_OBJ := $(BUILD_DIR)/kernel_init.o
KERNEL_SRC := init.cpp
KERNEL_LD := ld.ld

CLANG_TARGET ?= x86_64-unknown-elf

.PHONY: build clean

build:
	@if [ -z "$(LD)" ]; then echo "No linker found (tried: ld.lld, lld, ld)"; exit 1; fi
	@mkdir -p "$(BUILD_DIR)"
	$(CLANG) \
		--target="$(CLANG_TARGET)" \
		-ffreestanding \
		-fno-exceptions -fno-rtti \
		-nostdlib \
		-O2 -Wall -Wextra -pedantic \
		-c "$(KERNEL_SRC)" -o "$(BUILD_DIR)/kernel_init.o"
	$(LD) -T "$(KERNEL_LD)" -o "$(KERNEL_ELF)" $(KERNEL_OBJ)
	$(OBJCOPY) -O binary "$(KERNEL_ELF)" "$(KERNEL_RAW)"

clean:
	$(RM) -f "$(KERNEL_ELF)" "$(KERNEL_RAW)" "$(KERNEL_OBJ)"
