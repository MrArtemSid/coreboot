#!/usr/bin/env python3
"""Host test actual SoC resource hooks with a synthetic LK device tree."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MemoryResourcesTest(unittest.TestCase):
    def test_dt_ram_is_resource_not_late_bootmem_override(self):
        source = (ROOT / 'src/soc/qualcomm/msm89x7/soc.c').read_text()
        source = '\n'.join(s for s in source.splitlines() if not s.startswith('#include'))
        harness = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef uint32_t u32;
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MiB (1024ULL*1024)
#define CONFIG_MSM89X7_DRAM_SIZE_MB 3072
#define CONFIG_ROM_SIZE 0x1000000
#define CONFIG(option) CONFIG_##option
#define CONFIG_MSM89X7_RAMOOPS_CONSOLE 1
#define CONFIG_MSM89X7_RAMOOPS_ADDR 0xbffc0000
#define CONFIG_MSM89X7_RAMOOPS_SIZE 0x40000
#define BIOS_INFO 6
#define printk(...) ((void)0)
#define SMEM_BASE 0x86300000
#define SMEM_SIZE 0x200000
static char _bootblock[1], _lk_dtb[8];
#define _dram ((void *)0x40000000)
struct device { int dummy; };
struct device_tree_region { uint64_t addr, size; };
enum bootmem_type { BM_MEM_RAM, BM_MEM_RESERVED };
static int ram_resources, late_ram, reservations, ramoops_reserved, diag_reserved;
static void ram_range(struct device *dev, unsigned idx, uint64_t base, uint64_t size) {
    if (base == 0x40000000 && size == 0xc0000000) ram_resources++;
    else { fprintf(stderr, "wrong RAM resource %llx+%llx\n",
                    (unsigned long long)base, (unsigned long long)size); exit(1); }
}
static void bootmem_add_range(uint64_t base, uint64_t size, enum bootmem_type tag) {
    if (tag == BM_MEM_RAM) late_ram++;
    else reservations++;
    if (tag == BM_MEM_RESERVED && base == CONFIG_MSM89X7_RAMOOPS_ADDR &&
        size == CONFIG_MSM89X7_RAMOOPS_SIZE) ramoops_reserved++;
    if (tag == BM_MEM_RESERVED && base == 0xbfffff80 && size == 0x80) diag_reserved++;
}
static uint64_t read64(void *p) { return 0x12340000; }
static bool fdt_is_valid(const void *p) { return true; }
static u32 fdt_find_node_by_path(const void *p, const char *path, u32 *a, u32 *s) {
    *a = *s = 2;
    return !strcmp(path, "/memory") ? 1 : 2;
}
static size_t fdt_read_reg_prop(const void *p, u32 off, u32 a, u32 s,
                               struct device_tree_region *r, size_t n) {
    r[0].addr = off == 1 ? 0x40000000 : 0x85e00000;
    r[0].size = off == 1 ? 0xc0000000 : 0x200000;
    return 1;
}
static size_t fdt_find_subnodes_by_prefix(const void *p, u32 off, const char *pre,
                        u32 *a, u32 *s, u32 *nodes, size_t n) { nodes[0] = 3; return 1; }
static void clock_init(void) {}
struct device_operations {
    void (*read_resources)(struct device *);
    void (*init)(struct device *);
};
struct chip_operations { const char *name; void (*enable_dev)(struct device *); };
'''
        harness = harness.replace('struct device { int dummy; };',
                                  'struct device_operations;\nstruct device { struct device_operations *ops; };')
        harness += source
        harness += r'''
int main(void) {
    struct device dev = {0};
    soc_read_resources(&dev);
    bootmem_platform_add_ranges();
    if (late_ram) { fprintf(stderr, "DT RAM overwrites existing CBMEM/stage reservations\n"); return 1; }
    if (ram_resources != 1 || reservations < 3 ||
        ramoops_reserved != 1 || diag_reserved != 1) {
        fprintf(stderr, "RAM resources=%d reservations=%d\n", ram_resources, reservations); return 1;
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            c, exe = Path(tmp) / 'test.c', Path(tmp) / 'test'
            c.write_text(harness)
            subprocess.run(['cc', '-std=gnu11', str(c), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
