#!/usr/bin/env python3
"""Execute the real stage-transition hook with synthetic program types."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PayloadWatchdogTest(unittest.TestCase):
    def test_only_ramstage_payload_disarms_diagnostic_watchdog(self):
        source = (ROOT / 'src/soc/qualcomm/msm89x7/cbmem.c').read_text()
        hook = source[source.index('void platform_prog_run('):]
        watchdog = (ROOT / 'src/soc/qualcomm/msm89x7/ramoops.c').read_text()
        start = watchdog.index('void msm89x7_wdt_disable(')
        stop = watchdog.index('\nstatic void ramoops_reset_header', start)
        harness = r'''
#include <stdint.h>
#include <string.h>
#define CONFIG(option) TEST_RAMOOPS
#define ENV_RAMSTAGE TEST_RAMSTAGE
#define PROG_PAYLOAD 1
struct prog { int type; };
static unsigned disables, barriers, bytes, bytes_at_disable;
static int prog_type(struct prog *p) { return p->type; }
static const char *prog_name(struct prog *p) { return "test"; }
static void __ramoops_tx_byte(unsigned char c) { bytes++; }
static void *ramoops_wdt_base(void) { return (void *)0x0b017000; }
static void write32(void *p, uint32_t value) {
    if ((uintptr_t)p != 0x0b017008 || value) __builtin_trap();
    disables++; bytes_at_disable = bytes;
}
static void dsb(void) { barriers++; }
'''+watchdog[start:stop]+hook+r'''
int main(void) {
    struct prog p = {0};
    platform_prog_run(&p);
    if (disables || barriers) return 1;
    p.type = PROG_PAYLOAD;
    platform_prog_run(&p);
    unsigned expected = TEST_RAMSTAGE && TEST_RAMOOPS;
    if (disables != expected || barriers != expected) return 2;
    /* The final stage marker must precede disabling the timer. */
    if (expected && bytes_at_disable != bytes) return 3;
    return 0;
}
'''
        for ramstage in (0, 1):
            for ramoops in (0, 1):
                with self.subTest(ramstage=ramstage, ramoops=ramoops):
                    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
                        c, exe = Path(tmp) / 'test.c', Path(tmp) / 'test'
                        c.write_text(harness)
                        subprocess.run(['cc', '-std=gnu11', f'-DTEST_RAMSTAGE={ramstage}',
                                        f'-DTEST_RAMOOPS={ramoops}', str(c), '-o', str(exe)],
                                       check=True)
                        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
