#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build-test Grantley diagnostics without producing or executing firmware."""

import argparse
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BLOCKED = "Grantley DRAM initialization is not implemented"


def run(args, expect_failure=False):
    result = subprocess.run(args, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if expect_failure:
        if result.returncode == 0 or BLOCKED not in result.stdout:
            raise RuntimeError(f"Image build was not blocked correctly:\n{result.stdout}")
    elif result.returncode:
        raise RuntimeError(f"Command failed: {args!r}\n{result.stdout}")
    return result.stdout


def config_values(path):
    return dict(line.split("=", 1) for line in path.read_text().splitlines()
                if line.startswith("CONFIG_") and "=" in line)


def check_elf(path, symbol):
    data = path.read_bytes()
    if data[:7] != b"\x7fELF\x01\x01\x01" or struct.unpack_from("<H", data, 18)[0] != 3:
        raise RuntimeError(f"Not a little-endian IA32 ELF: {path}")
    symbols = run(["nm", "-u", str(path)])
    if symbols.strip():
        raise RuntimeError(f"Unresolved symbols in {path}: {symbols}")
    symbols = run(["nm", str(path)])
    if symbol not in {line.split()[-1] for line in symbols.splitlines() if line.split()}:
        raise RuntimeError(f"Missing {symbol} in {path}")
    return len(data)


def check_microcode(path):
    data = path.read_bytes()
    offset = 0
    signatures = []
    while offset < len(data):
        if len(data) - offset < 48:
            raise RuntimeError("Truncated microcode header")
        header = struct.unpack_from("<12I", data, offset)
        size = header[8] or 2048
        if header[0] != 1 or size < 48 or size % 4 or offset + size > len(data):
            raise RuntimeError("Invalid microcode header/size")
        words = struct.unpack_from(f"<{size // 4}I", data, offset)
        if sum(words) & 0xffffffff:
            raise RuntimeError("Microcode checksum failed")
        signatures.append(header[3])
        offset += size
    if not {0x306f2, 0x406f1}.issubset(signatures):
        raise RuntimeError(f"Missing production EP microcode: {signatures}")
    if any((signature & ~0xf) not in (0x306f0, 0x406f0) for signature in signatures):
        raise RuntimeError("Client microcode included in EP bundle")
    return [f"{signature:08x}" for signature in signatures]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--any-toolchain", action="store_true",
                        help="Explicitly allow the installed host compiler for build tests")
    args = parser.parse_args()
    report = {}
    for board in ("qd4", "bd4"):
        obj = Path("build") / f"grantley-{board}"
        config = obj / ".config"
        make = ["make", f"obj={obj}", f"DOTCONFIG={config}"]
        run(make + [f"KBUILD_DEFCONFIG=configs/config.huananzhi_x99_{board}.diagnostic",
                    "defconfig"])
        if args.any_toolchain:
            run(["util/scripts/config", "--file", str(config), "--enable", "ANY_TOOLCHAIN"])
            run(make + ["olddefconfig"])
        values = config_values(ROOT / config)
        for name in ("CPU_INTEL_HASWELL_EP", "NORTHBRIDGE_INTEL_HASWELL_EP",
                     "NO_ECAM_MMCONF_SUPPORT", "SEPARATE_ROMSTAGE", "BOOTBLOCK_CONSOLE"):
            if values.get(f"CONFIG_{name}") != "y":
                raise RuntimeError(f"Missing required config: {name}")
        for name in ("CPU_INTEL_HASWELL", "NORTHBRIDGE_INTEL_HASWELL", "ECAM_MMCONF_SUPPORT",
                     "USE_NATIVE_RAMINIT", "USE_BROADWELL_MRC"):
            if values.get(f"CONFIG_{name}") == "y":
                raise RuntimeError(f"Unsafe client config enabled: {name}")
        bootblock = obj / "cbfs/fallback/bootblock.debug"
        romstage = obj / "cbfs/fallback/romstage.debug"
        microcode = obj / "cpu_microcode_blob.bin"
        # Rebuild the bundle even if the Makefile's selected input list changed.
        (ROOT / microcode).unlink(missing_ok=True)
        run(make + ["-j4", str(bootblock), str(romstage), str(microcode)])
        report[board] = {
            "bootblock_elf_bytes": check_elf(ROOT / bootblock, "bootblock_early_northbridge_init"),
            "romstage_elf_bytes": check_elf(ROOT / romstage, "romstage_main"),
            "microcode_signatures": check_microcode(ROOT / microcode),
        }
        for goal in ([], [str(obj / "coreboot.rom")], ["coreboot"]):
            run(make + goal, expect_failure=True)
        if (ROOT / obj / "coreboot.rom").exists():
            raise RuntimeError("Unexpected flashable image")
        report[board]["image_guards_passed"] = 3
        print(f"PASS: {board}: both stages linked, microcode checked, image builds blocked")
    run(["make", "build-tests/northbridge/haswell_ep-platform-test",
                 "build-tests/northbridge/haswell_ep-spd-test",
                 "build-tests/northbridge/haswell_ep-pei-test"])
    # Invoke the binary directly: make's per-test runner deliberately ignores failures.
    print(run(["build/tests/tests/northbridge/haswell_ep-platform-test/run"]), end="")
    print(run(["build/tests/tests/northbridge/haswell_ep-spd-test/run"]), end="")
    print(run(["build/tests/tests/northbridge/haswell_ep-pei-test/run"]), end="")
    output = ROOT / "build/grantley-diagnostic-results.json"
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Results: {output.relative_to(ROOT)}")
    print("Build tests only; no firmware execution or hardware validation was performed.")


if __name__ == "__main__":
    main()
