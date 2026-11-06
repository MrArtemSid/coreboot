#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Extract the firmware inputs of a Grantley board from a stock image.
#
# A board port needs three blobs that only the vendor firmware carries:
#
#   descriptor.bin  - the flash descriptor of the image
#   me.bin          - the ME region, so the image keeps the vendor layout
#   uncore.bin      - the PE32 image of the UncoreInitPeim module, the memory
#                     reference code that runs from coreboot
#   intelsetup.bin  - the SYSTEM_CONFIGURATION the vendor setup left in NVRAM
#                     (an NVAR variable named IntelSetup)
#
# The module is located by its user interface section, not by a GUID, because
# the file GUID is not stable across reference code releases.

import argparse
import hashlib
import os
import struct
import sys

# Flash descriptor signature, at offset 0x10 of the image.
IFD_SIGNATURE_OFFSET = 0x10
IFD_SIGNATURE = 0x0FF0A55A
IFD_REGION_NAMES = ("descriptor", "bios", "me", "gbe", "pdr", "devexp")

FV_SIGNATURE = b"_FVH"
FV_SIGNATURE_OFFSET = 0x28
FV_LENGTH_OFFSET = 0x20
FV_HEADER_LENGTH_OFFSET = 0x30

FFS_HEADER_SIZE = 0x18

SECTION_PE32 = 0x10
SECTION_USER_INTERFACE = 0x15
SECTION_EXTENDED_SIZE = 0xFFFFFF

# The variable store keeps a trailing NUL after the payload.
NVAR_SIGNATURE = b"NVAR"
NVAR_HEADER_SIZE = 0x0B
UNCORE_MODULE_NAME = "UncoreInitPeim"
SETUP_VARIABLE_NAME = "IntelSetup"
# sizeof(SYSTEM_CONFIGURATION) built into UncoreInitPeim.
SETUP_PAYLOAD_SIZE = 6090


def read_image(path):
    with open(path, "rb") as image:
        return image.read()


def ifd_regions(image):
    """Return the populated regions of the flash descriptor as (name, start, end)."""
    if len(image) < IFD_SIGNATURE_OFFSET + 4:
        raise ValueError("image is too small to hold a flash descriptor")
    if struct.unpack_from("<I", image, IFD_SIGNATURE_OFFSET)[0] != IFD_SIGNATURE:
        raise ValueError("image does not start with a flash descriptor")

    flmap0 = struct.unpack_from("<I", image, 0x14)[0]
    flreg_base = ((flmap0 >> 16) & 0xFF) << 4
    regions = []
    for index, name in enumerate(IFD_REGION_NAMES):
        flreg = struct.unpack_from("<I", image, flreg_base + 4 * index)[0]
        base = (flreg & 0x7FFF) * 0x1000
        limit = (((flreg >> 16) & 0x7FFF) + 1) * 0x1000
        # An unused region reads as 0x7fff, that is base 0xfff000 and no limit.
        if base >= limit:
            continue
        if limit > len(image):
            raise ValueError("region %s ends beyond the image" % name)
        regions.append((name, base, limit))
    return regions


def iter_firmware_volumes(data):
    """Yield (start, end) of every firmware volume found in data."""
    offset = 0
    while True:
        signature = data.find(FV_SIGNATURE, offset)
        if signature < 0:
            return
        start = signature - FV_SIGNATURE_OFFSET
        offset = signature + len(FV_SIGNATURE)
        if start < 0:
            continue
        length = struct.unpack_from("<Q", data, start + FV_LENGTH_OFFSET)[0]
        if length == 0 or start + length > len(data):
            continue
        yield start, length


def ffs_size(file_header):
    return file_header[0] | (file_header[1] << 8) | (file_header[2] << 16)


def iter_ffs_files(data, start, end):
    """Yield (offset, size) of every FFS file of a firmware volume."""
    header_length = struct.unpack_from("<H", data, start + FV_HEADER_LENGTH_OFFSET)[0]
    offset = start + header_length
    while offset + FFS_HEADER_SIZE <= end:
        header = data[offset + 0x14:offset + 0x17]
        size = ffs_size(header)
        if size < FFS_HEADER_SIZE or offset + size > end:
            return
        yield offset, size
        offset = (offset + size + 7) & ~7


def iter_sections(data, start, end):
    """Yield (type, offset, size) of the sections inside an FFS file."""
    offset = start
    while offset + 4 <= end:
        size = ffs_size(data[offset:offset + 3])
        section_type = data[offset + 3]
        header_size = 4
        if size == SECTION_EXTENDED_SIZE:
            if offset + 8 > end:
                return
            size = struct.unpack_from("<I", data, offset + 4)[0]
            header_size = 8
        if size < header_size or offset + size > end:
            return
        yield section_type, offset + header_size, size - header_size
        offset = (offset + size + 3) & ~3


