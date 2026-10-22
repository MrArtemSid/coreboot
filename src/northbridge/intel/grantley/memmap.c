/* SPDX-License-Identifier: GPL-2.0-only */

#define __SIMPLE_DEVICE__

#include <arch/romstage.h>
#include <cbmem.h>
#include <commonlib/helpers.h>
#include <cpu/x86/mtrr.h>
#include <cpu/x86/smm.h>
#include <device/pci_ops.h>
#include <stdint.h>
#include <types.h>

/*
 * Default Top of Low Usable Memory (TOLUM) fallback for Haswell-EP.
 * Under 4 GiB, MMIO space (PCIe, MMCFG, APIC) is typically mapped at 0x80000000..0xffffffff.
 * Usable low RAM sits below TOLUM (or 0x80000000 / 2 GiB minimum).
 */
#define GRANTLEY_DEFAULT_TOLUM  0x80000000UL

static uintptr_t top_of_low_usable_memory(void)
{
	const pci_devfn_t iio_dev = PCI_DEV(0, 5, 0);
	uint32_t tolm =
		pci_s_read_config32(iio_dev, 0xd0) & 0xfc000000;
	uintptr_t low_ram_top;

	if (tolm >= 0x40000000 && tolm <= 0xd0000000)
		low_ram_top = tolm;
	else
		low_ram_top = GRANTLEY_DEFAULT_TOLUM;

	if (CONFIG(SMM_TSEG) && CONFIG_SMM_TSEG_SIZE > 0)
		low_ram_top -= CONFIG_SMM_TSEG_SIZE;

	return ALIGN_DOWN(low_ram_top, 8 * MiB);
}

uintptr_t cbmem_top_chipset(void)
{
	return top_of_low_usable_memory();
}

void smm_region(uintptr_t *start, size_t *size)
{
	*size = CONFIG_SMM_TSEG_SIZE;
	*start = cbmem_top_chipset();
}

void fill_postcar_frame(struct postcar_frame *pcf)
{
	/* Cache at least 8 MiB below top of RAM for CBMEM and ramstage execution */
	const uintptr_t top_of_ram = ALIGN_DOWN(cbmem_top(), 8 * MiB);

	postcar_frame_add_mtrr(pcf, top_of_ram - 8 * MiB, 16 * MiB, MTRR_TYPE_WRBACK);
}
