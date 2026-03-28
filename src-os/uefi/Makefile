UEFI_ENTRY := efi_main
EFI_OUT ?= ../build/BOOTX64.EFI
KERNEL_RAW ?= ../build/kernel.raw

BUILD_DIR := ../build

FAT_IMG ?= ../build/fat.img
EFI_IN ?=

CXX := clang++
UEFI_LD := $(or $(shell command -v lld-link 2>/dev/null),$(shell command -v ld.lld 2>/dev/null))

EFI_SRC := efi_main.cpp
EFI_OBJ := $(BUILD_DIR)/efi_main.obj

KERNEL_BLOB_INC := kernel_blob.inc

.PHONY: build image clean

build:
	@if [ -z "$(UEFI_LD)" ]; then echo "No UEFI linker found (tried: lld-link, ld.lld). Install lld."; exit 1; fi
	@mkdir -p "$(BUILD_DIR)"
	@python3 gen_kernel_blob.py "$(KERNEL_RAW)" "$(KERNEL_BLOB_INC)"
	$(CXX) \
		--target=x86_64-windows-gnu \
		-ffreestanding \
		-fno-exceptions -fno-rtti \
		-std=c++20 \
		-nostdlib \
		-c "$(EFI_SRC)" -o "$(EFI_OBJ)"
	@mkdir -p "$$(dirname "$(EFI_OUT)")"
	@if [ "$$(basename "$(UEFI_LD)")" = "ld.lld" ]; then \
		"$(UEFI_LD)" -flavor link \
			/entry:$(UEFI_ENTRY) \
			/subsystem:efi_application \
			/nodefaultlib \
			/nologo \
			/out:"$(EFI_OUT)" \
			"$(EFI_OBJ)"; \
	else \
		"$(UEFI_LD)" \
			/entry:$(UEFI_ENTRY) \
			/subsystem:efi_application \
			/nodefaultlib \
			/nologo \
			/out:"$(EFI_OUT)" \
			"$(EFI_OBJ)"; \
	fi

image:
	@mkdir -p "$$(dirname "$(FAT_IMG)")"
	@python3 make_fat16_image.py "$(FAT_IMG)" "$(EFI_IN)"

clean:
	$(RM) -f "$(EFI_OUT)" "$(EFI_OBJ)" "$(FAT_IMG)" "$(KERNEL_BLOB_INC)"

