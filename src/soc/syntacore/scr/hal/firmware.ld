/* SPDX-License-Identifier: GPL-2.0-only */

#include <config.h>
#include <soc/addressmap.h>

OUTPUT_ARCH(riscv)
/*
 * The single firmware= image is entered at the bootblock, not at the cluster
 * reset vector: on L2 the reset trampoline at the top of OCRAM is part of the
 * same raw image and jumps back to the bootblock.
 */
ENTRY(_start)

PHDRS
{
	bootblock PT_LOAD FLAGS(7);
	cbfs PT_LOAD FLAGS(4);
}

SECTIONS
{
	_start = SCR_OCRAM_BASE + SCR_OCRAM_BOOTBLOCK_OFFSET;
	.scr_bootblock _start : { *(.scr_bootblock) } :bootblock
	.scr_cbfs SCR_CBFS_BASE : { *(.scr_cbfs) } :cbfs

	ASSERT(SIZEOF(.scr_bootblock) <= CONFIG_SCR_OCRAM_SIZE - SCR_OCRAM_BOOTBLOCK_OFFSET,
		"Firmware bootblock exceeds OCRAM")
	ASSERT(SIZEOF(.scr_cbfs) == CONFIG_ROM_SIZE,
		"Firmware CBFS size differs from ROM size")
#if CONFIG(SCR_CLUSTER_L2)
	ASSERT(ADDR(.scr_bootblock) + SIZEOF(.scr_bootblock) > SCR_RESET_VECTOR,
		"Firmware is missing the L2 reset vector")
#endif
}
