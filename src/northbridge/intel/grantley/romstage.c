/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/romstage.h>
#include <arch/symbols.h>
#include <cbmem.h>
#include <cbfs.h>
#include <console/console.h>
#include <cpu/cpu.h>
#include <cpu/x86/msr.h>
#include <cpu/intel/grantley/grantley.h>
#include <cpu/intel/grantley/grantley_routing.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <northbridge/intel/grantley/uncore_runner/pei_services.h>
#include <post.h>
#include <romstage_common.h>
#include <romstage_handoff.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <static.h>
#include "chip.h"
#include "iio.h"
#include "platform.h"
#include "memmap.h"

bool __weak mainboard_get_board_identity(struct grantley_board_identity *identity)
{
	return false;
}

/* Static CAR storage for PEI runner services and HOB list */
static struct grantley_uncore_state uncore_state;
static uint8_t pei_pool[64 * 1024] __aligned(16);
static uint8_t pei_hob[32 * 1024] __aligned(16);

void mainboard_romstage_entry(void)
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
	       "Haswell-EP" : "Broadwell-EP");

	if (!grantley_pch_is_supported(pch & 0xffff, pch >> 16))
		printk(BIOS_WARNING, "Grantley: unrecognized PCH %04x:%04x, "
		       "continuing with unvalidated early setup\n",
		       pch & 0xffff, pch >> 16);

	/* Load the external UncoreInitPeim memory initializer. */
	if (CONFIG(HAVE_INTEL_UNCORE_BIN)) {
		/* CAR statics must stay below the reserved module stack. */
		const uintptr_t stack_top = CONFIG_DCACHE_RAM_BASE +
			CONFIG_DCACHE_RAM_SIZE - GRANTLEY_PEIM_STACK_HEADROOM;
		if (CONFIG_DCACHE_RAM_SIZE <= GRANTLEY_PEIM_STACK_SIZE +
		    GRANTLEY_PEIM_STACK_HEADROOM ||
		    (uintptr_t)_car_unallocated_start > stack_top - GRANTLEY_PEIM_STACK_SIZE)
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

		size_t setup_size = 0;
		const void *setup = cbfs_ro_map("intelsetup.bin", &setup_size);
		grantley_pei_set_intelsetup(&uncore_state, setup, setup_size);
		if (setup)
			printk(BIOS_INFO, "Grantley: IntelSetup %zu bytes from CBFS\n",
			       setup_size);

		struct grantley_board_identity identity = { 0 };
		if (mainboard_get_board_identity(&identity)) {
			status = grantley_pei_set_board_identity(&uncore_state,
				identity.board_id, identity.policy_revision);
			if (status != EFI_SUCCESS)
				die("Grantley: PlatformInfo board identity failure\n");
		}

		/*
		 * Two values the module reads before it can say anything itself.
		 * CPUBUSNO names the buses this socket owns and the module dead-loops
		 * when the register is invalid; PLATFORM_INFO[15:8] is the ratio every
		 * one of its delays is computed from, so a zero there turns each of
		 * them into a spin that never ends. Neither is printed by the release
		 * build, and both are cheap to rule out here.
		 */
		const msr_t cpubusno = rdmsr(0x300);		/* CPUBUSNO */
		const msr_t platform_info = rdmsr(0xce);	/* PLATFORM_INFO */
		printk(BIOS_INFO, "Grantley: CPUBUSNO %08x:%08x ratio %u\n",
		       cpubusno.hi, cpubusno.lo,
		       (unsigned int)((platform_info.lo >> 8) & 0xff));

		post_code(0x35);
		printk(BIOS_INFO, "Grantley: launching UncoreInitPeim memory training...\n");

		status = grantley_run_uncore_peim(uncore_bin, uncore_size,
						  &uncore_state, stack_top);
		if (status != EFI_SUCCESS) {
			printk(BIOS_ERR, "Grantley: uncore PEIM failed: 0x%lx\n",
			       (unsigned long)status);
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
		/* The SMRAM HOB omits the IED; take the full TSEG from the IIO. */
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

		/* Route C0000h-FFFFFh to DRAM for the payload; keep PAM unlocked. */
		const pci_devfn_t sad = PCI_DEV(grantley_uncore_bus(), 15, 4);
		pci_s_write_config32(sad, 0x40, 0x33333330);
		pci_s_write_config32(sad, 0x44, 0x00333333);

		post_code(0x36);
		printk(BIOS_INFO, "Grantley: validated %llu memory ranges, SMRAM %llx+%llx\n",
		       (unsigned long long)memory_map.count,
		       (unsigned long long)memory_map.smram_base,
		       (unsigned long long)memory_map.smram_size);

		/* Persist the validated physical reservations before leaving CAR. */
		cbmem_initialize_empty();
		if (CONFIG(CACHE_MRC_SETTINGS))
			grantley_training_cache_save(&uncore_state);
		grantley_memmap_save();
		grantley_save_dimm_info(&uncore_state);
		romstage_handoff_init(false);

		post_code(0x37);
		printk(BIOS_INFO, "Grantley: memory initialization complete\n");

		/* Let the links train while the later stages load. */
		grantley_iio_init(config_of_soc());
		return;
	}

	/* Do not invoke the client MRC, train DRAM, publish CBMEM or leave CAR. */
	post_code(0xe3);
	die("Grantley: stopping before DRAM training; native controller init not implemented.\n");
}
