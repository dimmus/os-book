# OS-0 — Introduction: disk image, FAT, and the UEFI boot path (`src-os/`)

This chapter is **not** a numbered boot stage in the kernel. It explains **how the tutorial presents a UEFI application to QEMU/OVMF** before you read [OS-1](OS-1-HelloInit-UEFI-boot-and-serial-study.md): the **`make_fat16_image.py`** tool, **why** the repo uses a small **FAT16** image, and how that relates to **real PCs**.

---

## What building `src-os` produces (and what it is *not*)

Running `make build` / `make image` / `make run` from [`src-os/`](src-os/) creates several **different kinds** of file. They are **not** all “an executable you double-click,” and this tree does **not** build an **ISO 9660** optical-disc image.

| Output (under `src-os/build/` unless noted) | What kind of file | Purpose |
| --- | --- | --- |
| **`kernel.elf`** | **ELF** (Executable and Linkable Format), 64-bit freestanding | The **linked** kernel: sections (`.text`, `.bss`, …), symbols, debug info. Used as the **source** for `objcopy` and for debugging. **Firmware does not load this file directly.** |
| **`kernel.raw`** | **Raw binary** (no ELF header) | Produced by `llvm-objcopy -O binary` from `kernel.elf`, then padded by `pad_kernel_raw.py` so `.bss` is sized correctly. A **flat blob of bytes** at the kernel’s linked VMA layout. **Still not loaded by the firmware** in this tutorial—see below. |
| **`uefi/kernel_blob.inc`** | **Generated C++ source** (not in `build/`) | `gen_kernel_blob.py` turns `kernel.raw` into a **`static const uint8_t kernel_blob[]`** + length. **Included** into the UEFI app so the kernel bytes live **inside** the UEFI image. |
| **`BOOTX64.EFI`** | **PE32+** portable executable, **UEFI application** subsystem | The **only** thing here that UEFI **executes as a program**: `lld-link` / `subsystem:efi_application`, entry `efi_main`. It **embeds** the kernel data from `kernel_blob.inc`. |
| **`fat.img`** | **Raw disk image** (sector dump) | A **FAT16** filesystem in a file; **not** ELF, not PE, not ISO. Attached to QEMU as `-drive file=...,format=raw`. |

**Not produced here:** **`.iso`** (CD/DVD layout), **GPT disk images** with multiple partitions, **kernel as a separate file** on the FAT volume. Other tutorials might load `kernel.raw` from disk; in **`src-os`**, the kernel is **only** carried inside **`BOOTX64.EFI`** as embedded bytes.

---

## Is the disk image “like an external drive”?

**Conceptually, yes.** `fat.img` is a **sequence of 512-byte sectors**—the same abstraction as a **USB stick** or **internal disk** presented to firmware as a **block device**. QEMU’s `-drive file=fat.img,format=raw` tells the emulator: “pretend this file is a **removable** (or fixed) disk; read/write LBA 0, 1, 2, …”

So:

- It is **not** a special “external drive” file format—just **raw sectors**.
- It **is** the same **idea** as **imaging** a USB drive to a `.img` file with `dd` (except here the image is **generated** by Python instead of copied from hardware).

---

## How does the computer know what to do with these files?

Different actors understand **different** pieces:

| Stage | Who reads what | What happens |
| --- | --- | --- |
| **1. Firmware** | `fat.img` as a **disk** | **UEFI** (OVMF in QEMU) runs **before** your OS. It understands **partitions** (not used in this superfloppy image) or a **FAT volume** starting at LBA 0, follows the **UEFI default path** for removable media, and **loads one file**: **`EFI\BOOT\BOOTX64.EFI`**. |
| **2. UEFI loader** | **`BOOTX64.EFI`** (PE format) | The firmware’s PE/COFF loader maps your app into memory and jumps to **`efi_main`**. From here you still have **Boot Services** (allocate pages, memory map, `ExitBootServices`, …). |
| **3. Your UEFI app** | **Embedded `kernel_blob[]`** in the `.efi` | The code in `efi_main.cpp` **does not** read `kernel.raw` from FAT. It copies **`kernel_blob[]`** (compiled from `kernel.raw`) into **allocated RAM**, then jumps to that address after `ExitBootServices`. |
| **4. Kernel** | N/A (runs from RAM) | The CPU executes **`kernel.raw`** bytes **at the physical address** you chose—your tutorial’s **kernel entry**. |

