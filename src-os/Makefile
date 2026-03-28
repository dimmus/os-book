PROJECT_DIR := .

BUILD_DIR := build
FAT_IMG := $(BUILD_DIR)/fat.img
RUN_IMG_PREFIX := $(BUILD_DIR)/fat.run

EFI_OUT := $(BUILD_DIR)/BOOTX64.EFI
KERNEL_OUT_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_OUT_RAW := $(BUILD_DIR)/kernel.raw

# QEMU / OVMF configuration
QEMU ?= qemu-system-x86_64
# OVMF "4m" images are commonly installed on Arch/edk2 packages.
OVMF_FD ?= /usr/share/edk2/x64/OVMF.4m.fd

QEMU_MACHINE ?= q35
QEMU_MEM ?= 512M
QEMU_SMP ?= 4
FAT_IMG_ABS := $(abspath $(FAT_IMG))
RUN_IMG_PREFIX_ABS := $(abspath $(RUN_IMG_PREFIX))

.PHONY: build image run run-clean stop-qemu clean

build:
	$(MAKE) -C kernel build KERNEL_ELF="$(abspath $(KERNEL_OUT_ELF))" KERNEL_RAW="$(abspath $(KERNEL_OUT_RAW))"
	$(MAKE) -C uefi build EFI_OUT="$(abspath $(EFI_OUT))" KERNEL_RAW="$(abspath $(KERNEL_OUT_RAW))"

image: build
	$(MAKE) -C uefi image FAT_IMG="$(abspath $(FAT_IMG))" EFI_IN="$(abspath $(EFI_OUT))"

run: image
	@if [ ! -f "$(OVMF_FD)" ]; then \
		echo "OVMF_FD not found: $(OVMF_FD)"; \
		echo "Set OVMF_FD to your local OVMF.fd path."; \
		exit 1; \
	fi
	@RUN_IMG="$(RUN_IMG_PREFIX_ABS).$$(date +%s).$$$$.img"; \
	cp -f "$(FAT_IMG_ABS)" "$$RUN_IMG"; \
	echo "Running QEMU with $$RUN_IMG"; \
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-no-reboot -no-shutdown \
		-m $(QEMU_MEM) \
		-smp $(QEMU_SMP) \
		-bios "$(OVMF_FD)" \
		-drive "file=$$RUN_IMG,format=raw" \
		-serial stdio \
		-display none

stop-qemu:
	@PIDS="$$(ps -eo pid=,comm=,args= | awk -v imgA="$(FAT_IMG_ABS)" -v runPrefix="$(RUN_IMG_PREFIX_ABS)." '$$2=="qemu-system-x86_64" && (index($$0, "file=" imgA ",format=raw") || index($$0, "file=" runPrefix)) {print $$1}')"; \
	if [ -n "$$PIDS" ]; then \
		echo "Stopping QEMU using project disk image(s): $$PIDS"; \
		kill $$PIDS; \
		sleep 1; \
	else \
		echo "No QEMU instance found for this project image"; \
	fi

run-clean: stop-qemu run

clean:
	$(RM) -r "$(BUILD_DIR)"
	$(RM) -f "$(RUN_IMG_PREFIX)."*.img
	$(MAKE) -C uefi clean || true
	$(MAKE) -C kernel clean || true

