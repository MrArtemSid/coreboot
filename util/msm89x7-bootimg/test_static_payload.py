#!/usr/bin/env python3
"""Regression checks for the santoni diagnostic payload's real ELF headers."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SAFE_START, SAFE_END = 0x84400000, 0x84500000


def validate_elf(data):
    assert data[:7] == b'\x7fELF\x02\x01\x01', 'not ELF64 little endian'
    kind, machine = struct.unpack_from('<HH', data, 16)
    assert (kind, machine) == (2, 183), 'not static AArch64 executable'
    entry, phoff = struct.unpack_from('<QQ', data, 24)
    phsize, count = struct.unpack_from('<HH', data, 54)
    loads = []
    for i in range(count):
        typ, flags, off, va, pa, filesz, memsz, align = struct.unpack_from(
            '<IIQQQQQQ', data, phoff + i * phsize)
        assert typ not in (2, 3), 'PT_DYNAMIC/PT_INTERP forbidden'
        if typ != 1:
            continue
        assert SAFE_START <= pa < pa + memsz <= SAFE_END, (
            f'PT_LOAD outside safe RAM: {pa:#x}+{memsz:#x}')
        assert va == pa and filesz <= memsz and off + filesz <= len(data)
        loads.append((pa, memsz, flags))
    assert loads and any(pa <= entry < pa + size and flags & 1
                         for pa, size, flags in loads), 'entry not executable/mapped'
    return entry, loads


class StaticPayloadTest(unittest.TestCase):
    def test_entry_only_does_not_assume_framebuffer_format(self):
        self.assertNotIn('0x90001000', (ROOT / 'payload_test.S').read_text(),
                         'entry diagnostic must not draw using a hard-coded framebuffer')

    def test_real_payload_layout_and_reproducibility(self):
        override = os.environ.get('PAYLOAD_TEST_ELF')
        if override:
            validate_elf(Path(override).read_bytes())
            return
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            paths = [Path(tmp) / name for name in ('a.elf', 'b.elf')]
            for path in paths:
                subprocess.run(['python3', str(ROOT / 'util/msm89x7-bootimg/build_payload.py'),
                                '--output', str(path), '--generation', '0x123456789abcdef0'],
                               check=True)
                validate_elf(path.read_bytes())
            self.assertEqual(paths[0].read_bytes(), paths[1].read_bytes())


if __name__ == '__main__':
    unittest.main()
