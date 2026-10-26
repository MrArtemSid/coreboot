#!/usr/bin/env python3
"""Exercise the real ramoops writer at its diagnostic-tail boundary."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DiagnosticTailTest(unittest.TestCase):
    def test_console_wrap_does_not_overwrite_beacon(self):
        source = (ROOT / 'src/soc/qualcomm/msm89x7/ramoops.c').read_text()
        source = source[source.index('#define RAMOOPS_SIG'):source.index('void __ramoops_tx_flush')]
        harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_MSM89X7_RAMOOPS_SIZE 0x40000
static unsigned char buffer[CONFIG_MSM89X7_RAMOOPS_SIZE];
#define CONFIG_MSM89X7_RAMOOPS_ADDR ((uintptr_t)buffer)
#define MSM89X7_WDT_BASE 0
#define ENV_BOOTBLOCK 1
#define MIN(a,b) ((a)<(b)?(a):(b))
static void write32(void *p, uint32_t v) {}
static uint32_t read32(void *p) { return *(uint32_t *)p; }
static void dsb(void) {}
''' + source + r'''
int main(void) {
    __ramoops_init();
    memset(buffer + sizeof(buffer) - 0x100, 0xa5, 0x100);
    for (unsigned i=0; i<2*sizeof(buffer); i++) __ramoops_tx_byte('X');
    for (unsigned i=sizeof(buffer)-0x100; i<sizeof(buffer); i++)
        if (buffer[i] != 0xa5) { fputs("console overwrote diagnostic tail\n", stderr); return 1; }
    if (ramoops_hdr()->size != sizeof(buffer)-0x100-sizeof(struct ramoops_header)) return 2;
    return 0;
}
'''
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            c, exe = Path(tmp)/'test.c', Path(tmp)/'test'
            c.write_text(harness)
            subprocess.run(['cc', '-std=gnu11', str(c), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
