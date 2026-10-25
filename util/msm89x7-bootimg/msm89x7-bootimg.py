#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Package a coreboot ROM as an Android boot image for msm89x7 devices.

The stock Android bootloader (LK/aboot) on msm89x7 devices boots
arm64 images from the boot partition using the Android boot image
format. The coreboot bootblock already embeds the ARM64 kernel image
header (magic at offset 0x38) that LK checks, so this script only
verifies it and wraps the ROM into the boot image container.

The primary bootloader also needs a device tree to prepare (it copies
the selected one to its tags address and passes it in x0), which on
this SoC generation is provided as a QCDT table. Extract it from the
device's stock boot image with --extract-qcdt.

Examples:
  msm89x7-bootimg.py --extract-qcdt stock-boot.img -o stock-dt.img
  msm89x7-bootimg.py --kernel coreboot.rom --qcdt stock-dt.img -o boot.img
"""

import argparse
import struct
import sys

BOOT_MAGIC = b"ANDROID!"
SEANDROIDENFORCE = b"SEANDROIDENFORCE"
HDR_FMT = "<8s10I16s512s8I1024s"
HDR_SIZE = struct.calcsize(HDR_FMT)
assert HDR_SIZE == 1632

# Fixed load addresses of the stock LK on msm89x7 devices
# (ABOOT_FORCE_KERNEL64_ADDR / _TAGS_ADDR / _RAMDISK_ADDR).
DEFAULT_KERNEL_ADDR = 0x80080000
DEFAULT_TAGS_ADDR = 0x83400000
DEFAULT_RAMDISK_ADDR = 0x83600000

DEFAULT_PAGE_SIZE = 2048


def pad(data, page_size):
    pad_len = -len(data) % page_size
    return data + b"\0" * pad_len


def round_page(size, page_size):
    return (size + page_size - 1) // page_size * page_size


def read_stock_header(f):
    hdr = struct.unpack(HDR_FMT, f.read(HDR_SIZE))
    if hdr[0] != BOOT_MAGIC:
        sys.exit("stock boot image: bad magic")
    return hdr


def cmd_extract_qcdt(args):
    hdr = read_stock_header(args.stock)
    (magic, kernel_size, kernel_addr, ramdisk_size, ramdisk_addr,
     second_size, second_addr, tags_addr, page_size, dt_size,
     unused) = hdr[:11]

    page = page_size
    offset = page
    offset += round_page(kernel_size, page)
    offset += round_page(ramdisk_size, page)
    offset += round_page(second_size, page)

    args.stock.seek(offset)
    qcdt = args.stock.read(dt_size)
    if len(qcdt) != dt_size:
        sys.exit("stock boot image: truncated QCDT")
    args.output.write(qcdt)
    print(f"extracted QCDT of {dt_size} bytes to {args.output.name}")


def cmd_make(args):
    kernel = bytearray(args.kernel.read())
    if len(kernel) < 0x40 or kernel[0x38:0x3c] != b"ARMd":
        sys.exit("kernel does not look like a coreboot ROM with an "
                 "ARM64 kernel image header in its bootblock")

    # A single device tree appended behind the image. The stock LK
    # reads its offset from word 0x2c of the ARM64 kernel image header
    # (dev_tree_appended()).
    if args.append_dtb:
        dtb = args.append_dtb.read()
        dtb_offset = len(kernel)
        kernel += dtb
        struct.pack_into("<I", kernel, 0x2c, dtb_offset)

    qcdt = args.qcdt.read() if args.qcdt else b""
    # A 1-byte ramdisk, like lk2nd uses when it is booted as a second
    # stage.
    ramdisk = b"\0"

    hdr = struct.pack(
        HDR_FMT,
        BOOT_MAGIC,
        len(kernel), args.kernel_addr,
        len(ramdisk), args.ramdisk_addr,
        0, 0,  # second
        args.tags_addr,
        args.page_size,
        len(qcdt),
        0,  # unused
        b"coreboot\0".ljust(16, b"\0"),
        args.cmdline.encode().ljust(512, b"\0"),
        *([0] * 8),
        b"\0" * 1024,
    )

    out = args.output
    out.write(pad(hdr, args.page_size))
    out.write(pad(kernel, args.page_size))
    out.write(pad(ramdisk, args.page_size))
    if qcdt:
        out.write(pad(qcdt, args.page_size))
    out.write(SEANDROIDENFORCE)
    print(f"wrote {out.name} ({out.tell()} bytes)")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("make", help="build a boot image from coreboot.rom")
    p.add_argument("--kernel", required=True, type=argparse.FileType("rb"),
                   help="coreboot.rom")
    p.add_argument("--append-dtb", type=argparse.FileType("rb"),
                   help="device tree to append behind the image (offset "
                        "referenced from the ARM64 kernel image header), "
                        "e.g. the lk2nd device tree for the target")
    p.add_argument("--qcdt", type=argparse.FileType("rb"),
                   help="QCDT device tree table from the stock boot image")
    p.add_argument("--cmdline", default="coreboot")
    p.add_argument("--kernel-addr", type=lambda x: int(x, 0),
                   default=DEFAULT_KERNEL_ADDR)
    p.add_argument("--tags-addr", type=lambda x: int(x, 0),
                   default=DEFAULT_TAGS_ADDR)
    p.add_argument("--ramdisk-addr", type=lambda x: int(x, 0),
                   default=DEFAULT_RAMDISK_ADDR)
    p.add_argument("--page-size", type=int, default=DEFAULT_PAGE_SIZE)
    p.add_argument("--output", "-o", required=True, type=argparse.FileType("wb"))
    p.set_defaults(func=cmd_make)

    p = sub.add_parser("extract-qcdt",
                       help="extract the QCDT table from a stock boot image")
    p.add_argument("stock", type=argparse.FileType("rb"))
    p.add_argument("--output", "-o", required=True, type=argparse.FileType("wb"))
    p.set_defaults(func=cmd_extract_qcdt)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
