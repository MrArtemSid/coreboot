/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_INTEL_GRANTLEY_GRANTLEY_ROUTING_H
#define CPU_INTEL_GRANTLEY_GRANTLEY_ROUTING_H

/* Single-socket pre-Uncore CSR routing contract, shared by the CAR entry
 * assembly (early_startup.inc) and the Uncore PEI runner.  Plain integer
 * constants only: this header is included from assembly. */

/* One 256-bus ECAM segment for socket zero, below the IA-32 ceiling.  LIMIT
 * is what the IIO limit register is loaded with: it stores address bits 31:26
 * (early_startup.inc:97-99), so a limit of 0x8c000000 decodes through
 * 0x8fffffff and the segment is 256 MiB = 256 buses (acpi_fill_mcfg).  The
 * decoded end, not the encoded field, is what may be reserved as MMIO. */
#define GRANTLEY_ECAM_BASE		0x80000000
#define GRANTLEY_ECAM_LIMIT		0x8c000000
#define GRANTLEY_ECAM_END		((GRANTLEY_ECAM_LIMIT | 0x3ffffffULL) + 1)
/* Cold-boot four-socket scheme: socket 0 IIO on bus 0, uncore on 0x3f. */
#define GRANTLEY_UNCORE_BUS		0x3f
#define GRANTLEY_CPUBUSNO		0x80003f00
/* IIO bus rule value: the uncore bus range stacked on bus zero. */
#define GRANTLEY_IIO_BUS_RULE		0x13f00
/* Cbo MMCFG target list for four sockets: 32-bus fields [3,3,2,2,1,1,0,0],
 * so buses 0x00-0x3f decode to node zero and 0x40-0x7f to node one. */
#define GRANTLEY_MMCFG_TARGET_LIST	0x006d2240

/* Legacy CF8 selector. */
#define GRANTLEY_CF8(bus, dev, fn, reg) \
	(0x80000000 | ((bus) << 16) | ((dev) << 11) | ((fn) << 8) | (reg))
/* Uncore bus register in the ECAM window. */
#define GRANTLEY_UNCORE_MMIO(offset) \
	(GRANTLEY_ECAM_BASE + (GRANTLEY_UNCORE_BUS << 20) + (offset))

#endif /* CPU_INTEL_GRANTLEY_GRANTLEY_ROUTING_H */
