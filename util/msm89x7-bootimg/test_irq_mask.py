#!/usr/bin/env python3
"""Check actual assembled exception setup for LK handoff interrupt masks."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class InterruptMaskTest(unittest.TestCase):
    def test_irq_fiq_stay_masked_without_gic_takeover(self):
        for stage in ('bootblock', 'romstage', 'ramstage'):
            with self.subTest(stage=stage):
                text = subprocess.check_output([
                    'aarch64-linux-gnu-objdump', '-d',
                    str(ROOT / f'build/cbfs/fallback/{stage}.debug'),
                    '--disassemble=exception_init_asm'], text=True)
                self.assertIn('daifclr, #0xc', text)
                self.assertNotIn('daifclr, #0xf', text)


if __name__ == '__main__':
    unittest.main()
