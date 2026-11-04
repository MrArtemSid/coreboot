/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/romstage.h>
#include <arch/symbols.h>
#include <cbmem.h>
#include <cbfs.h>
#include <console/console.h>
#include <cpu/cpu.h>
#include <cpu/intel/grantley/grantley.h>
#include <cpu/intel/grantley/grantley_routing.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <device/smbus_host.h>
#include <northbridge/intel/grantley/native_raminit/spd.h>
#include <northbridge/intel/grantley/uncore_runner/native_contract.h>
#include <northbridge/intel/grantley/uncore_runner/pei_pcd.h>
#include <northbridge/intel/grantley/uncore_runner/pei_services.h>
#include <post.h>
#include <romstage_common.h>
#include <romstage_handoff.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include "platform.h"
#include "memmap.h"

void __weak mainboard_memory_init_params(struct grantley_memory_params *params)
{
	/* Default implementation: leave all parameters as 0 for safe auto detection */
}

bool __weak mainboard_get_board_identity(struct grantley_board_identity *identity)
{
	return false;
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

	/* Initialize PCH BARs, GPIOs and un-halt timers */
	early_pch_init();

	printk(BIOS_INFO, "Grantley diagnostic: CPUID %08x, host %08x, LPC %08x\n",
	       signature, host, pch);
	printk(BIOS_INFO, "CPU model: %s\n",
	       grantley_cpu_model(signature) == GRANTLEY_HASWELL_EP ?
	       "Haswell-EP" : "Broadwell-EP (not hardware-validated)");

	if (!grantley_pch_is_supported(pch & 0xffff, pch >> 16))
		printk(BIOS_WARNING, "Grantley: unrecognized PCH %04x:%04x, "
		       "continuing with unvalidated early setup\n",
		       pch & 0xffff, pch >> 16);

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
		/* Keep all CAR statics, including the PEI arenas and private IDT,
		 * below a dedicated 768 KiB descending stack.  The selected entry
		 * frame alone is 0x6db38 bytes; maximum nested usage is not proven.
		 */
		const uintptr_t stack_top =
			CONFIG_DCACHE_RAM_BASE + CONFIG_DCACHE_RAM_SIZE - 64;
		if (CONFIG_DCACHE_RAM_SIZE <= 768 * 1024 + 64 ||
		    (uintptr_t)_car_unallocated_start > stack_top - 768 * 1024)
			die("Grantley: CAR static storage overlaps the PEIM stack\n");
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

		/* The board contract is the only source of the PCD base policy.
		 * QD4's module calls Get64(1): the value must be supplied and
		 * must EQUAL the base this port programs below.  X99D4M4's
		 * module carries the same base as a compiled-in constant and
		 * never calls the getter; the runner checks those bytes
		 * instead, so no runtime token is required there.
		 */
#if CONFIG(GRANTLEY_UNCORE_NATIVE_ENTRY)
		const struct grantley_native_contract *contract =
			grantley_native_contract();

		if (!contract) {
			post_code(0xe6);
			die("Grantley: no native contract is configured for this board\n");
		}
		if (contract->pcd_mode == GRANTLEY_NATIVE_PCD_RUNTIME_TOKEN1) {
			if (CONFIG_GRANTLEY_UNCORE_PCD_MMCFG_BASE !=
			    contract->pcd_token1) {
				post_code(0xe6);
				die("Grantley: PCD token 1 config 0x%llx does not "
				    "match the %s contract value 0x%llx\n",
				    (unsigned long long)CONFIG_GRANTLEY_UNCORE_PCD_MMCFG_BASE,
				    contract->board,
				    (unsigned long long)contract->pcd_token1);
			}
			if (grantley_pcd_set_mmcfg_base(
			    contract->pcd_token1, GRANTLEY_ECAM_BASE) != 0) {
				post_code(0xe6);
				printk(BIOS_ERR, "Grantley: PCD token 1 value 0x%llx "
				       "rejected; it must equal GRANTLEY_ECAM_BASE "
				       "0x%llx\n",
				       (unsigned long long)contract->pcd_token1,
				       (unsigned long long)GRANTLEY_ECAM_BASE);
			}
		} else {
			if (CONFIG_GRANTLEY_UNCORE_PCD_MMCFG_BASE !=
			    contract->pcd_ecam_value) {
				post_code(0xe6);
				die("Grantley: MMCFG base config 0x%llx does not "
				    "match the %s contract constant 0x%llx\n",
				    (unsigned long long)CONFIG_GRANTLEY_UNCORE_PCD_MMCFG_BASE,
				    contract->board,
				    (unsigned long long)contract->pcd_ecam_value);
			}
			grantley_pcd_set_token1_required(false);
		}
#endif

		/* A board's stock IntelSetup bytes are never served implicitly:
		 * only a contract with a compiled-in payload can enable them. */
#if CONFIG(GRANTLEY_UNCORE_NATIVE_ENTRY)
		grantley_intelsetup_allow_stock(contract->intelsetup_payload != NULL);
#else
		grantley_intelsetup_allow_stock(false);
#endif

		/* Enable MMCFG at standard Grantley ECAM base (0x80000000) */
		status = grantley_pei_set_mmcfg(&uncore_state, GRANTLEY_ECAM_BASE);
		if (status != EFI_SUCCESS) {
			post_code(0xee);
			die("Grantley: uncore configuration routing failed\n");
		}

		struct grantley_board_identity identity = { 0 };
		if (mainboard_get_board_identity(&identity)) {
			status = grantley_pei_set_board_identity(&uncore_state,
				identity.board_id, identity.policy_revision);
			if (status != EFI_SUCCESS)
				die("Grantley: PlatformInfo board identity failure\n");
		}

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
#if CONFIG(GRANTLEY_UNCORE_REQUIRE_NATIVE_CONTRACT) && \
	!CONFIG(GRANTLEY_UNCORE_NATIVE_ENTRY)
		/* The runner refuses in this configuration; do not print a
		 * "launching ... memory training" line for an entry that will
		 * not happen.
		 */
		printk(BIOS_ERR, "Grantley: this board requires its native entry "
		       "contract; UncoreInitPeim will not be entered\n");
#else
		printk(BIOS_INFO, "Grantley: launching UncoreInitPeim memory training...\n");
#endif

		status = grantley_run_uncore_peim(uncore_bin, uncore_size,
						  &uncore_state);
		if (status != EFI_SUCCESS) {
			printk(BIOS_ERR, "Grantley: uncore PEIM not run: 0x%lx "
			       "(stop %d)\n", (unsigned long)status,
			       uncore_state.stop_reason);
			post_code(0xe4);
			die("Grantley: uncore memory initialization failed!\n");
		}

		/* Verify the PEIM's HOBs against current hardware before touching CBMEM. */
		const pci_devfn_t dev = PCI_DEV(0, 5, 0);
		uint32_t tolm = pci_s_read_config32(dev, 0xd0);
		uint32_t tohm_lo = pci_s_read_config32(dev, 0xd4);
		uint32_t tohm_hi = pci_s_read_config32(dev, 0xd8);
		struct grantley_memory_map memory_map;
		status = grantley_pei_memory_map(&uncore_state, tolm, tohm_lo, tohm_hi,
						&memory_map);
		/* The SMRAM HOB reports the cacheable TSEG only; the vendor IED
		 * above it is still inside the TSEG the IIO decodes, and coreboot's
		 * TSEG layout carves the IED off the top of smm_region(). Bind the
		 * map to the TSEG registers instead of guessing the size. */
		const bool tseg_ok = status == EFI_SUCCESS &&
			grantley_memmap_bind_tseg(&memory_map,
						  pci_s_read_config32(dev, 0xa8),
						  pci_s_read_config32(dev, 0xac),
						  CONFIG_IED_REGION_SIZE,
						  CONFIG_SMM_RESERVED_SIZE +
						  CONFIG_SMM_OPAL_S3_STATE_SMRAM_SIZE);
		if (!tseg_ok || !grantley_memmap_install(&memory_map)) {
			post_code(0xe5);
			die("Grantley: invalid post-PEIM memory map\n");
		}

		post_code(0x36);
		printk(BIOS_INFO, "Grantley: validated %llu memory ranges, SMRAM %llx+%llx\n",
		       (unsigned long long)memory_map.count,
		       (unsigned long long)memory_map.smram_base,
		       (unsigned long long)memory_map.smram_size);

		/* Persist the validated physical reservations before leaving CAR. */
		cbmem_initialize_empty();
		grantley_memmap_save();
		romstage_handoff_init(false);

		post_code(0x37);
		printk(BIOS_INFO, "Grantley: tearing down CAR and transferring to ramstage...\n");
		prepare_and_run_postcar();
	}

	/* Do not invoke the client MRC, train DRAM, publish CBMEM or leave CAR. */
	post_code(0xe3);
	die("Grantley: stopping before DRAM training; native controller init not implemented.\n");
}
