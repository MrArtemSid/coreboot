/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_INTEL_GRANTLEY_GRANTLEY_ROUTING_H
#define CPU_INTEL_GRANTLEY_GRANTLEY_ROUTING_H

/* Single-socket pre-Uncore CSR routing contract, shared by the CAR entry
 * assembly (early_startup.inc) and the Uncore PEI runner.  Plain integer
 * constants only: this header is included from assembly. */

/* One 256-bus ECAM segment for socket zero, below the IA-32 ceiling. */
#define GRANTLEY_ECAM_BASE		0x80000000
#define GRANTLEY_ECAM_LIMIT		0x8c000000
/* UBOX and SAD CSRs answer on the uncore bus after the CPUBUSNO remap. */
#define GRANTLEY_UNCORE_BUS		0xff
#define GRANTLEY_CPUBUSNO		0x8000ff00
/* IIO bus rule value: the uncore bus range stacked on bus zero. */
#define GRANTLEY_IIO_BUS_RULE		0x1ff00

#endif /* CPU_INTEL_GRANTLEY_GRANTLEY_ROUTING_H */
