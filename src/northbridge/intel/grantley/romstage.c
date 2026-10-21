/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/romstage.h>
#include <cbmem.h>
#include <cbfs.h>
#include <console/console.h>
#include <cpu/cpu.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <device/smbus_host.h>
#include <northbridge/intel/grantley/native_raminit/spd.h>
#include <northbridge/intel/grantley/uncore_runner/pei_services.h>
#include <post.h>
#include <romstage_common.h>
#include <romstage_handoff.h>
#include "platform.h"

void __weak mainboard_memory_init_params(struct grantley_memory_params *params)
{
	/* Default implementation: leave all parameters as 0 for safe auto detection */
}

/* Static CAR storage for PEI runner services and HOB list */
static struct grantley_uncore_state uncore_state;
static uint8_t pei_pool[64 * 1024] __aligned(16);
static uint8_t pei_hob[16 * 1024] __aligned(16);

void __noreturn romstage_main(void)
{
	uint32_t signature = cpuid_eax(1);
	uint32_t host = pci_s_read_config32(PCI_DEV(0, 0, 0), PCI_VENDOR_ID);
	uint32_t pch = pci_s_read_config32(PCI_DEV(0, 0x1f, 0), PCI_VENDOR_ID);

	printk(BIOS_INFO, "Grantley diagnostic: CPUID %08x, host %08x, LPC %08x\n",
	       signature, host, pch);
	printk(BIOS_INFO, "CPU model: %s\n",
	       grantley_cpu_model(signature) == GRANTLEY_HASWELL_EP ?
	       "Haswell-EP" : "Broadwell-EP (not hardware-validated)");

	/* Probe SMBus SPD on standard DIMM addresses (0x50..0x57) */
	enable_smbus();
	for (uint8_t addr = 0x50; addr <= 0x57; addr++) {
		struct dimm_attr_ddr4_st dimm;
		enum grantley_spd_status st = grantley_read_spd(smbus_base(), addr, &dimm);
		if (st == GRANTLEY_SPD_OK) {
			printk(BIOS_INFO, "DIMM at 0x%02x: %u MB, %u ranks, type %u, part %s\n",
			       addr, dimm.size_mb, dimm.ranks, dimm.dimm_type, dimm.part_number);
		}
	}

	/* Check for Intel Grantley Uncore reference blob (UncoreInitPeim) */
	if (CONFIG(HAVE_INTEL_UNCORE_BIN)) {
		size_t uncore_size = 0;
		const void *uncore_bin = cbfs_ro_map("uncore.bin", &uncore_size);

		if (!uncore_bin) {
			printk(BIOS_ERR, "Grantley: uncore.bin not found in CBFS\n");
			post_code(0xee);
			die("Grantley: uncore.bin required but missing from CBFS!\n");
		}

		printk(BIOS_INFO, "Grantley: found uncore.bin (%zu bytes) at %p\n",
		       uncore_size, uncore_bin);

		/* Initialise PEI Services in CAR */
		EFI_STATUS status = grantley_pei_services_init(
			&uncore_state,
			pei_pool, sizeof(pei_pool),
			pei_hob, sizeof(pei_hob));

		if (status != EFI_SUCCESS) {
			printk(BIOS_ERR, "Grantley: failed to init PEI services: 0x%lx\n",
			       (unsigned long)status);
			post_code(0xef);
			die("Grantley: PEI services init failure\n");
		}

		/* Enable MMCFG at standard Grantley ECAM base (0x80000000) */
		grantley_pei_set_mmcfg(&uncore_state, 0x80000000UL);

		/* Query mainboard for optional memory parameter overrides */
		struct grantley_memory_params mem_params = { 0 };
		mainboard_memory_init_params(&mem_params);
		grantley_pei_set_mem_params(&uncore_state,
					    mem_params.max_dram_speed_mts,
					    mem_params.tcl,
					    mem_params.trcd,
					    mem_params.trp,
					    mem_params.tras,
					    mem_params.command_rate,
					    mem_params.ecc_en);

		post_code(0x35);
		printk(BIOS_INFO, "Grantley: launching UncoreInitPeim memory training...\n");

		status = grantley_run_uncore_peim(uncore_bin, &uncore_state);
		if (status != EFI_SUCCESS) {
			printk(BIOS_ERR, "Grantley: uncore PEIM returned error: 0x%lx\n",
			       (unsigned long)status);
			post_code(0xe4);
			die("Grantley: uncore memory initialization failed!\n");
		}

		post_code(0x36);
		printk(BIOS_INFO, "Grantley: DRAM training complete! Detected %llu MB\n",
		       (unsigned long long)(uncore_state.dram_size >> 20));

		/* Memory is online: initialise CBMEM and hand off to postcar */
		cbmem_initialize_empty();
		romstage_handoff_init(false);

		post_code(0x37);
		printk(BIOS_INFO, "Grantley: tearing down CAR and transferring to ramstage...\n");
		prepare_and_run_postcar();
	}

	/* Do not invoke the client MRC, train DRAM, publish CBMEM or leave CAR. */
	post_code(0xe3);
	die("Grantley: stopping before DRAM training; native controller init not implemented.\n");
}
