#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Hosted Grantley memory map regressions; no firmware or hardware access."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
STUBS = r"""
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#define ENV_SEPARATE_ROMSTAGE 1
#define CONFIG(x) 1
#define CONFIG_SMM_TSEG_SIZE (8 * MiB)
#define MiB (1024UL * 1024)
#define ALIGN_DOWN(x,a) ((x) & ~((a)-1))
#define MTRR_TYPE_WRBACK 6
#define PCI_DEV(b,d,f) 0
#define __noreturn __attribute__((noreturn))
typedef uint32_t pci_devfn_t;
struct postcar_frame { int unused; };
static jmp_buf failure;
static unsigned char saved[8192];
static int saved_valid;
static uint32_t pci_s_read_config32(pci_devfn_t dev, unsigned int reg)
{ (void)dev; (void)reg; return 0; }
static __noreturn void die(const char *msg)
{ (void)msg; longjmp(failure, 1); }
static void *cbmem_find(unsigned int id)
{ (void)id; return saved_valid ? saved : NULL; }
static void *cbmem_add(unsigned int id, size_t size)
{ (void)id; if (size > sizeof(saved)) return NULL; saved_valid = 1; return saved; }
uintptr_t cbmem_top_chipset(void);
static uintptr_t cbmem_top(void) { return cbmem_top_chipset(); }
struct resource_record { uint64_t base, size; int type; };
static struct resource_record records[256];
static unsigned int record_count;
struct device { int unused; };
static void pci_domain_read_resources(struct device *dev) { (void)dev; }
static void record_range(struct device *dev, int idx, uint64_t base, uint64_t size, int type)
{
	(void)dev; (void)idx;
	if (size) records[record_count++] = (struct resource_record) { base, size, type };
}
#define ram_range(d,i,b,s) record_range(d,i,b,s,1)
#define reserved_ram_range(d,i,b,s) record_range(d,i,b,s,2)
#define mmio_range(d,i,b,s) record_range(d,i,b,s,3)
#define ram_from_to(d,i,b,e) ram_range(d,i,b,(e)-(b))
#define reserved_ram_from_to(d,i,b,e) reserved_ram_range(d,i,b,(e)-(b))
#define mmio_from_to(d,i,b,e) mmio_range(d,i,b,(e)-(b))
#define GRANTLEY_DEFAULT_TOLUM 0x80000000UL
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
static uintptr_t mtrr_base;
static size_t mtrr_size;
static void postcar_frame_add_mtrr(struct postcar_frame *p, uintptr_t b, size_t s, int t)
{ (void)p; (void)t; mtrr_base = b; mtrr_size = s; }
"""
TEST = r"""
int main(void)
{
	if (setjmp(failure) == 0) {
		(void)cbmem_top_chipset();
		fputs("FAIL: unavailable memory map must fail closed, not guess 2 GiB\n", stderr);
		return 1;
	}
	puts("PASS: missing map fails closed");
	struct grantley_memory_map map = {
		.count = 4,
		.ranges = {
			{ 0, 0xa0000, GRANTLEY_MEMORY_RAM },
			{ MiB, 0x60000000 - MiB, GRANTLEY_MEMORY_RAM },
			{ 0x60000000, 0x20000000, GRANTLEY_MEMORY_RESERVED_RAM },
			{ 0x100000000ULL, 0x100000000ULL, GRANTLEY_MEMORY_RAM },
		},
		.smram_base = 0x70000000,
		.smram_size = 16 * MiB,
	};
	if (!grantley_memmap_install(&map)) return 2;
	if (cbmem_top_chipset() != 0x60000000) return 3;
	uintptr_t smram_base;
	size_t smram_size;
	smm_region(&smram_base, &smram_size);
	if (smram_base != 0x70000000 || smram_size != 16 * MiB) return 4;
	grantley_memmap_save();
	if (!saved_valid || !grantley_get_memory_map()) return 5;
	puts("PASS: actual reserved bounds separate CBMEM from SMRAM");
#define REJECT(change) do { \
	struct grantley_memory_map bad = map; \
	change; \
	if (grantley_memmap_install(&bad)) { \
		fprintf(stderr, "FAIL: accepted invalid map: %s\\n", #change); \
		return 6; \
	} \
} while (0)
	REJECT(bad.ranges[1].size = 0);
	REJECT(bad.ranges[3].size = UINT64_MAX);
	REJECT(bad.ranges[2].type = 0);
	REJECT(bad.ranges[1].base = 0x90000);
	REJECT(bad.count = GRANTLEY_MAX_MEMORY_RANGES + 1);
	REJECT(bad.smram_size = 0);
	REJECT(bad.smram_size = MiB);
	REJECT(bad.smram_base = 0x50000000);
	REJECT(bad.smram_base = 0xfffff000);
	REJECT(bad.smram_base++);
	REJECT(bad.ranges[2].type = GRANTLEY_MEMORY_RAM);
	REJECT(bad.ranges[1].size = MiB);
	if (setjmp(failure) == 0) {
		(void)cbmem_top_chipset();
		fputs("FAIL: rejected install reused stale saved map\\n", stderr);
		return 7;
	}
	if (!grantley_memmap_install(&map)) return 8;
	puts("PASS: malformed/overlapping/unsafe maps rejected without stale fallback");
	struct device dev;
	grantley_domain_read_resources(&dev);
	bool smram_reserved = false, high_ram = false;
	for (unsigned int i = 0; i < record_count; i++) {
		struct resource_record *r = &records[i];
		if (r->base <= map.smram_base &&
		    r->base + r->size >= map.smram_base + map.smram_size) {
			if (r->type != 2) {
				fputs("FAIL: SMM region is not reserved cacheable RAM\n", stderr);
				return 9;
			}
			smram_reserved = true;
		}
		if (r->base == 0x100000000ULL && r->size == 0x100000000ULL && r->type == 1)
			high_ram = true;
	}
	if (!smram_reserved || !high_ram) return 10;
	puts("PASS: resources retain reserved RAM/WB SMRAM and actual high RAM");
	if (late_cbmem_top_chipset() != 0x60000000) return 11;
	late_smm_region(&smram_base, &smram_size);
	if (smram_base != map.smram_base || smram_size != map.smram_size) return 12;
	struct postcar_frame pcf;
	late_fill_postcar_frame(&pcf);
	if (mtrr_base != 0x60000000 - 8 * MiB || mtrr_size != 8 * MiB) return 13;
	((struct grantley_memmap_handoff *)saved)->map.ranges[1].size = 0;
	if (setjmp(failure) == 0) {
		(void)late_get_memory_map();
		fputs("FAIL: late stage accepted corrupt memory map\n", stderr);
		return 14;
	}
	grantley_memmap_save();
	((struct grantley_memmap_handoff *)saved)->version++;
	if (setjmp(failure) == 0) {
		(void)late_get_memory_map();
		fputs("FAIL: late stage accepted incompatible memory map\n", stderr);
		return 15;
	}
	puts("PASS: cross-stage map revalidated; postcar WB window excludes reserved DRAM");
	return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="grantley-memmap-", dir=os.environ.get("TMPDIR")) as work:
    work = Path(work)
    for header in ["arch/romstage.h", "cbmem.h", "commonlib/helpers.h", "console/console.h",
                   "cpu/x86/mtrr.h", "cpu/x86/smm.h", "device/pci_ops.h", "types.h", "rules.h"]:
        path = work / header
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("")
    source = work / "test.c"
    # Compile the real resource functions, without unrelated CPU/MP dependencies.
    northbridge = (ROOT / "src/northbridge/intel/grantley/northbridge.c").read_text()
    resources = northbridge[northbridge.index("static void grantley_domain_read_resources"):northbridge.index("static void grantley_domain_set_resources")]
    include_map = '\n#include "' + str(ROOT / "src/northbridge/intel/grantley/memmap.c") + '"\n'
    late_stage = r"""