def ffs_name(data, start, end):
    for section_type, offset, size in iter_sections(data, start, end):
        if section_type != SECTION_USER_INTERFACE:
            continue
        return data[offset:offset + size].decode("utf-16-le").rstrip("\0")
    return None


def find_pe32_image(data, bios_start, bios_end):
    """Return (name, payload) of the module whose UI section is UNCORE_MODULE_NAME."""
    for fv_start, fv_length in iter_firmware_volumes(data[bios_start:bios_end]):
        fv_start += bios_start
        fv_end = fv_start + fv_length
        for ffs_start, ffs_length in iter_ffs_files(data, fv_start, fv_end):
            name = ffs_name(data, ffs_start + FFS_HEADER_SIZE, ffs_start + ffs_length)
            if name != UNCORE_MODULE_NAME:
                continue
            for section_type, offset, size in iter_sections(
                    data, ffs_start + FFS_HEADER_SIZE, ffs_start + ffs_length):
                if section_type == SECTION_PE32:
                    return name, data[offset:offset + size]
    return None, None


def iter_nvar_variables(data, start, end):
    """Yield (name, payload) for every variable of the NVAR stores in [start, end)."""
    offset = start
    while True:
        offset = data.find(NVAR_SIGNATURE, offset, end)
        if offset < 0:
            return
        record = offset
        offset += 4
        total = struct.unpack_from("<H", data, record + 4)[0]
        name_start = record + NVAR_HEADER_SIZE
        name_end = data.find(b"\0", name_start, min(name_start + 64, end))
        if total < NVAR_HEADER_SIZE or name_end < 0 or record + total > end:
            continue
        name = data[name_start:name_end].decode("ascii", "replace")
        if not name.isprintable():
            continue
        payload = data[name_end + 1:record + total]
        yield name, payload


def extract_setup(data, bios_start, bios_end, expected_size):
    """Return the payload of the last IntelSetup variable of the store."""
    payload = None
    for name, candidate in iter_nvar_variables(data, bios_start, bios_end):
        if name == SETUP_VARIABLE_NAME:
            payload = candidate
    if payload is None:
        return None
    # The store terminates the payload with a NUL that is not part of the struct.
    while len(payload) > expected_size and payload[-1] == 0:
        payload = payload[:-1]
    return payload


def write_blob(path, data):
    with open(path, "wb") as blob:
        blob.write(data)
    print("%-16s %8d bytes  sha256 %s" % (
        os.path.basename(path), len(data), hashlib.sha256(data).hexdigest()))


def main():
    parser = argparse.ArgumentParser(
        description="Extract Grantley firmware blobs from a stock image.")
    parser.add_argument("image", help="stock image, or a BIOS region dump")
    parser.add_argument("-o", "--out", default=".",
                        help="output directory (default: current directory)")
    parser.add_argument("--keep-bios", action="store_true",
                        help="also write the BIOS region")
    parser.add_argument("--setup-size", type=int, default=SETUP_PAYLOAD_SIZE,
                        help="size of SYSTEM_CONFIGURATION in bytes (default: %d)"
                             % SETUP_PAYLOAD_SIZE)
    parser.add_argument("--print-regions", action="store_true",
                        help="only list the flash descriptor regions")
    arguments = parser.parse_args()

    image = read_image(arguments.image)

    try:
        regions = ifd_regions(image)
    except ValueError as error:
        sys.exit("%s" % error)
    except struct.error:
        sys.exit("image is truncated")

    if arguments.print_regions:
        for name, start, end in regions:
            print("%-10s %08x - %08x  %d bytes" % (name, start, end, end - start))
        return

    os.makedirs(arguments.out, exist_ok=True)
    regions_by_name = {name: (start, end) for name, start, end in regions}

    blobs = {}
    for name in ("descriptor", "me"):
        if name not in regions_by_name:
            sys.exit("image has no %s region" % name)
        start, end = regions_by_name[name]
        blobs[name] = image[start:end]

    bios_start, bios_end = regions_by_name.get("bios", (0, len(image)))
    if arguments.keep_bios:
        blobs["bios"] = image[bios_start:bios_end]

    name, uncore = find_pe32_image(image, bios_start, bios_end)
    if uncore is None:
        sys.exit("no %s module in the BIOS region" % UNCORE_MODULE_NAME)
    blobs["uncore"] = uncore

    setup = extract_setup(image, bios_start, bios_end, arguments.setup_size)
    if setup is None:
        sys.exit("no %s variable in the BIOS region" % SETUP_VARIABLE_NAME)
    if len(setup) != arguments.setup_size:
        print("warning: IntelSetup is %d bytes, expected %d" % (
            len(setup), arguments.setup_size), file=sys.stderr)
    blobs["intelsetup"] = setup

    for name in ("descriptor", "bios", "me", "uncore", "intelsetup"):
        if name in blobs:
            write_blob(os.path.join(arguments.out, name + ".bin"), blobs[name])


if __name__ == "__main__":
    main()
