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
/* UBOX and SAD CSRs answer on the uncore bus after the CPUBUSNO remap. */
#define GRANTLEY_UNCORE_BUS		0xff
#define GRANTLEY_CPUBUSNO		0x8000ff00
/* IIO bus rule value: the uncore bus range stacked on bus zero. */
#define GRANTLEY_IIO_BUS_RULE		0x1ff00

#endif /* CPU_INTEL_GRANTLEY_GRANTLEY_ROUTING_H */
