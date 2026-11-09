#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Extract the IIO PCIe PHY receiver settings of a Haswell-EP board from its
# stock image and print them as a C header.
#
# Before the IIO links train, the vendor firmware programs the receiver of
# every PCIe PHY lane group through the IIO DFX functions (bus 0, devices 6
# and 7). Its IioInit DXE module keeps those settings as tables of
#
#     struct { u32 port; u8 size; u8 pad[3]; u32 reg; u32 and; u32 or; }
#
# and applies a common table, then one selected by the CPU stepping, then a
# CTLE table that only applies from B0 on when the part is not in DCU mode.
# The tables are found through the code that hands them to the writer, so no
# offset is assumed: every table address and size is decoded from the
# instructions themselves.

import argparse
import lzma
import re
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_blobs  # noqa: E402

IIO_MODULE_NAME = "IioInit"

SECTION_GUID_DEFINED = 0x02
# The LZMA section GUID of EDK II, EE4E5898-3914-4259-9D6E-DC7BD79403CF.
LZMA_SECTION_GUID = bytes.fromhex("98584eee14395942" "9d6edc7bd79403cf")

ENTRY = struct.Struct("<IB3xIII")

# Port numbers of the tables. 15 selects the global DFX function.
PORT_GLOBAL = 15
PORT_DMI = 0
MAX_PORT = 10

# Code patterns. The register allocation differs between builds, so every
# register operand is a character class.
LEA_RIP = rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d](....)"
# lea reg, [rip + common]; mov esi, size; mov r9, reg; mov [rax - 0x18], esi
COMMON = re.compile(LEA_RIP + rb"\xbe(....)\x4c\x8b[\xc8-\xcf]\x89\x70\xe8", re.S)
# switch (stepping) { case 2: C0; case 1: B0; case 0: A0 }
STEPPING = re.compile(
    rb"\x83\xe9\x01\x74.\x83\xf9\x01\x75." +
    LEA_RIP + rb"\xbe(....)\xeb." +
    LEA_RIP + rb"\xbe(....)\xeb." +
    LEA_RIP + rb"\xbe(....)", re.S)
# CAPID0 (uncore bus, 30.3, offset 0x84), then a test of bit 30, the DCU mode
# fuse, guarding lea r9, [rip + ctle]; ...; mov dword [rsp + 0x20], size
CTLE = re.compile(
    rb"\x84\x30\x0f\x00.{0,40}?\x0f\xba[\xe0-\xe7]\x1e\x72.\x4c\x8d\x0d(....)"
    rb".{0,16}?\xc7\x44\x24\x20(....)", re.S)
# The writer skips port 0 (the DMI link, 06.0 on bus 0) unless the register
# is one of a list, compared as cmp dword [entry + 8], imm32.
DMI_FIRST_CMP = re.compile(rb"\x81[\x38-\x3f]....", re.S)