So: **the computer “knows” what to do** because **UEFI is standardized**: FAT + path `\EFI\BOOT\BOOTX64.EFI` + PE32+ entry point. **Nothing** in the hardware automatically runs `kernel.elf` or `kernel.raw`; only **`BOOTX64.EFI`** is loaded by firmware in this setup, and **your** C++ code copies the embedded kernel into place.

---

## What `src-os/uefi/make_fat16_image.py` does (in detail)

The script is a **minimal, from-scratch formatter**: it does **not** call `mkfs` or rely on the host kernel. It writes raw bytes that form a valid **FAT16** volume so firmware can read a file from a **disk image**.

**Inputs (command line):**

```text
python3 make_fat16_image.py <out.fat.img> <efi_in>
```

| Argument | Role |
| --- | --- |
| **`<out.fat.img>`** | Path of the output file (e.g. `build/fat.img`). This becomes the **raw disk image** attached to QEMU (`-drive file=...,format=raw`). |
| **`<efi_in>`** | Path to a **PE/COFF UEFI application** (binary). The script reads it with `open(efi_in, "rb")` and stores its contents in the data area. In this tree, that is always the **built** loader produced by the UEFI link step—see [§ `efi_in` and `BOOTX64.EFI`](#what-efi_in-means-and-where-bootx64efi-comes-from). |

**High-level pipeline inside the script:**

1. **Read** the entire UEFI binary into memory (`efi_data`).
2. **Choose geometry** (fixed, simple, deterministic):
   - 512 bytes/sector, 1 sector/cluster, 2 FAT copies, 64 root directory entries, **32768** sectors total → **16 MiB** image.
   - Iteratively compute how many sectors the **FAT** itself needs (FAT16 uses **2 bytes per cluster** in the FAT table), so the layout stays self-consistent.
3. **Build the boot sector (first 512 bytes)** with a valid **BIOS Parameter Block (BPB)**:
   - Jump + OEM name (`HELLOINIT`), sector/cluster counts, media byte `0xF8`, label `HELLOINITOS`, filesystem type string **`FAT16`**, ending **`0x55 0xAA`**.
   - This is **not** an x86 boot loader for the tutorial kernel; it is the **Volume Boot Record** expected by FAT so tools and firmware can parse the volume.
4. **Allocate clusters** (cluster numbers start at **2** in FAT):
   - Cluster **2**: directory **`EFI`** (subdirectory).
   - Cluster **3**: directory **`BOOT`** (inside the EFI tree).
   - Clusters **4+**: **file data** for the UEFI app, chained in the FAT (last cluster marked end-of-chain `0xFFFF`).
5. **Fill the File Allocation Tables** (two identical copies): mark reserved clusters, directory clusters, and the file’s cluster chain.
6. **Root directory**: one entry for **`EFI`** (directory attribute `0x10`), pointing to cluster 2.
7. **Data area**:
   - In cluster 2’s sector: a directory listing for **`BOOT`** → cluster 3.
   - In cluster 3’s sector: a **32-byte directory entry** for **`BOOTX64.EFI`** (file attribute `0x20`), first cluster = start of file data, size = `len(efi_data)`.
   - Following clusters: raw bytes of **`efi_in`** (padded with zeros to cluster size).
8. **Concatenate** in order: **boot sector ∥ FAT1 ∥ FAT2 ∥ root directory ∥ data**, then trim/pad to exactly `total_sectors * 512` bytes.

**Important:** the image is a **single FAT volume starting at LBA 0**. There is **no MBR** and **no GPT** partition table in this file. It is the “**superfloppy**” style: the first sector is the FAT **VBR**, not a partition map. That keeps the generator small and matches “attach this whole file as one raw disk” in QEMU.

---

## Why FAT16 (and not FAT32, ext4, …)?

- **UEFI firmware expects a FAT-family filesystem** on the **EFI System Partition (ESP)** when it loads boot loaders from removable or fixed media. The **UEFI specification** describes access via **EFI_SIMPLE_FILE_SYSTEM_PROTOCOL** to FAT volumes. So using **FAT** in the tutorial matches how real UEFI machines discover `\EFI\BOOT\BOOTX64.EFI`.
- **FAT16** is enough here: the image is tiny (16 MiB), the file is one bootloader, and **long filenames (LFN)** are not required. The script only implements **short 8.3 names** (see below).
- **FAT32** would be normal on larger ESPs today; **FAT16** was chosen for **simplicity** (smaller FAT entries are not the limiting factor at this size; the code path is just “FAT16 demo geometry”).
- **ext4 / NTFS** are irrelevant for the **firmware’s default** search path on the ESP: those are not what UEFI’s standard boot discovery uses for the default removable path.

So: **FAT16 is a deliberate simplification**, not a claim that retail PCs use FAT16 for everything—they usually use **FAT32** on the ESP, but the **same conceptual layout** applies: FAT volume + `\EFI\BOOT\` + architecture-specific file names.

---

## Is this “mimicking a solid-state drive”?

**Not specifically.** The output file is a **raw sector image** (`format=raw` in QEMU). QEMU exposes it as a **block device** (like a small disk or USB stick image). Whether the real machine uses an **SSD**, **NVMe**, or **USB** does not change the **file format** of the image: the firmware still sees **sectors** and a **FAT** filesystem.

So: it **mimics a small disk** (or partitionless removable volume), not “SSD physics.” **TRIM**, **NVMe queues**, etc. are **not** part of this tutorial.

---

## What do common modern PCs use?

Typical **x86_64 UEFI** boot from internal storage:

- **GPT** partitioning on the disk.
- An **EFI System Partition** (ESP), formatted **FAT32** (almost always), mounted at `/EFI/...` from the firmware’s point of view.
- Boot entries in **NVRAM** pointing to `\EFI\<vendor>\bootx64.efi` or similar, or the **default removable path** `\EFI\BOOT\BOOTX64.EFI` when you boot from USB.

This tutorial **skips GPT** and builds one **FAT image** that is the **whole** virtual disk, to reduce moving parts. The **path inside FAT** (`EFI/BOOT/BOOTX64.EFI`) still follows the **same convention** firmware looks for on removable media.

---

## Can you inspect the directory/file layout of `fat.img`?

Yes. The file is a normal **FAT16** volume (no partition offset).

**Linux (loop mount):**

```bash
sudo mkdir -p /mnt/fat16
sudo mount -o loop build/fat.img /mnt/fat16   # from src-os/ after `make image`
ls -R /mnt/fat16
# Expect: EFI/BOOT/BOOTX64.EFI
sudo umount /mnt/fat16
```

If `mount` complains, specify the type explicitly:

```bash
sudo mount -t vfat -o loop build/fat.img /mnt/fat16
```

**Read-only tools:** `dosfsck -v build/fat.img`, `file build/fat.img`, or a hex editor to see the boot sector and FAT (advanced).

**QEMU:** you can also mount the same image in a helper VM; for quick checks, **loop mount on the host** is enough.

---

## What does **8.3** / **`name_8_3`** mean?

**8.3** is the legacy **short filename** format from MS-DOS:

- Up to **8** characters for the **base name**, then **`.`**, then up to **3** characters for the **extension**.
- Stored in directory entries as **11 bytes**: 8 + 3, **space-padded**, **ASCII uppercase** in this script (`make_dir_entry` uppercases and pads).

Examples:

| Logical name | Stored 11 bytes (conceptually) |
| --- | --- |
| `BOOTX64.EFI` | `BOOTX64` + `EFI` |
| `EFI` (directory) | `EFI` + 8 spaces in base? Actually script uses base `EFI` padded to 8 + ext empty → `EFI     ` + `   ` |
| `BOOT` | `BOOT` padded + ext spaces |

**Long filenames** (LFN) use **additional** directory slots with a special attribute; **this script does not implement LFN**, only classic 8.3 entries—enough for `EFI`, `BOOT`, and `BOOTX64.EFI`.

---

## What `efi_in` means, and where **`BOOTX64.EFI`** comes from

### The second argument `efi_in`

In `make_fat16_image.py`:

```python
efi_in = sys.argv[2]
efi_data = open(efi_in, "rb").read()
```

So **`efi_in`** is simply the **filesystem path** to the UEFI **application binary** to embed in the image under `\EFI\BOOT\BOOTX64.EFI`.

### What the top-level `Makefile` passes

From `src-os/Makefile`:

```make
image: build
	$(MAKE) -C uefi image FAT_IMG="$(abspath $(FAT_IMG))" EFI_IN="$(abspath $(EFI_OUT))"
```

And:

```make
EFI_OUT := $(BUILD_DIR)/BOOTX64.EFI
```

So **`EFI_IN` is always `$(BUILD_DIR)/BOOTX64.EFI`** (i.e. `build/BOOTX64.EFI` when built from `src-os/`), **after** the UEFI project has been linked.

### How `build/BOOTX64.EFI` is produced

From `src-os/uefi/Makefile`:

- `clang++` compiles `efi_main.cpp` (with embedded `kernel_blob.inc`) to `efi_main.obj`.
- **`lld-link`** (or `ld.lld` in MS link flavor) links it as a **UEFI application**:
  - `/subsystem:efi_application`
  - entry symbol `efi_main`
  - output **`BOOTX64.EFI`** (default `EFI_OUT ?= ../build/BOOTX64.EFI` relative to `uefi/`).

So **`BOOTX64.EFI` is your compiled UEFI program**—not something downloaded. The **name** matches the **UEFI default** for **64-bit x86** removable boot:

### Why the file is named **`BOOTX64.EFI`**

UEFI defines a **default boot file** path for removable media on **x86_64**:

```text
\EFI\BOOT\BOOTX64.EFI
```

- **`BOOT`** directory: generic fallback when there is no vendor-specific path.
- **`BOOTX64.EFI`**: **64-bit x86** PE32+ UEFI image (`IA32` would use another name, e.g. `BOOTIA32.EFI`).

Firmware (including **OVMF** in QEMU) scans FAT volumes on bootable devices and loads this path when appropriate. The Python script **creates** that exact tree so OVMF finds your app **without** custom NVRAM entries.

---

## How this connects to the rest of the book

| Topic | Where to read next |
| --- | --- |
| UEFI entry, serial, memory map, `ExitBootServices`, kernel jump | [OS-1](OS-1-HelloInit-UEFI-boot-and-serial-study.md), [OS-2](OS-2-ExitBootServices-and-kernel-jump-study.md) |
| Kernel build, `kernel.raw`, `kernel_blob.inc` | [`src-os/README.md`](src-os/README.md) |
| Roadmap after hello-init | [OS-3](OS-3-From-hello-init-to-real-OS-next-milestones-study.md) |

---

## Summary

- **`src-os` build outputs:** **`kernel.elf`** (ELF, for link/debug), **`kernel.raw`** (flat binary for embedding), **`kernel_blob.inc`** (generated array), **`BOOTX64.EFI`** (PE UEFI app that **firmware runs** and that **embeds** the kernel), **`fat.img`** (raw FAT16 disk for QEMU). **No ISO**; the kernel is **not** a separate file on the FAT in this tree.
- **Disk image ≈ small USB/disk** in behavior: raw sectors attached as a **block device**; firmware only needs **FAT** + **`\EFI\BOOT\BOOTX64.EFI`** per UEFI rules.
- **`make_fat16_image.py`** builds a **16 MiB raw FAT16 image** with **VBR + 2×FAT + root + data**, placing **`efi_in`** at **`\EFI\BOOT\BOOTX64.EFI`** using **8.3** directory entries only.
- **`efi_in`** in practice is **`build/BOOTX64.EFI`**, the **linked output** of the tutorial UEFI app.
- **FAT16** keeps the generator simple; real ESPs are usually **FAT32**, but the **path convention** is the same.
- The image is a **raw block device**, not a model of SSD internals; you can **loop-mount** it on Linux to see files.
- **8.3** names are the classic **8 + 3** short filename format; the script pads and uppercases them to match FAT directory entry layout.
