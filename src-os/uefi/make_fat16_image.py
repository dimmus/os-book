#!/usr/bin/env python3

import math
import os
import struct
import sys


def u16(x: int) -> bytes:
    return struct.pack("<H", x & 0xFFFF)


def u32(x: int) -> bytes:
    return struct.pack("<I", x & 0xFFFFFFFF)


def make_dir_entry(name_8_3: str, attr: int, fst_clus: int, size: int = 0) -> bytes:
    # name_8_3: "BOOTX64.EFI" or "EFI" or "BOOT"
    name_8_3 = name_8_3.upper()
    if "." in name_8_3:
        base, ext = name_8_3.split(".", 1)
    else:
        base, ext = name_8_3, ""
    base = (base + " " * 8)[:8]
    ext = (ext + " " * 3)[:3]
    name = (base + ext).encode("ascii")

    # FAT directory entry is 32 bytes.
    # We keep timestamps zero for simplicity.
    # Fields:
    #   0..10 name (11)
    #   11 attr (1)
    #   12 ntres (1)
    #   13 crtTenth (1)
    #   14..21 crtTime/crtDate (8)
    #   22..23 lstAccDate (2)
    #   24..25 fstClusHI (2)
    #   26..27 wrtTime (2)
    #   28..29 wrtDate (2)
    #   30..31 fstClusLO (2)
    #   28..31 fileSize (4) overlaps wrtDate in some layouts; but official layout is fixed.
    # We'll pack properly with the exact FAT16 layout.
    return (
        name
        + struct.pack("<B", attr)  # attr
        + b"\x00"  # ntRes
        + b"\x00"  # crtTimeTenth
        + u16(0)  # crtTime
        + u16(0)  # crtDate
        + u16(0)  # lstAccDate
        + u16(0)  # fstClusHI
        + u16(0)  # wrtTime
        + u16(0)  # wrtDate
        + u16(fst_clus)  # fstClusLO
        + u32(size)  # fileSize
    )


