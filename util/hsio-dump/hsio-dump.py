#!/usr/bin/env python3
"""Find and dump PCH HSIO / mPHY IOBP tables in a firmware image.

A table entry is the 12-byte struct used by the coreboot HSIO tables:

    struct { u32 addr; u32 and_mask; u32 or_mask; }

with an IOBP address of the form 0xea00LLRR (LL = lane, RR = register offset).
The tool scans for runs of such triples, reports every run it finds, and can
emit the run as a C array in coreboot's `struct hsio_table_row` style.

Usage:
    hsio-dump.py IMAGE [--emit INDEX|NAME] [--compare TABLE.c ...] [--min N]
"""
import argparse
import hashlib
import struct
import sys

ADDR_MASK = 0xFF000000
ADDR_BASE = 0xEA000000
ENTRY = 12


def valid_entry(buf, off):
    if off + ENTRY > len(buf):
        return None
    addr, and_m, or_m = struct.unpack_from("<III", buf, off)
    if (addr & ADDR_MASK) != ADDR_BASE:
        return None
    # Address must address a real lane/register pair: bits 23:16 are zero in
    # every documented Wellsburg/Lynx Point table, the lane sits in 15:8.
    if addr & 0x00F00000:
        return None
    if and_m in (0, 0xFFFFFFFF) and or_m == 0:
        return None
    if and_m == 0 and or_m == 0:
        return None
    # A mask that keeps every bit makes the entry a plain write; allowed, but a
    # mask that keeps nothing plus a zero value is padding, already excluded.
    if (and_m | or_m) == 0:
        return None
    return (addr, and_m, or_m)


def find_runs(buf, min_rows=3):
    runs = []
    off = 0
    size = len(buf)
    while off + ENTRY * min_rows <= size:
        if valid_entry(buf, off) is None:
            off += 4
            continue
        rows = []
        cur = off
        while True:
            ent = valid_entry(buf, cur)
            if ent is None:
                break
            rows.append(ent)
            cur += ENTRY
        if len(rows) >= min_rows:
            runs.append((off, rows))
            off = cur
        else:
            off += 4
    return runs


def describe(rows):
    lanes = sorted({(a >> 8) & 0xFF for a, _, _ in rows})
    offs = sorted({a & 0xFF for a, _, _ in rows})
    return (f"rows={len(rows)} lanes={','.join(f'{l:#04x}' for l in lanes)} "
            f"regs={','.join(f'{o:#04x}' for o in offs[:8])}{'...' if len(offs) > 8 else ''}")


def key(rows):
    return hashlib.sha256(b"".join(struct.pack("<III", *r) for r in rows)).hexdigest()[:16]


def load_c_tables(path):
    tables, cur = {}, None
    with open(path) as f:
        for line in f:
            line = line.strip()
            if "struct hsio_table_row" in line and "[" in line and "=" in line:
                cur = line.split("struct hsio_table_row")[1].strip().split("[")[0].strip()
                tables[cur] = []
            elif line.startswith("{") and cur:
                parts = [p.strip() for p in line.strip("{} ,").split(",") if p.strip()]
                if len(parts) == 3:
                    addr, and_m, or_m = (int(p.replace("~", ""), 16) for p in parts)
                    # C stores the and-mask with a leading '~'; the binary holds
                    # the complemented value.
                    if parts[1].startswith("~"):
                        and_m = (~and_m) & 0xFFFFFFFF
                    tables[cur].append((addr, and_m, or_m))
    return tables


def identify(rows, refs):
    """Name a run by exact set match, table concatenation, or best partial match."""
    want = tuple(sorted(rows))
    for name, ref in refs.items():
        if tuple(sorted(ref)) == want:
            return name
    names = list(refs)
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            if len(refs[a]) + len(refs[b]) == len(rows) and tuple(sorted(refs[a] + refs[b])) == want:
                return f"{a} + {b}"
    best, count = None, 0
    have = {(addr, and_m, or_m) for addr, and_m, or_m in rows}
    for name, ref in refs.items():
        hit = sum(1 for row in ref if row in have)
        if hit > count:
            best, count = name, hit
    if best:
        return f"partial {count}/{len(refs[best])} of {best}"
    return "no match in reference"


def emit_c(name, rows):
    out = [f"static const struct hsio_table_row {name}[] = {{"]
    for addr, and_m, or_m in rows:
        # coreboot writes the mask in its complemented form: ~(and_mask).
        out.append(f"\t{{ {addr:#010x}, ~{(~and_m) & 0xFFFFFFFF:#010x}, {or_m:#010x} }},")
    out.append("};")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--emit", help="index of a run, or 'all'")
    ap.add_argument("--compare", nargs="*", default=[], help="C files with hsio_table_row arrays")
    ap.add_argument("--min", type=int, default=3, help="minimum rows to report a run")
    ap.add_argument("--base", type=lambda x: int(x, 0), default=0,
                    help="offset added to the image offset in the report (e.g. flash base)")
    args = ap.parse_args()

    buf = open(args.image, "rb").read()
    runs = find_runs(buf, args.min)
    print(f"{args.image}: {len(buf)} bytes, {len(runs)} candidate runs")
    for i, (off, rows) in enumerate(runs):
        print(f"  [{i:2d}] off={off:#08x} {describe(rows)} sha={key(rows)}")

    refs = {}
    for path in args.compare:
        for name, rows in load_c_tables(path).items():
            refs[name] = rows
    if refs:
        print("\nmatches against reference tables:")
        for i, (off, rows) in enumerate(runs):
            print(f"  [{i:2d}] off={off:#08x} {identify(rows, refs)}")

    if args.emit is not None:
        if args.emit == "all":
            for i, (off, rows) in enumerate(runs):
                if len(rows) < 8:
                    continue
                print(f"\n/* image offset {off:#08x}, {len(rows)} rows */")
                print(emit_c(f"hsio_{off:08x}", rows))
        else:
            idx = int(args.emit, 0)
            off, rows = runs[idx]
            print(f"\n/* image offset {off:#08x}, {len(rows)} rows */")
            print(emit_c(f"hsio_{off:08x}", rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
