/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_INTEL_GRANTLEY_GRANTLEY_ROUTING_H
#define CPU_INTEL_GRANTLEY_GRANTLEY_ROUTING_H

/* Shared by stackless assembly and the Uncore runner. */
#define GRANTLEY_ECAM_BASE		0x80000000
#define GRANTLEY_ECAM_LIMIT		0x8c000000
/* The limit register stores bits 31:26 of the inclusive decoded end. */
#define GRANTLEY_ECAM_END		((GRANTLEY_ECAM_LIMIT | 0x3ffffffULL) + 1)

/* Cold four-socket map: socket zero occupies buses 0x00 through 0x3f. */
#define GRANTLEY_COLD_UNCORE_BUS		0x3f
#define GRANTLEY_COLD_CPUBUSNO		0x80003f00
#define GRANTLEY_COLD_IIO_BUS_RULE	0x13f00
#define GRANTLEY_COLD_MMCFG_TARGET_LIST	0x006d2240

/* Warm single-socket map: all 256 buses route to socket zero. */
#define GRANTLEY_WARM_UNCORE_BUS		0xff
#define GRANTLEY_WARM_CPUBUSNO		0x8000ff00
#define GRANTLEY_WARM_IIO_BUS_RULE	0x1ff00
#define GRANTLEY_WARM_MMCFG_TARGET_LIST	0

#define GRANTLEY_CF8(bus, dev, fn, reg) \
	(0x80000000 | ((bus) << 16) | ((dev) << 11) | ((fn) << 8) | (reg))

#ifndef __ASSEMBLER__
#include <stdint.h>

#if ENV_TEST
uint8_t grantley_uncore_bus(void);
#else
#include <cpu/x86/msr.h>

static inline uint8_t grantley_uncore_bus(void)
{
	const msr_t buses = rdmsr(0x300);

	return buses.lo & (1U << 31) ? (buses.lo >> 8) & 0xff : 0;
}
#endif
#endif

#endif /* CPU_INTEL_GRANTLEY_GRANTLEY_ROUTING_H */
