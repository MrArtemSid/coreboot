#!/usr/bin/env python3
"""Assemble the real arm64_init_cpu and tlbiall() for EL1/2/3 and check
that each uses the TLB invalidation legal for (and covering) that EL."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
XC = ROOT / 'util/crossgcc/xgcc/bin/aarch64-elf-'
EXPECTED = {1: 'vmalle1', 2: 'alle2', 3: 'alle3'}
INCLUDES = ['-Isrc/include', '-Isrc/arch/arm64/include',
            '-Isrc/arch/arm64/include/armv8', '-Isrc/commonlib/include',
            '-Isrc/commonlib/bsd/include']


def tlbi_ops(obj, symbol):
    text = subprocess.check_output([str(XC) + 'objdump', '-d',
                                    f'--disassemble={symbol}', str(obj)],
                                   text=True)
    return [line.split('tlbi', 1)[1].split()[0].rstrip(',')
            for line in text.splitlines() if '\ttlbi\t' in line]


class TlbiPerElTest(unittest.TestCase):
    def build(self, tmp, el, name, src, extra=()):
        obj = Path(tmp) / f'{name}{el}.o'
        cfg = Path(tmp) / f'config{el}.h'
        cfg.write_text(f'#define CONFIG_ARM64_CURRENT_EL {el}\n'
                       '#define CONFIG_ARCH_ARM64 1\n')
        subprocess.run([str(XC) + 'gcc', '-c', '-O2', '-ffreestanding',
                        '-include', str(cfg), *INCLUDES, *extra,
                        str(src), '-o', str(obj)], cwd=ROOT, check=True)
        return obj

    def test_arm64_init_cpu(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            for el, op in EXPECTED.items():
                with self.subTest(el=el):
                    obj = self.build(tmp, el, 'cpu',
                                     ROOT / 'src/arch/arm64/armv8/cpu.S',
                                     ('-D__ASSEMBLER__',))
                    self.assertEqual(tlbi_ops(obj, 'arm64_init_cpu'), [op])

    def test_tlbiall(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            src = Path(tmp) / 't.c'
            src.write_text('#include <arch/lib_helpers.h>\n'
                           'void probe(void) { tlbiall(); }\n'
                           'void el1(void) { tlbiall_el1(); }\n')
            for el, op in EXPECTED.items():
                with self.subTest(el=el):
                    obj = self.build(tmp, el, 't', src)
                    self.assertEqual(tlbi_ops(obj, 'probe'), [op])
                    # tlbiall_el1() keeps upstream meaning: invalidate the
                    # EL1&0 regime from EL2/EL3.
                    self.assertEqual(tlbi_ops(obj, 'el1'), ['alle1'])


if __name__ == '__main__':
    unittest.main()
