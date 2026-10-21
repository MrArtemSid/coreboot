#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""
Extract native blobs (Flash Descriptor, Intel ME, and Uncore PEIM)
from Intel Grantley (Haswell-EP / Broadwell-EP) stock BIOS dumps.
"""

import argparse
import os
import struct
import sys

UNCORE_PEIM_GUID = bytes.fromhex("a48b1cd7f24a0d4db1baf2409f0c20d3")


def parse_flash_descriptor(data):
    """Parse regions from Intel Flash Descriptor (FRBA table)."""
    if len(data) < 0x1000:
        raise ValueError("Image too small to contain Intel Flash Descriptor")

    flvalsig = struct.unpack_from("<I", data, 0x10)[0]
    if flvalsig != 0x0FF0A55A:
        raise ValueError("Invalid Flash Descriptor signature (expected 0x0FF0A55A)")

    # Read Flash Region Base Address (FRBA) offset from Flash Map 0
    flmap0 = struct.unpack_from("<I", data, 0x14)[0]
    frba = ((flmap0 >> 16) & 0xFF) << 4

    regions = {}
    region_names = ["descriptor", "bios", "me", "gbe", "pdr"]

    for i, name in enumerate(region_names):
        val = struct.unpack_from("<I", data, frba + (i * 4))[0]
        base_blocks = val & 0x1FFF
        limit_blocks = (val >> 16) & 0x1FFF

        if base_blocks > limit_blocks:
            continue

        base = base_blocks << 12
        limit = (limit_blocks << 12) | 0xFFF
        regions[name] = (base, limit + 1)

    return regions


def extract_uncore_peim(data):
    """Find and extract UncoreInitPeim PE32 executable from BIOS dump."""
    pos = data.find(UNCORE_PEIM_GUID)
    if pos == -1:
        raise ValueError("UncoreInitPeim GUID not found in image")

    # Search for MZ signature within reasonable distance of the FFS header
    mz_pos = data.find(b"MZ", pos, pos + 0x200)
    if mz_pos == -1:
        raise ValueError("PE32 header (MZ) not found after UncoreInitPeim GUID")

    pe_offset = struct.unpack_from("<I", data, mz_pos + 0x3C)[0]
    pe_sig = data[mz_pos + pe_offset : mz_pos + pe_offset + 4]
    if pe_sig != b"PE\0\0":
        raise ValueError("Invalid PE signature in UncoreInitPeim")

    # Read SizeOfImage from PE Optional Header
    opt_header = mz_pos + pe_offset + 24
    size_of_image = struct.unpack_from("<I", data, opt_header + 56)[0]

    return data[mz_pos : mz_pos + size_of_image], mz_pos


def main():
    parser = argparse.ArgumentParser(
        description="Extract Grantley blobs (descriptor, me, uncore) from vendor BIOS"
    )
    parser.add_argument("image", help="Path to vendor BIOS dump")
    parser.add_argument(
        "-o", "--outdir", default=".", help="Output directory for blobs"
    )
    parser.add_argument(
        "--uncore-only", action="store_true", help="Extract only uncore.bin"
    )

    args = parser.parse_args()

    if not os.path.isfile(args.image):
        print(f"Error: file not found: {args.image}", file=sys.stderr)
        return 1

    with open(args.image, "rb") as f:
        data = f.read()

    os.makedirs(args.outdir, exist_ok=True)

    # 1. Extract uncore.bin
    try:
        uncore_bytes, uncore_offset = extract_uncore_peim(data)
        uncore_path = os.path.join(args.outdir, "uncore.bin")
        with open(uncore_path, "wb") as f:
            f.write(uncore_bytes)
        print(
            f"[+] uncore.bin: extracted {len(uncore_bytes)} bytes "
            f"(offset: 0x{uncore_offset:x}) -> {uncore_path}"
        )
    except ValueError as e:
        print(f"[-] uncore.bin: {e}", file=sys.stderr)
        return 1

    if args.uncore_only:
        return 0

    # 2. Extract Flash Descriptor and ME regions
    try:
        regions = parse_flash_descriptor(data)
    except ValueError as e:
        print(f"[-] Flash Descriptor: {e}", file=sys.stderr)
        return 1

    if "descriptor" in regions:
        base, end = regions["descriptor"]
        desc_path = os.path.join(args.outdir, "descriptor.bin")
        with open(desc_path, "wb") as f:
            f.write(data[base:end])
        print(
            f"[+] descriptor.bin: extracted {end - base} bytes "
            f"(0x{base:x}..0x{end:x}) -> {desc_path}"
        )

    if "me" in regions:
        base, end = regions["me"]
        me_path = os.path.join(args.outdir, "me.bin")
        with open(me_path, "wb") as f:
            f.write(data[base:end])
        print(
            f"[+] me.bin: extracted {end - base} bytes "
            f"(0x{base:x}..0x{end:x}) -> {me_path}"
        )

    return 0


if __name__ == "__main__":
    sys.exit(main())
