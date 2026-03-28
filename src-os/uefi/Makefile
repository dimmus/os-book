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

# Regenerate the include whenever the raw kernel changes, and force a UEFI
# recompile: `efi_main.obj` must be newer than `kernel_blob.inc` or the
# embedded blob can stay stale (common reason for missing Stage 2 output).
$(KERNEL_BLOB_INC): gen_kernel_blob.py $(KERNEL_RAW)
	@if [ ! -f "$(KERNEL_RAW)" ]; then echo "Missing $(KERNEL_RAW); run kernel build first."; exit 1; fi
	@python3 gen_kernel_blob.py "$(KERNEL_RAW)" "$(KERNEL_BLOB_INC)"

$(EFI_OBJ): $(EFI_SRC) $(KERNEL_BLOB_INC)
	@mkdir -p "$(BUILD_DIR)"
	$(CXX) \
		--target=x86_64-windows-gnu \
		-ffreestanding \
		-fno-exceptions -fno-rtti \
		-std=c++20 \
		-nostdlib \
		-c "$(EFI_SRC)" -o "$(EFI_OBJ)"

$(EFI_OUT): $(EFI_OBJ)
	@if [ -z "$(UEFI_LD)" ]; then echo "No UEFI linker found (tried: lld-link, ld.lld). Install lld."; exit 1; fi
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

build: $(EFI_OUT)

image:
	@mkdir -p "$$(dirname "$(FAT_IMG)")"
	@python3 make_fat16_image.py "$(FAT_IMG)" "$(EFI_IN)"

clean:
	$(RM) -f "$(EFI_OUT)" "$(EFI_OBJ)" "$(FAT_IMG)" "$(KERNEL_BLOB_INC)"

