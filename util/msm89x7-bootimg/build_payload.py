#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build the static santoni framebuffer/entry/WDT diagnostic (not an OS)."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--generation', required=True, type=lambda x: int(x, 0))
    args = parser.parse_args()
    if not 0 < args.generation < (1 << 64) - 1:
        parser.error('generation must be a fresh nonzero/non-ff uint64')
    cc = ROOT / 'util/crossgcc/xgcc/bin/aarch64-elf-gcc'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(cc), '-nostdlib', '-nostartfiles', '-ffreestanding',
                    '-fno-pie', '-no-pie', '-static',
                    f'-DPAYLOAD_GENERATION={args.generation:#x}',
                    '-Wl,-s,--build-id=none,--orphan-handling=error',
                    '-Wl,-T,' + str(ROOT / 'payload_test.ld'),
                    str(ROOT / 'payload_test.S'), '-o', str(args.output)], check=True)


if __name__ == '__main__':
    main()