class PeImage:
    def __init__(self, data):
        if data[:2] != b"MZ":
            raise ValueError("not a PE image")
        pe = struct.unpack_from("<I", data, 0x3c)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("not a PE image")
        sections = struct.unpack_from("<H", data, pe + 6)[0]
        optional = struct.unpack_from("<H", data, pe + 20)[0]
        table = pe + 24 + optional
        self.data = data
        self.sections = []
        for i in range(sections):
            entry = table + 40 * i
            vsize, rva, rsize, raw = struct.unpack_from("<IIII", data, entry + 8)
            self.sections.append((rva, max(vsize, rsize), raw, rsize))

    def rva(self, offset):
        for rva, _, raw, rsize in self.sections:
            if raw <= offset < raw + rsize:
                return offset - raw + rva
        raise ValueError("file offset %#x is outside the sections" % offset)

    def offset(self, rva):
        for start, size, raw, rsize in self.sections:
            if start <= rva < start + min(size, rsize):
                return rva - start + raw
        raise ValueError("RVA %#x is outside the sections" % rva)

    def rip_target(self, insn_offset, disp_offset, insn_size):
        disp = struct.unpack_from("<i", self.data, disp_offset)[0]
        return self.offset(self.rva(insn_offset) + insn_size + disp)


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def read_table(pe, offset, size, name):
    if size % ENTRY.size:
        raise ValueError("%s: size %d is not a multiple of %d" % (name, size, ENTRY.size))
    entries = []
    for i in range(size // ENTRY.size):
        port, width, reg, and_mask, or_mask = ENTRY.unpack_from(pe.data, offset + i * ENTRY.size)
        if port != PORT_GLOBAL and port > MAX_PORT:
            raise ValueError("%s[%d]: bad port %d" % (name, i, port))
        if width not in (1, 2, 4):
            raise ValueError("%s[%d]: bad size %d" % (name, i, width))
        entries.append((port, width, reg, and_mask, or_mask))
    return entries


def decode_dmi_filter(data, start, end):
    """Return the (register, excluded and-mask) pairs the writer allows on port 0."""
    for match in DMI_FIRST_CMP.finditer(data, start, end):
        if u32(data, match.start() + 2) >> 20 != 0x120:
            continue
        pos = match.start()
        allowed = []
        while data[pos] == 0x81 and 0x38 <= data[pos + 1] <= 0x3f:
            reg = u32(data, pos + 2)
            pos += 6
            if data[pos] == 0x74:                       # je allow
                allowed.append((reg, None))
                pos += 2
            elif data[pos] == 0x75 and data[pos + 2] == 0x81 and \
                    0x78 <= data[pos + 3] <= 0x7f and data[pos + 4] == 4:
                # jne next; cmp dword [entry + 4], imm32; jne allow
                allowed.append((reg, u32(data, pos + 5)))
                pos += 11
            elif data[pos:pos + 2] == b"\x0f\x85":       # last: jne skip
                allowed.append((reg, None))
                return allowed
            else:
                break
    raise ValueError("could not decode the DMI register filter")


def dmi_allows(allowed, reg, and_mask):
    for allowed_reg, excluded_mask in allowed:
        if reg == allowed_reg and (excluded_mask is None or and_mask != excluded_mask):
            return True
    return False


def extract(pe):
    data = pe.data
    stepping = STEPPING.search(data)
    if not stepping:
        raise ValueError("stepping switch not found")
    common = None
    for match in COMMON.finditer(data, max(0, stepping.start() - 0x60), stepping.start()):
        common = match
    if not common:
        raise ValueError("common table reference not found")
    ctle = CTLE.search(data, stepping.end(), stepping.end() + 0x100)
    if not ctle:
        raise ValueError("CTLE table reference not found")

    def target(match, group):
        disp_offset = match.start(group)
        insn_end = disp_offset + 4
        disp = struct.unpack_from("<i", data, disp_offset)[0]
        return pe.offset(pe.rva(insn_end) + disp)

    tables = {"common": (target(common, 1), u32(data, common.start(2)))}
    for i, name in enumerate(("C0", "B0", "A0")):
        tables[name] = (target(stepping, 1 + 2 * i), u32(data, stepping.start(2 + 2 * i)))
    tables["ctle"] = (target(ctle, 1), u32(data, ctle.start(2)))

    # The writer is the function the CTLE table is handed to.
    call = data.find(b"\xe8", ctle.end(), ctle.end() + 8)
    if call < 0:
        raise ValueError("table writer call not found")
    writer = pe.offset(pe.rva(call + 5) + struct.unpack_from("<i", data, call + 1)[0])
    allowed = decode_dmi_filter(data, writer, writer + 0x200)

    result = {name: read_table(pe, offset, size, name)
              for name, (offset, size) in tables.items()}
    return result, allowed, tables


def iter_leaf_sections(data, start, end):
    """Yield (type, payload) of the sections of a file, unpacking LZMA sections."""
    for kind, offset, size in extract_blobs.iter_sections(data, start, end):
        if kind != SECTION_GUID_DEFINED:
            yield kind, data[offset:offset + size]
            continue
        # The header so far is the common one; the GUID-defined header follows.
        guid = data[offset:offset + 16]
        data_offset = struct.unpack_from("<H", data, offset + 16)[0]
        header = 8 if extract_blobs.ffs_size(data[offset - 4:offset - 1]) == \
            extract_blobs.SECTION_EXTENDED_SIZE else 4
        payload = data[offset - header + data_offset:offset + size]
        if guid == LZMA_SECTION_GUID:
            payload = lzma.decompress(payload, format=lzma.FORMAT_ALONE)
            yield from iter_leaf_sections(payload, 0, len(payload))
        else:
            yield from iter_leaf_sections(data, offset - header + data_offset, offset + size)


def find_module(image):
    """Return the PE32 body of the IioInit module of a full image or BIOS region."""
    if image[:2] == b"MZ":
        return image
    try:
        regions = {n: (s, e) for n, s, e in extract_blobs.ifd_regions(image)}
        start, end = regions.get("bios", (0, len(image)))
    except ValueError:
        start, end = 0, len(image)
    for fv_start, fv_length in extract_blobs.iter_firmware_volumes(image[start:end]):
        fv_start += start
        fv_end = fv_start + fv_length
        for ffs_start, ffs_length in extract_blobs.iter_ffs_files(image, fv_start, fv_end):
            body = ffs_start + extract_blobs.FFS_HEADER_SIZE
            sections = list(iter_leaf_sections(image, body, ffs_start + ffs_length))
            names = [p.decode("utf-16-le").rstrip("\0") for k, p in sections
                     if k == extract_blobs.SECTION_USER_INTERFACE]
            if IIO_MODULE_NAME not in names:
                continue
            for kind, payload in sections:
                if kind == extract_blobs.SECTION_PE32:
                    return payload
    return None


def c_table(name, entries, allowed):
    lines = ["static const struct iio_phy_setting %s[] = {" % name]
    for port, width, reg, and_mask, or_mask in entries:
        if port == PORT_DMI and not dmi_allows(allowed, reg, and_mask):
            continue
        lines.append("\t{ %2d, %d, 0x%03x, 0x%08x, 0x%08x }," % (
            port, width, reg & 0xfff, and_mask, or_mask))
    lines.append("};")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Extract the Haswell-EP IIO PHY settings from a stock image.")
    parser.add_argument("image", help="stock image, BIOS region or IioInit PE32 image")
    parser.add_argument("-o", "--out", help="write the C header here (default: stdout)")
    parser.add_argument("--summary", action="store_true",
                        help="print where the tables were found instead of the header")
    args = parser.parse_args()

    with open(args.image, "rb") as f:
        image = f.read()
    module = find_module(image)
    if module is None:
        sys.exit("no %s module in the image" % IIO_MODULE_NAME)
    try:
        pe = PeImage(module)
        tables, allowed, where = extract(pe)
    except (ValueError, struct.error) as error:
        sys.exit("%s: %s" % (args.image, error))

    if args.summary:
        for name, (offset, size) in where.items():
            print("%-6s at %#06x, %3d entries" % (name, offset, size // ENTRY.size))
        print("DMI port registers:", ", ".join(
            "%#x%s" % (r, "" if m is None else " (and != %#x)" % m) for r, m in allowed))
        return

    out = [
        "/* SPDX-License-Identifier: GPL-2.0-only */",
        "",
        "/*",
        " * Haswell-EP IIO PCIe PHY receiver settings, as programmed by the vendor",
        " * firmware before link training. Generated by",
        " * util/grantley/extract_iio_recipe.py; do not edit.",
        " *",
        " * Port 0 is the DMI link (DFX 06.0) and only carries the settings the",
        " * vendor firmware applies to it, ports 1-6 are DFX 06.1-06.6, ports 7-10",
        " * are DFX 07.0-07.3 and port 15 is the global DFX function 06.7.",
        " */",
        "",
        "#ifndef NORTHBRIDGE_INTEL_GRANTLEY_IIO_PHY_RECIPE_H",
        "#define NORTHBRIDGE_INTEL_GRANTLEY_IIO_PHY_RECIPE_H",
        "",
        "#include \"iio_phy.h\"",
        "",
        c_table("hsx_phy_common", tables["common"], allowed),
        "",
        c_table("hsx_phy_a0", tables["A0"], allowed),
        "",
        c_table("hsx_phy_b0", tables["B0"], allowed),
        "",
        c_table("hsx_phy_c0", tables["C0"], allowed),
        "",
        "/* B0 and later, unless the part is fused for DCU mode. */",
        c_table("hsx_phy_ctle", tables["ctle"], allowed),
        "",
        "#endif /* NORTHBRIDGE_INTEL_GRANTLEY_IIO_PHY_RECIPE_H */",
        "",
    ]
    text = "\n".join(out)
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
