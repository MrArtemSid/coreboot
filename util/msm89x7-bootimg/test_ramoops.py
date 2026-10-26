#!/usr/bin/env python3
"""Host tests of the actual MSM89x7 watchdog function with recorded MMIO."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WatchdogTest(unittest.TestCase):
    def test_kpss_register_sequence(self):
        source = (ROOT / 'src/soc/qualcomm/msm89x7/ramoops.c').read_text()
        start = source.index('static void *ramoops_wdt_base(')
        end = source.index('\nstatic void ramoops_reset_header', start)
        body = source[start:end]
        harness = r'''
#include <stdint.h>
#include <stdio.h>
#define MSM89X7_WDT_BASE 0x0b017000
static void write32(void *p, uint32_t value) {
    printf("%lx %u\n", (unsigned long)(uintptr_t)p, value);
}
static void dsb(void) {}
'''+body+'\nint main(void) { msm89x7_wdt_arm(3000); return 0; }\n'
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            c = Path(tmp) / 'test.c'
            exe = Path(tmp) / 'test'
            c.write_text(harness)
            subprocess.run(['cc', '-std=gnu11', str(c), '-o', str(exe)], check=True)
            actual = subprocess.check_output([str(exe)], text=True).splitlines()
        # BARK stays at the 20-bit maximum, after the 3-second BITE,
        # so the watchdog resets before TZ can handle a bark interrupt.
        self.assertEqual(actual, [
            'b017008 0', 'b017004 1', 'b017010 1048575',
            'b017014 98304', 'b017008 1'])


class MemoryMapTest(unittest.TestCase):
    def test_santoni_cbmem_top_matches_lk_ram(self):
        source = (ROOT / 'src/soc/qualcomm/msm89x7/cbmem.c').read_text()
        start = source.index('uintptr_t cbmem_top_chipset(')
        end = source.index('\n}', start) + 2
        config = {}
        import re
        definitions = (ROOT / 'build/config.h').read_text()
        for name in ['MSM89X7_DDR_START', 'MSM89X7_DRAM_SIZE_MB']:
            config[name] = re.search(r'^#define CONFIG_' + name + r'\s+(\S+)',
                                     definitions, re.M).group(1)
        harness = '#include <stdint.h>\n#include <stdio.h>\n#define MiB (1024ULL*1024)\n'
        harness += ''.join(f'#define CONFIG_{k} {v}\n' for k, v in config.items())
        harness += source[start:end]
        harness += '\nint main(void) { printf("%llx", (unsigned long long)cbmem_top_chipset()); }\n'
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            c, exe = Path(tmp) / 'test.c', Path(tmp) / 'test'
            c.write_text(harness)
            subprocess.run(['cc', str(c), '-o', str(exe)], check=True)
            actual = subprocess.check_output([str(exe)], text=True)
        # Captured LK /memory reg: [0x40000000,0x80000000), [0x80000000,0x100000000).
        self.assertEqual(actual, '100000000')


class FaultMirrorTest(unittest.TestCase):
    def test_fault_text_and_registers_reach_ramoops(self):
        source = (ROOT / 'src/soc/qualcomm/msm89x7/bootblock.c').read_text()
        marker = 'static void mmu_diag_byte('
        start = source.index(marker if marker in source else 'void mmu_diag_text(')
        end = source.index('static int mmu_first_exception(', start)
        harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static char panel[1024], persistent[1024];
static unsigned p, r;
static void __display_console_tx_byte(unsigned char c) { panel[p++] = c; }
static void __ramoops_tx_byte(unsigned char c) { persistent[r++] = c; }
'''+source[start:end]+r'''
int main(void) {
    mmu_diag_text("FAULT\n");
    mmu_diag_hex("ESR", 0x96000010);
    if (strcmp(panel, persistent)) {
        fprintf(stderr, "panel=%s ramoops=%s", panel, persistent);
        return 1;
    }
    return strcmp(persistent, "FAULT\nESR=0x0000000096000010\n") != 0;
}
'''
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            c, exe = Path(tmp) / 'test.c', Path(tmp) / 'test'
            c.write_text(harness)
            subprocess.run(['cc', str(c), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
