#!/usr/bin/env python3
"""Exercise the actual display driver with synthetic MDP registers and RAM."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DisplayConsoleTest(unittest.TestCase):
    def run_driver(self, bootblock, mmu, ramoops=1):
        source = (ROOT / 'src/soc/qualcomm/msm89x7/display_console.c').read_text()
        source = '\n'.join(line for line in source.splitlines()
                           if not line.startswith('#include'))
        harness = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <soc/ramoops.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define ALIGN_UP(a,b) (((a)+(b)-1)&~((b)-1))
#define MDSS_MDP_BASE 0x01a00000
#define CONFIG(option) CONFIG_##option
#define CONFIG_MSM89X7_RAMOOPS_CONSOLE TEST_RAMOOPS
#define CONFIG_MSM89X7_FB_ADDR 0x90001000
#define CONFIG_MSM89X7_FB_WIDTH 720
#define CONFIG_MSM89X7_FB_HEIGHT 1280
#define BIOS_ERR 3
#define BIOS_NOTICE 5
#define ENV_RAMSTAGE (!ENV_BOOTBLOCK)
#define printk(...) ((void)0)
/* Synthetic IMEM persists independently of the driver's stage-local BSS. */
static uint32_t cursor[2] = {2, 3};
static unsigned int watchdog_pets;
void msm89x7_wdt_pet(void) { ++watchdog_pets; }
static uint64_t raw_read_sctlr(void) { return TEST_MMU; }
static uint32_t read32(void *p) {
    if ((uintptr_t)p == MSM89X7_IMEM_CURSOR_ADDR) return cursor[0];
    if ((uintptr_t)p == MSM89X7_IMEM_CURSOR_ADDR + 4) return cursor[1];
    uintptr_t reg = (uintptr_t)p - (MDSS_MDP_BASE + 0x15000);
    switch (reg) {
    case 0x14: return CONFIG_MSM89X7_FB_ADDR;
    case 0x00: return (1280 << 16) | 720;
    case 0x24: return 720 * 3;
    case 0x30: return 2 << 9;
    default: fprintf(stderr, "unexpected MMIO %p\n", p); exit(2);
    }
}
static void write32(void *p, uint32_t v) {
    if ((uintptr_t)p == MSM89X7_IMEM_CURSOR_ADDR) cursor[0] = v;
    else if ((uintptr_t)p == MSM89X7_IMEM_CURSOR_ADDR + 4) cursor[1] = v;
    else memcpy(p, &v, 4);
}
'''
        harness += source
        harness += r'''
int main(void) {
    size_t bytes = 720 * 1280 * 3;
    void *fb = mmap((void *)CONFIG_MSM89X7_FB_ADDR, bytes,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (fb == MAP_FAILED) { perror("mmap"); return 2; }
    memset(fb, 0x5a, bytes);
    __display_console_init();
    unsigned int expected_row = ENV_BOOTBLOCK ? 0 : 2;
    unsigned int expected_col = ENV_BOOTBLOCK ? 0 : 3;
    if (row != expected_row || col != expected_col ||
        cursor[0] != expected_row || cursor[1] != expected_col) {
        fprintf(stderr, "cursor was not reset/restored for this stage\n"); return 1;
    }
    if (!ENV_BOOTBLOCK && ((unsigned char *)fb)[bytes - 1] != 0x5a) {
        fprintf(stderr, "later stage cleared the inherited screen\n"); return 1;
    }
    unsigned char before[16 * 720 * 3];
    unsigned char *line = (unsigned char *)fb + expected_row * 16 * 720 * 3;
    memcpy(before, line, sizeof(before));
    __display_console_tx_byte('A');
    if (!memcmp(before, line, sizeof(before))) {
        fprintf(stderr, "no glyph output: bootblock=%d MMU=%d\n",
                ENV_BOOTBLOCK, TEST_MMU); return 1;
    }
    if (cursor[0] != expected_row || cursor[1] != expected_col + 1) {
        fprintf(stderr, "glyph output did not persist the cursor\n"); return 1;
    }
    if (watchdog_pets != TEST_RAMOOPS) {
        fprintf(stderr, "unexpected watchdog activity\n"); return 1;
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            c, exe = Path(tmp) / 'test.c', Path(tmp) / 'test'
            c.write_text(harness)
            subprocess.run(['cc', '-std=gnu11', f'-DENV_BOOTBLOCK={bootblock}',
                            f'-DTEST_MMU={mmu}', f'-DTEST_RAMOOPS={ramoops}',
                            '-I', str(ROOT / 'src/soc/qualcomm/msm89x7/include'),
                            str(c),
                            str(ROOT / 'src/soc/qualcomm/msm89x7/font_8x16.c'),
                            '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_bootblock_without_mmu(self):
        self.run_driver(1, 0)

    def test_bootblock_with_mmu(self):
        self.run_driver(1, 1)

    def test_later_stage_keeps_screen_and_outputs(self):
        self.run_driver(0, 1)

    def test_display_without_ramoops_does_not_pet_watchdog(self):
        self.run_driver(0, 1, ramoops=0)


if __name__ == '__main__':
    unittest.main()