def align_up(x: int, a: int) -> int:
    return (x + a - 1) // a * a


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: make_fat16_image.py <out.fat.img> <efi_in>", file=sys.stderr)
        return 2

    out_img = sys.argv[1]
    efi_in = sys.argv[2]
    efi_data = open(efi_in, "rb").read()

    # FAT16 parameters (simple, deterministic).
    bytes_per_sector = 512
    sectors_per_cluster = 1
    reserved_sectors = 1
    fat_count = 2
    root_entries = 64
    root_dir_sectors = (root_entries * 32 + bytes_per_sector - 1) // bytes_per_sector

    total_sectors = 32768  # 16MiB image (FAT16, small enough)

    # Iteratively compute FAT size.
    cluster_count = 0
    fat_sectors = 0
    # Initial guess.
    fat_sectors = 1
    for _ in range(20):
        cluster_count = total_sectors - reserved_sectors - root_dir_sectors - fat_count * fat_sectors
        fat_sectors_new = math.ceil(((cluster_count + 2) * 2) / bytes_per_sector)  # FAT16: 2 bytes/entry
        if fat_sectors_new == fat_sectors:
            break
        fat_sectors = fat_sectors_new

    if cluster_count < 10:
        raise RuntimeError(f"cluster_count too small: {cluster_count}")

    data_sectors = total_sectors - reserved_sectors - root_dir_sectors - fat_count * fat_sectors
    if data_sectors < 1:
        raise RuntimeError("invalid FAT geometry")

    data_start_lba = reserved_sectors + fat_count * fat_sectors + root_dir_sectors

    def cluster_to_lba(clus: int) -> int:
        # clusters start at 2.
        return data_start_lba + (clus - 2) * sectors_per_cluster

    # Allocate clusters:
    #   2: EFI directory
    #   3: BOOT directory
    #   4..: file data
    efi_dir_clus = 2
    boot_dir_clus = 3
    file_first_clus = 4

    file_clusters_needed = math.ceil(len(efi_data) / (bytes_per_sector * sectors_per_cluster))
    if file_clusters_needed <= 0:
        file_clusters_needed = 1

    if file_first_clus + file_clusters_needed >= 0xFFF0:
        raise RuntimeError("file requires too many clusters for FAT16 demo")

    # Build boot sector.
    bs = bytearray(512)
    bs[0:3] = b"\xEB\x3C\x90"  # jmp short
    # OEM ID is exactly 8 bytes in the FAT boot sector.
    bs[3:11] = b"HELLOINIT"[:8]
    # BPB fields:
    bs[11:13] = struct.pack("<H", bytes_per_sector)
    bs[13] = sectors_per_cluster
    bs[14:16] = struct.pack("<H", reserved_sectors)
    bs[16] = fat_count
    bs[17:19] = struct.pack("<H", root_entries)
    bs[19:21] = struct.pack("<H", total_sectors if total_sectors < 0x10000 else 0)
    bs[21] = 0xF8  # media descriptor
    bs[22:24] = struct.pack("<H", fat_sectors)
    bs[24:26] = struct.pack("<H", 63)  # sectors per track
    bs[26:28] = struct.pack("<H", 255)  # number of heads
    bs[28:32] = struct.pack("<I", 0)  # hidden sectors
    bs[32:36] = struct.pack("<I", 0 if total_sectors < 0x10000 else total_sectors)
    # Extended BPB:
    bs[36] = 0  # drive number
    bs[37] = 0  # reserved
    bs[38] = 0x29  # boot signature
    bs[39:43] = struct.pack("<I", 0x12345678)  # volume ID
    bs[43:54] = b"HELLOINITOS"[:11]  # volume label (11 bytes)
    fs_type = b"FAT16   "  # 8 bytes
    bs[54:62] = fs_type
    bs[62:510] = b"\x00" * (510 - 62)
    bs[510:512] = b"\x55\xAA"

    # FAT tables.
    fat_bytes = fat_sectors * bytes_per_sector
    fat1 = bytearray(fat_bytes)
    fat2 = bytearray(fat_bytes)

    def set_fat_entry(clus: int, val: int):
        # FAT16 entry offset = clus*2
        off = clus * 2
        fat1[off:off + 2] = u16(val)
        fat2[off:off + 2] = u16(val)

    # Reserved entries.
    set_fat_entry(0, 0xFFF8)
    set_fat_entry(1, 0xFFFF)

    EOC = 0xFFFF
    # EFI dir single cluster
    set_fat_entry(efi_dir_clus, EOC)
    # BOOT dir single cluster
    set_fat_entry(boot_dir_clus, EOC)

    # File cluster chain
    for i in range(file_clusters_needed):
        clus = file_first_clus + i
        if i + 1 == file_clusters_needed:
            set_fat_entry(clus, EOC)
        else:
            set_fat_entry(clus, clus + 1)

    # Root directory (fixed).
    root = bytearray(root_dir_sectors * bytes_per_sector)
    # Entry 0: EFI directory at cluster 2
    root[0:32] = make_dir_entry("EFI", attr=0x10, fst_clus=efi_dir_clus, size=0)
    # Remaining entries are 0x00 => end-of-list.

    # Data area clusters.
    data = bytearray(data_sectors * bytes_per_sector)

    def write_cluster(clus: int, payload: bytes):
        lba = cluster_to_lba(clus)
        sector_index = lba - data_start_lba
        assert 0 <= sector_index < data_sectors
        start = sector_index * bytes_per_sector
        data[start:start + len(payload)] = payload

    # EFI directory cluster: contains BOOT directory entry.
    efidir = bytearray(bytes_per_sector)
    efidir[0:32] = make_dir_entry("BOOT", attr=0x10, fst_clus=boot_dir_clus, size=0)
    data_index = (efi_dir_clus - 2) * sectors_per_cluster
    write_cluster(efi_dir_clus, bytes(efidir))

    # BOOT directory cluster: contains BOOTX64.EFI file entry.
    bootdir = bytearray(bytes_per_sector)
    bootdir[0:32] = make_dir_entry(
        "BOOTX64.EFI",
        attr=0x20,  # archive
        fst_clus=file_first_clus,
        size=len(efi_data),
    )
    write_cluster(boot_dir_clus, bytes(bootdir))

    # File data clusters.
    cluster_bytes = bytes_per_sector * sectors_per_cluster
    for i in range(file_clusters_needed):
        clus = file_first_clus + i
        start = i * cluster_bytes
        chunk = efi_data[start:start + cluster_bytes]
        if len(chunk) < cluster_bytes:
            chunk = chunk + b"\x00" * (cluster_bytes - len(chunk))
        write_cluster(clus, chunk)

    # Assemble image.
    img = bytearray()
    img += bytes(bs)
    img += bytes(fat1)
    img += bytes(fat2)
    img += bytes(root)
    img += bytes(data)

    # Sanity: exact size.
    # Some Python bytearray slice operations can cause off-by-one resizing;
    # QEMU/OVMF only cares about the geometry in the BPB, so we
    # trim/pad to match the BPB-reported size.
    expected_size = total_sectors * bytes_per_sector
    if len(img) != expected_size:
        # Keep the beginning intact (where the BPB + FAT live).
        if len(img) > expected_size:
            img = img[:expected_size]
        else:
            img += b"\x00" * (expected_size - len(img))

    os.makedirs(os.path.dirname(out_img), exist_ok=True)
    with open(out_img, "wb") as f:
        f.write(img)

    print(out_img)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

