#!/usr/bin/env python3

import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: gen_kernel_blob.py <kernel.raw> <kernel_blob.inc>", file=sys.stderr)
        return 2

    src_path = sys.argv[1]
    dst_path = sys.argv[2]

    data = open(src_path, "rb").read()

    with open(dst_path, "w", encoding="utf-8") as f:
        f.write("// generated kernel_blob.inc\n")
        f.write("static const unsigned char kernel_blob[] = {\n")
        for i, b in enumerate(data):
            if i % 16 == 0:
                f.write("    ")
            f.write(f"0x{b:02X}, ")
            if i % 16 == 15:
                f.write("\n")
        f.write("\n};\n")
        f.write(f"static const unsigned long kernel_blob_len = {len(data)};\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

