#!/usr/bin/env python3
"""Pad llvm-objcopy -O binary output to PT_LOAD MemSiz (includes .bss).

llvm-objcopy copies at most FileSiz bytes per segment; .bss is NOBITS so the
tail of the in-memory image is dropped. The UEFI loader and kernel size math
must see the full image through the end of .bss (e.g. g_idt).
"""
from __future__ import annotations

import struct
import sys


def _u16(b: bytes, off: int) -> int:
    return struct.unpack_from("<H", b, off)[0]


def _u32(b: bytes, off: int) -> int:
    return struct.unpack_from("<I", b, off)[0]


def _u64(b: bytes, off: int) -> int:
    return struct.unpack_from("<Q", b, off)[0]


def max_load_vaddr_plus_memsz(elf: bytes) -> int:
    if elf[:4] != b"\x7fELF":
        raise SystemExit("not an ELF file")
    ei_class = elf[4]
    if ei_class != 2:
        raise SystemExit("expected ELFCLASS64")
    e_phoff = _u64(elf, 32)
    e_phentsize = _u16(elf, 54)
    e_phnum = _u16(elf, 56)
    if e_phentsize < 56:
        raise SystemExit("bad e_phentsize")

    pt_load = 1
    end = 0
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type = _u32(elf, o)
        if p_type != pt_load:
            continue
        p_vaddr = _u64(elf, o + 16)
        p_memsz = _u64(elf, o + 40)
        cand = p_vaddr + p_memsz
        if cand > end:
            end = cand
    if end == 0:
        raise SystemExit("no PT_LOAD segments")
    return end


def main() -> None:
    if len(sys.argv) != 3:
        print("usage: pad_kernel_raw.py <kernel.elf> <kernel.raw>", file=sys.stderr)
        sys.exit(2)
    elf_path, raw_path = sys.argv[1], sys.argv[2]
    with open(elf_path, "rb") as f:
        elf = f.read()
    need = max_load_vaddr_plus_memsz(elf)
    with open(raw_path, "rb") as f:
        raw = f.read()
    if len(raw) > need:
        raise SystemExit(f"{raw_path}: {len(raw)} bytes > ELF image {need}")
    if len(raw) < need:
        raw += b"\x00" * (need - len(raw))
        with open(raw_path, "wb") as f:
            f.write(raw)


if __name__ == "__main__":
    main()
