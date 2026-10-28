/* SPDX-License-Identifier: GPL-2.0-only */

	/*
	 * Everything before romstage brings up DDR stays in OCRAM. The first
	 * 0x200 bytes hold the QEMU clock blob, the last 0x100 the reset vector.
	 */
	SRAM_START(SCR_OCRAM_BASE)
	BOOTBLOCK(SCR_OCRAM_BASE + 0x200, 28K - 0x200)
	CAR_STACK(SCR_OCRAM_BASE + 28K, 4K * CONFIG_MAX_CPUS)
	PRERAM_CBMEM_CONSOLE(SCR_OCRAM_BASE + 60K, 0xf00)
	_ = ASSERT(_epreram_cbmem_console - SCR_OCRAM_BASE <= CONFIG_SCR_OCRAM_SIZE - 0x100,
		   "Pre-RAM regions overlap the reset vector");
#if ENV_BOOTBLOCK && CONFIG(SCR_CLUSTER_L2)
	.reset_vector 0xffffffffffffff00 : {
		KEEP(*(.reset_vector))
	}
	_ = ASSERT(SIZEOF(.reset_vector) <= 0x100, "Reset vector exceeds OCRAM");
#endif
