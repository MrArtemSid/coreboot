/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/barrier.h>
#include <arch/smp/smp.h>
#include <commonlib/device_tree.h>
#include <console/console.h>
#include <endian.h>
#include <mcall.h>
#include <soc/scr_hal.h>

/* Each loaded stage owns its initialization flag; no BSS reset is assumed. */
static int iccm_ready __section(".data") = 0;
#if CONFIG(RISCV_GET_HART_COUNT_AT_RUNTIME)
static int hart_ready[CONFIG_MAX_CPUS] __section(".data") = { 0 };
#endif

void scr_iccm_init(void)
{
	/* MMCFG access is hart-local; MRT and ICCM control are shared. */
	scr_hal_pmp_platform_init();
	if (read_csr(mhartid) == CONFIG_RISCV_WORKING_HARTID) {
		if (ENV_BOOTBLOCK)
			scr_hal_mrt_init();
		scr_hal_iccm_init();
		__atomic_store_n(&iccm_ready, 1, __ATOMIC_RELEASE);
	} else {
		while (!__atomic_load_n(&iccm_ready, __ATOMIC_ACQUIRE))
			;
	}
#if CONFIG(RISCV_GET_HART_COUNT_AT_RUNTIME)
	/* hls_init() has cleared this hart's native synchronization fields. */
	__atomic_store_n(&hart_ready[read_csr(mhartid)], 1, __ATOMIC_RELEASE);
#endif
}

/* Linux describes CSR ICCM slots separately from hardware hart IDs. */
static unsigned int csr_iccm_slot(unsigned int hartid)
{
	const void *fdt = HLS()->fdt;
	struct fdt_property map, prop;
	u32 cpus[32];

	if (!fdt || !fdt_is_valid(fdt))
		die("ICCM requires the Linux board FDT");
	u32 node = fdt_find_node_by_path(fdt, "/soc/cluster_bus/iccm", NULL, NULL);

	if (!node || !fdt_read_prop(fdt, node, "mbox-mapping", &map) || map.size % 8)
		die("Missing or invalid ICCM mbox-mapping");
	node = fdt_find_node_by_path(fdt, "/cpus", NULL, NULL);
	size_t count = fdt_find_subnodes_by_prefix(fdt, node, "cpu@", NULL, NULL,
						 cpus, ARRAY_SIZE(cpus));
	for (size_t i = 0; i < count; i++) {
		if (!fdt_read_prop(fdt, cpus[i], "reg", &prop) ||
		    (prop.size != 4 && prop.size != 8) ||
		    fdt_read_int_prop(&prop, 0) != hartid)
			continue;
		if (!fdt_read_prop(fdt, cpus[i], "phandle", &prop) || prop.size != 4)
			break;
		u32 phandle = fdt_read_int_prop(&prop, 1);

		for (size_t j = 0; j < map.size; j += 8) {
			const char *entry = (const char *)map.data + j;

			if (be32dec(entry) == phandle)
				return be32dec(entry + 4);
		}
		break;
	}
	die("Hart missing from ICCM mbox-mapping");
}

void set_msip(int hartid, int val)
{
	if (!val) {
		/* The native coreboot backend clears only its own pending interrupt. */
		if (hartid != read_csr(mhartid) || !scr_hal_iccm_clear())
			die("Cannot clear local ICCM mailbox");
		barrier();
		return;
	}

	unsigned int slot;

	if (CONFIG(SOC_SYNTACORE_SCR9_L3) || CONFIG(SOC_SYNTACORE_SCR7_L3))
		/* The Linux MMIO binding and OpenSBI driver index by physical hart ID. */
		slot = hartid;
	else
		slot = csr_iccm_slot(hartid);
	/* Publish normal memory before the CSR/MMIO notification. */
	barrier();
	if (!scr_hal_iccm_send(slot))
		die("ICCM wakeup failed");
	barrier();
}

#if CONFIG(RISCV_GET_HART_COUNT_AT_RUNTIME)
unsigned int smp_get_hart_count(void)
{
	unsigned int count;

	if (CONFIG(SOC_SYNTACORE_SCR9_L3)) {
		count = scr_hal_iccm_hart_count();
	} else {
		/* CSR ICCM reports slot capacity, not the number of active harts. */
		const void *fdt = HLS()->fdt;
		u32 cpus[CONFIG_MAX_CPUS + 1];

		if (!fdt || !fdt_is_valid(fdt))
			die("SMP requires the Linux board FDT");
		u32 node = fdt_find_node_by_path(fdt, "/cpus", NULL, NULL);

		count = fdt_find_subnodes_by_prefix(fdt, node, "cpu@", NULL, NULL,
						    cpus, ARRAY_SIZE(cpus));
	}
	if (!count || count > CONFIG_MAX_CPUS)
		die("Hart count exceeds reserved stacks");
	for (unsigned int i = 0; i < count; i++) {
		if (i == CONFIG_RISCV_WORKING_HARTID)
			continue;
		while (!__atomic_load_n(&hart_ready[i], __ATOMIC_ACQUIRE))
			;
		/* Native smp_pause() publishes HART_SLEEPING (1) before WFI. */
		while (__atomic_load_n(&OTHER_HLS(i)->entry.sync_a.counter,
				       __ATOMIC_ACQUIRE) != 1)
			;
	}
	return count;
}
#endif
