/* SPDX-License-Identifier: GPL-2.0-only */

	/*
	 * Everything before romstage brings up DDR stays in OCRAM. The first
	 * 0x200 bytes hold the QEMU clock blob. The L2 cluster resets into the
	 * trampoline in the last 0x100 bytes; L3 enters the bootblock directly.
	 */
	SRAM_START(SCR_OCRAM_BASE)
	BOOTBLOCK(SCR_OCRAM_BASE + SCR_OCRAM_BOOTBLOCK_OFFSET,
		  28K - SCR_OCRAM_BOOTBLOCK_OFFSET)
	CAR_STACK(SCR_OCRAM_BASE + 28K, 4K * CONFIG_MAX_CPUS)
	PRERAM_CBMEM_CONSOLE(SCR_OCRAM_BASE + 60K, 0xf00)
#if CONFIG(SCR_CLUSTER_L2)
	_ = ASSERT(_epreram_cbmem_console - SCR_OCRAM_BASE <= CONFIG_SCR_OCRAM_SIZE - 0x100,
		   "Pre-RAM regions overlap the reset vector");
#else
	_ = ASSERT(_epreram_cbmem_console - SCR_OCRAM_BASE <= CONFIG_SCR_OCRAM_SIZE,
		   "Pre-RAM regions exceed OCRAM");
#endif
#if ENV_BOOTBLOCK && CONFIG(SCR_CLUSTER_L2)
	.reset_vector SCR_RESET_VECTOR : {
		KEEP(*(.reset_vector))
	}
	_ = ASSERT(SIZEOF(.reset_vector) <= 0x100, "Reset vector exceeds OCRAM");
#endif