#undef ENV_SEPARATE_ROMSTAGE
#define ENV_SEPARATE_ROMSTAGE 0
#define grantley_memmap_handoff late_handoff
#define memory_map_top late_memory_map_top
#define memory_map_valid late_memory_map_valid
#define grantley_get_memory_map late_get_memory_map
#define cbmem_top_chipset late_cbmem_top_chipset
#define smm_region late_smm_region
#define fill_postcar_frame late_fill_postcar_frame
""" + include_map + r"""
#undef grantley_memmap_handoff
#undef memory_map_top
#undef memory_map_valid
#undef grantley_get_memory_map
#undef cbmem_top_chipset
#undef smm_region
#undef fill_postcar_frame
"""
    source.write_text(STUBS + '\n#include "' + str(ROOT / "src/northbridge/intel/grantley/memmap.h") + '"\n' + include_map + late_stage + resources + TEST)
    exe = work / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-I", str(work), str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    abi = work / "abi.c"
    abi.write_text('#include "' + str(ROOT / "src/northbridge/intel/grantley/memmap.h") + '"\n'
                   '_Static_assert(sizeof(struct grantley_memory_range) == 24, "range ABI");\n'
                   '_Static_assert(offsetof(struct grantley_memory_map, ranges) == 8, "map ABI");\n'
                   '_Static_assert(sizeof(struct grantley_memory_map) == 24 + 64 * 24, "map size");\n')
    for arch in ["-m32", "-m64"]:
        subprocess.run(["cc", arch, "-ffreestanding", "-std=c11", "-fsyntax-only", str(abi)], check=True)
    print("PASS: identical 32-/64-bit memory map ABI")
