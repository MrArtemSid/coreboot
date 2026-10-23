/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <cf9_reset.h>
#include <device/mmio.h>
#include <drivers/efi/efivars.h>
#include <post.h>
/* Forward declaration of I/O and PCI operations */
#if !defined(__TESTS__)
#include <arch/io.h>
#include <cpu/cpu.h>
#include <device/pci_ops.h>
#else
/* Test stubs or mock declarations for unit testing */
static inline uint8_t inb(uint16_t port) { return 0xff; }
static inline uint16_t inw(uint16_t port) { return 0xffff; }
static inline uint32_t inl(uint16_t port) { return 0xffffffff; }
static inline void outb(uint8_t val, uint16_t port) { }
static inline void outw(uint16_t val, uint16_t port) { }
static inline void outl(uint32_t val, uint16_t port) { }
#endif
#undef NULL
#include "pei_services.h"
#include "../memmap.h"

static struct grantley_uncore_state *g_state;

struct grantley_uncore_state *grantley_get_uncore_state(void)
{
	return g_state;
}

static EFI_STATUS EFIAPI grantley_install_ppi(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_PPI_DESCRIPTOR * ppi_list)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !ppi_list)
		return EFI_INVALID_PARAMETER;

	const EFI_PEI_PPI_DESCRIPTOR *desc = ppi_list;

	while (desc) {
		if (state->ppi_count >= MAX_GRANTLEY_PPI_DESCRIPTORS)
			return EFI_OUT_OF_RESOURCES;

		state->ppi_descriptors[state->ppi_count++] = desc;

		if (desc->Flags & EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST)
			break;
		desc++;
	}

	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI grantley_reinstall_ppi(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_PPI_DESCRIPTOR *old_ppi,
	const EFI_PEI_PPI_DESCRIPTOR *new_ppi)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !old_ppi || !new_ppi)
		return EFI_INVALID_PARAMETER;

	for (UINTN i = 0; i < state->ppi_count; i++) {
		if (state->ppi_descriptors[i] == old_ppi) {
			state->ppi_descriptors[i] = new_ppi;
			return EFI_SUCCESS;
		}
	}

	return EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI grantley_locate_ppi(
	const struct _EFI_PEI_SERVICES **services,
	const void *guid,
	UINTN instance,
	EFI_PEI_PPI_DESCRIPTOR **ppi_descriptor,
	void **ppi)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !guid || !ppi)
		return EFI_INVALID_PARAMETER;

	UINTN match_count = 0;

	for (UINTN i = 0; i < state->ppi_count; i++) {
		const EFI_PEI_PPI_DESCRIPTOR *desc = state->ppi_descriptors[i];

		if (!desc || !desc->Guid)
			continue;

		if (memcmp(desc->Guid, guid, sizeof(EFI_GUID)) == 0) {
			if (match_count == instance) {
				if (ppi_descriptor)
					*ppi_descriptor = (EFI_PEI_PPI_DESCRIPTOR *)desc;
				*ppi = desc->Ppi;
				return EFI_SUCCESS;
			}
			match_count++;
		}
	}

	return EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI grantley_notify_ppi(
	const struct _EFI_PEI_SERVICES **services,
	const struct _EFI_PEI_NOTIFY_DESCRIPTOR *notify_list)
{
	/* Notifications are recorded or ignored during pre-RAM */
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI grantley_get_boot_mode(
	const struct _EFI_PEI_SERVICES **services,
	EFI_BOOT_MODE * mode)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !mode)
		return EFI_INVALID_PARAMETER;

	*mode = state->boot_mode;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI grantley_set_boot_mode(
	const struct _EFI_PEI_SERVICES **services,
	EFI_BOOT_MODE boot_mode)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state)
		return EFI_INVALID_PARAMETER;

	state->boot_mode = boot_mode;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI grantley_get_hob_list(
	const struct _EFI_PEI_SERVICES **services,
	void **hob_list)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !hob_list)
		return EFI_INVALID_PARAMETER;

	*hob_list = state->hob_list;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI grantley_create_hob(
	const struct _EFI_PEI_SERVICES **services,
	UINT16 type,
	UINT16 length,
	void **hob)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !hob || length < sizeof(EFI_HOB_GENERIC_HEADER) || length > 65528)
		return EFI_INVALID_PARAMETER;

	/* Align length to 8 bytes without wrapping the 16-bit HOB length. */
	UINT16 aligned_length = (length + 7) & ~7;
	if (state->hob_allocated > state->hob_size)
		return EFI_OUT_OF_RESOURCES;
	UINTN remaining = state->hob_size - state->hob_allocated;

	if (remaining < (UINTN)(aligned_length + sizeof(EFI_HOB_GENERIC_HEADER)))
		return EFI_OUT_OF_RESOURCES;

	/* Overwrite the previous END_OF_HOB_LIST marker with the new HOB */
	EFI_HOB_GENERIC_HEADER *new_hob =
		(EFI_HOB_GENERIC_HEADER *)((UINT8 *)state->hob_list + state->hob_allocated);
	new_hob->HobType = type;
	new_hob->HobLength = aligned_length;
	new_hob->Reserved = 0;

	state->hob_allocated += aligned_length;

	/* Place a new END_OF_HOB_LIST marker */
	EFI_HOB_GENERIC_HEADER *end_hob =
		(EFI_HOB_GENERIC_HEADER *)((UINT8 *)state->hob_list + state->hob_allocated);
	end_hob->HobType = EFI_HOB_TYPE_END_OF_HOB_LIST;
	end_hob->HobLength = sizeof(EFI_HOB_GENERIC_HEADER);
	end_hob->Reserved = 0;

	EFI_HOB_HANDOFF_INFO_TABLE *hit = state->hob_list;
	hit->EfiEndOfHobList = (EFI_PHYSICAL_ADDRESS)(uintptr_t)end_hob;
	hit->EfiFreeMemoryBottom = hit->EfiEndOfHobList + sizeof(*end_hob);
	*hob = new_hob;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI grantley_install_pei_memory(
	const struct _EFI_PEI_SERVICES **services,
	EFI_PHYSICAL_ADDRESS memory_begin,
	UINT64 memory_length)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state)
		return EFI_INVALID_PARAMETER;

	state->dram_base = memory_begin;
	state->dram_size = memory_length;
	state->dram_initialized = TRUE;

	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI grantley_allocate_pages(
	const struct _EFI_PEI_SERVICES **services,
	EFI_MEMORY_TYPE memory_type,
	UINTN pages,
	EFI_PHYSICAL_ADDRESS * addr)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !addr)
		return EFI_INVALID_PARAMETER;

	if (!pages)
		return EFI_INVALID_PARAMETER;
	if (state->pool_allocated > state->pool_size || pages > (UINTN)-1 / 4096)
		return EFI_OUT_OF_RESOURCES;
	uintptr_t current = (uintptr_t)state->pool_base + state->pool_allocated;
	UINTN padding = -current & 4095;
	UINTN remaining = state->pool_size - state->pool_allocated;
	UINTN bytes = pages * 4096;
	if (padding > remaining || bytes > remaining - padding)
		return EFI_OUT_OF_RESOURCES;

	*addr = current + padding;
	state->pool_allocated += padding + bytes;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI grantley_allocate_pool(
	const struct _EFI_PEI_SERVICES **services,
	UINTN size,
	void **buffer)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !buffer)
		return EFI_INVALID_PARAMETER;

	if (size > (UINTN)-1 - 7 || state->pool_allocated > state->pool_size)
		return EFI_OUT_OF_RESOURCES;
	UINTN aligned_size = (size + 7) & ~7;

	if (aligned_size > state->pool_size - state->pool_allocated)
		return EFI_OUT_OF_RESOURCES;

	*buffer = state->pool_base + state->pool_allocated;
	state->pool_allocated += aligned_size;
	return EFI_SUCCESS;
}

static VOID EFIAPI grantley_copy_mem(
	void *destination,
	void *source,
	UINTN length)
{
	if (destination && source && length)
		memcpy(destination, source, length);
}

static VOID EFIAPI grantley_set_mem(
	void *buffer,
	UINTN size,
	UINT8 value)
{
	if (buffer && size)
		memset(buffer, value, size);
}

static EFI_STATUS EFIAPI grantley_report_status_code(
	const struct _EFI_PEI_SERVICES **services,
	EFI_STATUS_CODE_TYPE type,
	EFI_STATUS_CODE_VALUE value,
	UINT32 instance,
	const void *caller_id,
	const void *data)
{
	post_code((uint8_t)value);
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI grantley_reset_system(
	const struct _EFI_PEI_SERVICES **services)
{
	system_reset();
	return EFI_SUCCESS;
}

#if ENV_TEST
uint32_t grantley_test_route_read32(uint32_t dev, uint16_t reg, uintptr_t base);
void grantley_test_route_write32(uint32_t dev, uint16_t reg, uintptr_t base, uint32_t value);
uint32_t grantley_test_route_cpuid(void);
#define route_read32 grantley_test_route_read32
#define route_write32 grantley_test_route_write32
#define route_cpuid grantley_test_route_cpuid
#else
static uint32_t route_read32(pci_devfn_t dev, uint16_t reg, uintptr_t base)
{
	if (base)
		return read32((void *)(base + dev + reg));
	return pci_s_read_config32(dev, reg);
}

static void route_write32(pci_devfn_t dev, uint16_t reg, uintptr_t base, uint32_t value)
{
	if (base)
		write32((void *)(base + dev + reg), value);
	else
		pci_s_write_config32(dev, reg, value);
}

static uint32_t route_cpuid(void)
{
	return cpuid_eax(1);
}
#endif

EFI_STATUS grantley_pei_set_mmcfg(struct grantley_uncore_state *state, uintptr_t base)
{
	if (!state)
		return EFI_INVALID_PARAMETER;
	state->mmcfg_enabled = FALSE;
	state->mmcfg_base = 0;
	/* One complete 256-bus ECAM segment, below the IA-32 address ceiling. */
	if (!base || (base & 0xfffffff) || base > 0xf0000000ULL)
		return EFI_INVALID_PARAMETER;

	const pci_devfn_t reset_ubox = PCI_DEV(1, 16, 7);
	const pci_devfn_t ubox = PCI_DEV(0xff, 16, 7);
	const pci_devfn_t sad = PCI_DEV(0xff, 15, 5);
	const pci_devfn_t iio = PCI_DEV(0, 5, 0);
	const pci_devfn_t pcu = PCI_DEV(0xff, 30, 2);
	uint16_t base_reg = (route_cpuid() & 0xfffff) == 0x306f0 ? 0x84 : 0x90;
	uint16_t limit_reg = base_reg == 0x84 ? 0x88 : 0x98;

	/* Early assembly may already have moved UBOX off its reset bus.  Adopt
	 * the live route without touching CPU identity, master or scratch state.
	 * A conflicting live route is not a safe cold-reset fallback.
	 */
	if ((route_read32(ubox, 0, 0) & 0xffff) == 0x8086) {
		if ((route_read32(ubox, 0xd0, 0) & 0x8000ffff) != 0x8000ff00 ||
		    (route_read32(sad, 0, 0) & 0xffff) != 0x8086 ||
		    (route_read32(iio, 0, 0) & 0xffff) != 0x8086 ||
		    (route_read32(sad, 0xe4, 0) & 0x00ffffff) != 0 ||
		    (route_read32(sad, 0xc0, 0) & 0xfc000007) != (base | 1) ||
		    (route_read32(ubox, 0xdc, 0) & 7) != 1 ||
		    (route_read32(iio, base_reg, 0) & 0xfc000000) != base ||
		    (route_read32(iio, limit_reg, 0) & 0xfc000000) !=
			((base + 0xfffffff) & 0xfc000000))
			return EFI_DEVICE_ERROR;
		/* Production IIO exposes 64-bit base/limit pairs; A0 is 32-bit. */
		if (base_reg != 0x84 && (route_read32(iio, base_reg + 4, 0) != 0 ||
				       route_read32(iio, limit_reg + 4, 0) != 0))
			return EFI_DEVICE_ERROR;
		if ((route_read32(pcu, 0, base) & 0xffff) != 0x8086 ||
		    (route_read32(pcu, 0x40, base) & 0x8000ffff) != 0x8000ff00 ||
		    (route_read32(iio, 0x108, base) & 0x1ffff) != 0x1ff00)
			return EFI_DEVICE_ERROR;
		state->mmcfg_base = base;
		state->mmcfg_enabled = TRUE;
		return EFI_SUCCESS;
	}
	if ((route_read32(reset_ubox, 0, 0) & 0xffff) != 0x8086)
		return EFI_DEVICE_ERROR;

	/* CPUBUSNO changes the UBOX and SAD addresses immediately. */
	route_write32(reset_ubox, 0xd0, 0, 0x8000ff00);
	if ((route_read32(ubox, 0, 0) & 0xffff) != 0x8086 ||
	    (route_read32(sad, 0, 0) & 0xffff) != 0x8086 ||
	    (route_read32(iio, 0, 0) & 0xffff) != 0x8086)
		return EFI_DEVICE_ERROR;

	/* Single-socket target list: every ECAM bus targets socket zero. */
	route_write32(sad, 0xe4, 0, 0);
	route_write32(sad, 0xc0, 0, base | 1);
	uint32_t rule = route_read32(ubox, 0xdc, 0);
	route_write32(ubox, 0xdc, 0, (rule & ~7U) | 1U);
	if ((route_read32(sad, 0xc0, 0) & 0xfc000007) != (base | 1) ||
	    (route_read32(ubox, 0xdc, 0) & 7) != 1)
		return EFI_DEVICE_ERROR;

	/* Limit stores address bits 31:26, not a byte count or an enable flag. */
	route_write32(iio, base_reg, 0, base);
	route_write32(iio, limit_reg, 0, (base + 0xfffffff) & 0xfc000000);
	if ((route_read32(iio, base_reg, 0) & 0xfc000000) != base ||
	    (route_read32(iio, limit_reg, 0) & 0xfc000000) !=
	    ((base + 0xfffffff) & 0xfc000000))
		return EFI_DEVICE_ERROR;

	if ((route_read32(pcu, 0, base) & 0xffff) != 0x8086)
		return EFI_DEVICE_ERROR;
	uint32_t buses = route_read32(pcu, 0x40, base);
	route_write32(pcu, 0x40, base, (buses & 0xffff0000) | 0xff00);
	buses = route_read32(iio, 0x108, base);
	route_write32(iio, 0x108, base, (buses & 0xfffe0000) | 0x1ff00);
	buses = route_read32(pcu, 0x40, base);
	route_write32(pcu, 0x40, base, buses | 0x80000000);
	if ((route_read32(pcu, 0x40, base) & 0x8000ffff) != 0x8000ff00 ||
	    (route_read32(iio, 0x108, base) & 0x1ffff) != 0x1ff00)
		return EFI_DEVICE_ERROR;

	state->mmcfg_base = base;
	state->mmcfg_enabled = TRUE;
	return EFI_SUCCESS;
}

static inline void pci_cfg_decode(UINT64 address, pci_devfn_t *pdev,
				  UINT32 *reg, UINT8 *bus, UINT8 *dev,
				  UINT8 *func)
{
	*bus = (address >> 24) & 0xff;
	*dev = (address >> 16) & 0x1f;
	*func = (address >> 8) & 0x07;
	*reg = (address >> 32) != 0 ? (UINT32)(address >> 32) :
				       (UINT32)(address & 0xff);
	*pdev = PCI_DEV(*bus, *dev, *func);
}

static inline uintptr_t pci_cfg_mmio(uintptr_t base, UINT8 bus, UINT8 dev,
				     UINT8 func, UINT32 reg)
{
	return base + (((uintptr_t)bus) << 20) + (((uintptr_t)dev) << 15) +
	       (((uintptr_t)func) << 12) + reg;
}

static EFI_STATUS EFIAPI grantley_pci_cfg_read(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_PCI_CFG2_PPI * this,
	EFI_PEI_PCI_CFG_PPI_WIDTH width,
	UINT64 address,
	void *buffer)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();
	pci_devfn_t pdev;
	UINT32 reg;
	UINT8 bus, dev, func;

	if (!state || !buffer)
		return EFI_INVALID_PARAMETER;

	pci_cfg_decode(address, &pdev, &reg, &bus, &dev, &func);

	if (state->mmcfg_enabled) {
		void *mmio = (void *)pci_cfg_mmio(state->mmcfg_base, bus, dev,
						  func, reg);
		switch (width) {
		case EfiPeiPciCfgWidthUint8:
			*(UINT8 *)buffer = read8(mmio);
			return EFI_SUCCESS;
		case EfiPeiPciCfgWidthUint16:
			*(UINT16 *)buffer = read16(mmio);
			return EFI_SUCCESS;
		case EfiPeiPciCfgWidthUint32:
			*(UINT32 *)buffer = read32(mmio);
			return EFI_SUCCESS;
		default:
			return EFI_INVALID_PARAMETER;
		}
	}

	if (reg >= 256)
		return EFI_UNSUPPORTED;

	switch (width) {
	case EfiPeiPciCfgWidthUint8:
		*(UINT8 *)buffer = pci_s_read_config8(pdev, reg);
		return EFI_SUCCESS;
	case EfiPeiPciCfgWidthUint16:
		*(UINT16 *)buffer = pci_s_read_config16(pdev, reg);
		return EFI_SUCCESS;
	case EfiPeiPciCfgWidthUint32:
		*(UINT32 *)buffer = pci_s_read_config32(pdev, reg);
		return EFI_SUCCESS;
	default:
		return EFI_INVALID_PARAMETER;
	}
}

static EFI_STATUS EFIAPI grantley_pci_cfg_write(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_PCI_CFG2_PPI * this,
	EFI_PEI_PCI_CFG_PPI_WIDTH width,
	UINT64 address,
	void *buffer)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();
	pci_devfn_t pdev;
	UINT32 reg;
	UINT8 bus, dev, func;

	if (!state || !buffer)
		return EFI_INVALID_PARAMETER;

	pci_cfg_decode(address, &pdev, &reg, &bus, &dev, &func);

	if (state->mmcfg_enabled) {
		void *mmio = (void *)pci_cfg_mmio(state->mmcfg_base, bus, dev,
						  func, reg);
		switch (width) {
		case EfiPeiPciCfgWidthUint8:
			write8(mmio, *(UINT8 *)buffer);
			return EFI_SUCCESS;
		case EfiPeiPciCfgWidthUint16:
			write16(mmio, *(UINT16 *)buffer);
			return EFI_SUCCESS;
		case EfiPeiPciCfgWidthUint32:
			write32(mmio, *(UINT32 *)buffer);
			return EFI_SUCCESS;
		default:
			return EFI_INVALID_PARAMETER;
		}
	}

	if (reg >= 256)
		return EFI_UNSUPPORTED;

	switch (width) {
	case EfiPeiPciCfgWidthUint8:
		pci_s_write_config8(pdev, reg, *(UINT8 *)buffer);
		return EFI_SUCCESS;
	case EfiPeiPciCfgWidthUint16:
		pci_s_write_config16(pdev, reg, *(UINT16 *)buffer);
		return EFI_SUCCESS;
	case EfiPeiPciCfgWidthUint32:
		pci_s_write_config32(pdev, reg, *(UINT32 *)buffer);
		return EFI_SUCCESS;
	default:
		return EFI_INVALID_PARAMETER;
	}
}

static EFI_STATUS EFIAPI grantley_pci_cfg_modify(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_PCI_CFG2_PPI * this,
	EFI_PEI_PCI_CFG_PPI_WIDTH width,
	UINT64 address,
	void *set_bits,
	void *clear_bits)
{
	UINT32 val = 0, set = 0, clr = 0;
	EFI_STATUS status;

	if (width > EfiPeiPciCfgWidthUint32)
		return EFI_INVALID_PARAMETER;

	size_t bytes = 1 << width;
	if (set_bits)
		memcpy(&set, set_bits, bytes);
	if (clear_bits)
		memcpy(&clr, clear_bits, bytes);

	status = grantley_pci_cfg_read(services, this, width, address, &val);
	if (status != EFI_SUCCESS)
		return status;

	val = (val & ~clr) | set;
	return grantley_pci_cfg_write(services, this, width, address, &val);
}

static UINT8 EFIAPI grantley_io_read8(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_CPU_IO_PPI *this,
	UINT64 address)
{
	return inb((uint16_t)address);
}

static UINT16 EFIAPI grantley_io_read16(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_CPU_IO_PPI *this,
	UINT64 address)
{
	return inw((uint16_t)address);
}

static UINT32 EFIAPI grantley_io_read32(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_CPU_IO_PPI *this,
	UINT64 address)
{
	return inl((uint16_t)address);
}

static UINT64 EFIAPI grantley_io_read64(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_CPU_IO_PPI *this,
	UINT64 address)
{
	return (UINT64)inl((uint16_t)address) |
	       ((UINT64)inl((uint16_t)(address + 4)) << 32);
}

static VOID EFIAPI grantley_io_write8(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_CPU_IO_PPI *this,
	UINT64 address,
	UINT8 data)
{
	outb(data, (uint16_t)address);
}

static VOID EFIAPI grantley_io_write16(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_CPU_IO_PPI *this,
	UINT64 address,
	UINT16 data)
{
	outw(data, (uint16_t)address);
}

static VOID EFIAPI grantley_io_write32(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_CPU_IO_PPI *this,
	UINT64 address,
	UINT32 data)
{
	outl(data, (uint16_t)address);
}

static VOID EFIAPI grantley_io_write64(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_CPU_IO_PPI *this,
	UINT64 address,
	UINT64 data)
{
	outl((UINT32)data, (uint16_t)address);
	outl((UINT32)(data >> 32), (uint16_t)(address + 4));
}

static EFI_STATUS EFIAPI grantley_cpu_io_read(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_CPU_IO_PPI *this,
	EFI_PEI_CPU_IO_PPI_WIDTH width,
	UINT64 address,
	UINTN count,
	void *buffer)
{
	if (!buffer || count == 0)
		return EFI_INVALID_PARAMETER;

	UINT8 *buf = (UINT8 *)buffer;

	while (count--) {
		switch (width) {
		case EfiPeiCpuIoWidthUint8:
			*buf = grantley_io_read8(services, this, address);
			buf += 1;
			address += 1;
			break;
		case EfiPeiCpuIoWidthUint16:
			*(UINT16 *)buf = grantley_io_read16(services, this, address);
			buf += 2;
			address += 2;
			break;
		case EfiPeiCpuIoWidthUint32:
			*(UINT32 *)buf = grantley_io_read32(services, this, address);
			buf += 4;
			address += 4;
			break;
		default:
			return EFI_INVALID_PARAMETER;
		}
	}

	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI grantley_cpu_io_write(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_CPU_IO_PPI *this,
	EFI_PEI_CPU_IO_PPI_WIDTH width,
	UINT64 address,
	UINTN count,
	void *buffer)
{
	if (!buffer || count == 0)
		return EFI_INVALID_PARAMETER;

	const UINT8 *buf = (const UINT8 *)buffer;

	while (count--) {
		switch (width) {
		case EfiPeiCpuIoWidthUint8:
			grantley_io_write8(services, this, address, *buf);
			buf += 1;
			address += 1;
			break;
		case EfiPeiCpuIoWidthUint16:
			grantley_io_write16(services, this, address, *(const UINT16 *)buf);
			buf += 2;
			address += 2;
			break;
		case EfiPeiCpuIoWidthUint32:
			grantley_io_write32(services, this, address, *(const UINT32 *)buf);
			buf += 4;
			address += 4;
			break;
		default:
			return EFI_INVALID_PARAMETER;
		}
	}

	return EFI_SUCCESS;
}

void grantley_pei_set_var_store(struct grantley_uncore_state *state,
				const struct region_device *rdev)
{
	if (!state)
		return;

	state->var_store_rdev = rdev;
}

EFI_STATUS grantley_pei_set_board_identity(struct grantley_uncore_state *state,
	uint8_t board_id, uint32_t policy_revision)
{
	if (!state || !state->platform_info_ppi.Ppi)
		return EFI_INVALID_PARAMETER;

	uint8_t *data = state->platform_info_ppi.Ppi;
	data[0x46] = board_id;
	/* TypeRevisionId is an unaligned DWORD in the PlatformInfo payload. */
	memcpy(data + 0x1e, &policy_revision, sizeof(policy_revision));
	return EFI_SUCCESS;
}

void grantley_pei_set_mem_params(struct grantley_uncore_state *state,
				 uint16_t max_dram_speed_mts,
				 uint8_t tcl, uint8_t trcd, uint8_t trp,
				 uint8_t tras, uint8_t command_rate, bool ecc_en)
{
	if (!state)
		return;

	state->max_dram_speed_mts = max_dram_speed_mts;
	state->tcl = tcl;
	state->trcd = trcd;
	state->trp = trp;
	state->tras = tras;
	state->command_rate = command_rate;
	state->ecc_en = ecc_en;
}

static EFI_STATUS EFIAPI grantley_get_variable(
	const EFI_PEI_READ_ONLY_VARIABLE2_PPI *this,
	const CHAR16 *variable_name,
	const EFI_GUID *vendor_guid,
	UINT32 *attributes,
	UINTN *data_size,
	void *data)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !this || !variable_name || !vendor_guid || !data_size)
		return EFI_INVALID_PARAMETER;

	/* Convert CHAR16 UTF-16LE variable name to ASCII for coreboot efi_fv_get_option */
	const CHAR16 *var_name16 = (const CHAR16 *)variable_name;
	char ascii_name[64];
	size_t i = 0;

	while (var_name16[i] != 0 && i < sizeof(ascii_name) - 1) {
		ascii_name[i] = (char)(var_name16[i] & 0x7f);
		i++;
	}
	ascii_name[i] = '\0';

	enum cb_err err = CB_ERR;
	uint32_t size = (uint32_t)*data_size;
	if (state->var_store_rdev)
		err = efi_fv_get_option(state->var_store_rdev,
					vendor_guid, ascii_name,
					data, &size);

	/* If NVRAM store doesn't have IntelSetup, check if mainboard supplied manual overrides */
	if (err != CB_SUCCESS && strcmp(ascii_name, "IntelSetup") == 0) {
		bool has_override = (state->max_dram_speed_mts != 0 ||
				     state->tcl != 0 || state->trcd != 0 ||
				     state->trp != 0 || state->tras != 0 ||
				     state->command_rate != 0 || state->ecc_en);

		if (has_override) {
			if (*data_size < 0x200) {
				*data_size = 0x200;
				return EFI_BUFFER_TOO_SMALL;
			}
			memset(data, 0, *data_size);

			uint8_t *setup = (uint8_t *)data;

			/* Map max_dram_speed_mts to Intel Grantley DdrFreqLimit (offset 0x6e) */
			if (state->max_dram_speed_mts >= 2400)
				setup[0x6e] = 0xd; /* DDR_2400 */
			else if (state->max_dram_speed_mts >= 2133)
				setup[0x6e] = 0xb; /* DDR_2133 */
			else if (state->max_dram_speed_mts >= 1866)
				setup[0x6e] = 0x9; /* DDR_1866 */
			else if (state->max_dram_speed_mts >= 1600)
				setup[0x6e] = 0x7; /* DDR_1600 */
			else if (state->max_dram_speed_mts >= 1333)
				setup[0x6e] = 0x5; /* DDR_1333 */

			/* Timings (offsets in Intel Grantley SystemConfiguration structure) */
			setup[0x70] = state->tcl;
			setup[0x71] = state->trcd;
			setup[0x72] = state->trp;
			setup[0x73] = state->tras;
			setup[0x74] = state->command_rate;
			if (state->ecc_en)
				setup[0x75] = 1;

			*data_size = 0x200;
			if (attributes)
				*attributes = 0;
			return EFI_SUCCESS;
		}
	}

	if (err == CB_ERR_ARG)
		return EFI_BUFFER_TOO_SMALL;
	if (err != CB_SUCCESS)
		return EFI_NOT_FOUND;

	*data_size = (UINTN)size;
	if (attributes)
		*attributes = 0;

	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI grantley_get_next_variable_name(
	const EFI_PEI_READ_ONLY_VARIABLE2_PPI *this,
	UINTN *variable_name_size,
	CHAR16 *variable_name,
	EFI_GUID *vendor_guid)
{
	return EFI_NOT_FOUND;
}

/* IA-32 implicit PEI services pointer: the dword before IDTR.base is PeiServices **. */
#if !ENV_TEST
struct grantley_peim_idtr {
	uint16_t limit;
	uint32_t base;
} __packed;

/* Static CAR storage must outlive the call and remain outside the PEIM stack. */
static struct {
	uint32_t reserved;
	const struct _EFI_PEI_SERVICES **services;
	uint64_t gates[256];
} peim_idt __aligned(8);

_Static_assert(sizeof(struct grantley_peim_idtr) == 6, "IA-32 IDTR size");
_Static_assert(offsetof(__typeof__(peim_idt), gates) -
	       offsetof(__typeof__(peim_idt), services) == 4,
	       "PEI services double pointer immediately precedes IDT");
#endif

typedef EFI_STATUS (*grantley_peim_entry_t)(
	void *file_handle,
	const struct _EFI_PEI_SERVICES **services);

EFI_STATUS grantley_run_uncore_peim(const void *pe_base, struct grantley_uncore_state *state)
{
	if (!pe_base || !state)
		return EFI_INVALID_PARAMETER;

	const uint8_t *bytes = (const uint8_t *)pe_base;

	/* Verify DOS header signature 'MZ' */
	if (bytes[0] != 'M' || bytes[1] != 'Z')
		return EFI_UNSUPPORTED;

	uint32_t e_lfanew = *(const uint32_t *)&bytes[0x3c];
	const uint8_t *pe_hdr = bytes + e_lfanew;

	/* Verify PE signature 'PE\0\0' */
	if (pe_hdr[0] != 'P' || pe_hdr[1] != 'E' || pe_hdr[2] != 0 || pe_hdr[3] != 0)
		return EFI_UNSUPPORTED;

	/* In IA-32 PE binaries (Optional Header 32-bit):
	 * Offset 0x28 is AddressOfEntryPoint (RVA).
	 * In flat/CAR mapping, entry_addr = pe_base + entry_rva.
	 */
	uint32_t entry_rva = *(const uint32_t *)&pe_hdr[0x28];
	uint32_t image_base = *(const uint32_t *)&pe_hdr[0x34];
	grantley_peim_entry_t entry_point =
		(grantley_peim_entry_t)(uintptr_t)(bytes + (intptr_t)(int32_t)entry_rva);

	if ((uintptr_t)bytes != (uintptr_t)image_base)
		return EFI_UNSUPPORTED;

	/*
	 * UncoreInitPeim allocates ~440 KB on the stack at entry
	 * (e.g. sub $0x6db38, %esp). Switch to a dedicated stack at
	 * top of CAR (0xfe1fffc0), providing >800 KB of headroom,
	 * then restore coreboot's esp upon return.
	 */
	uintptr_t car_stack_top =
		CONFIG_DCACHE_RAM_BASE + CONFIG_DCACHE_RAM_SIZE - 64;
	EFI_STATUS status;
	const void *services_arg = &state->services_ptr;

#if ENV_TEST
	/* Host tests cannot execute an IA-32 PEIM or switch to firmware CAR. */
	(void)car_stack_top;
	(void)services_arg;
	(void)entry_point;
	status = EFI_UNSUPPORTED;
#else
	struct grantley_peim_idtr saved_idtr;
	struct grantley_peim_idtr peim_idtr = {
		.limit = sizeof(peim_idt.gates) - 1,
		.base = (uintptr_t)peim_idt.gates,
	};

	__asm__ __volatile__("sidt %0" : "=m"(saved_idtr) : : "memory");
	memset(peim_idt.gates, 0, sizeof(peim_idt.gates));
	/* Copy, never modify, coreboot's gates (when an IDT was installed). */
	if (saved_idtr.base) {
		if (saved_idtr.limit < peim_idtr.limit)
			peim_idtr.limit = saved_idtr.limit;
		memcpy(peim_idt.gates, (const void *)(uintptr_t)saved_idtr.base,
		       (size_t)peim_idtr.limit + 1);
	}
	peim_idt.services = &state->services_ptr;
	__asm__ __volatile__("lidt %0" : : "m"(peim_idtr) : "memory");
	__asm__ __volatile__(
		"movl %%esp, %[saved_esp]\n	"
		"movl %[stack], %%esp\n	"
		"subl $8, %%esp\n	"
		"pushl %[services]\n	"
		"pushl $0\n	"
		"call *%[entry]\n	"
		"movl %[saved_esp], %%esp\n	"
		: [status] "=&a"(status), [saved_esp] "=m"(state->saved_esp)
		: [stack] "r"(car_stack_top),
		  [services] "r"(services_arg),
		  [entry] "r"(entry_point)
		: "ecx", "edx", "memory", "cc"
	);
	/* Restore even when the PEIM returns an EFI error. */
	__asm__ __volatile__("lidt %0" : : "m"(saved_idtr) : "memory");
#endif

	return status;
}

static EFI_STATUS EFIAPI grantley_ffs_stub(void)
{
	return EFI_NOT_FOUND;
}

static EFI_GUID g_pci_cfg_guid = {
	0x57a449a, 0x1fdc, 0x4c06,
	{ 0xbf, 0xc9, 0xf5, 0x3f, 0x6a, 0x99, 0xbb, 0x92 }
};
static EFI_GUID g_cpu_io_guid = EFI_PEI_CPU_IO_PPI_INSTALLED_GUID;
static EFI_GUID g_read_only_var_guid = {
	0x2ab86ef5, 0xecb5, 0x4134,
	{ 0xb5, 0x56, 0x38, 0x54, 0xca, 0x1f, 0xe1, 0xb4 }
};
static EFI_GUID g_mem_test_guid = {
	0xb6ec423c, 0x21d2, 0x490d,
	{ 0x85, 0xc6, 0xdd, 0x58, 0x64, 0xea, 0xa6, 0x74 }
};
static EFI_GUID g_mem_size_guid = {
	0x9a7ef41e, 0xc140, 0x4bd1,
	{ 0xb8, 0x84, 0x1e, 0x11, 0x24, 0x0b, 0x4c, 0xe6 }
};
static EFI_GUID g_platform_info_guid = {
	0x1e2acc41, 0xe26a, 0x483d,
	{ 0xaf, 0xc7, 0xa0, 0x56, 0xc3, 0x4e, 0x08, 0x7b }
};

static const EFI_PEI_PCI_CFG2_PPI g_pci_cfg = {
	.Read   = (EFI_PEI_PCI_CFG2_PPI_IO)grantley_pci_cfg_read,
	.Write  = (EFI_PEI_PCI_CFG2_PPI_IO)grantley_pci_cfg_write,
	.Modify = (EFI_PEI_PCI_CFG2_PPI_RW)grantley_pci_cfg_modify,
	.Segment = 0,
};

static const EFI_PEI_CPU_IO_PPI g_cpu_io = {
	.Io = {
		.Read  = (EFI_PEI_CPU_IO_PPI_IO_MEM)grantley_cpu_io_read,
		.Write = (EFI_PEI_CPU_IO_PPI_IO_MEM)grantley_cpu_io_write,
	},
	.IoRead8   = (EFI_PEI_CPU_IO_PPI_IO_READ8)grantley_io_read8,
	.IoRead16  = (EFI_PEI_CPU_IO_PPI_IO_READ16)grantley_io_read16,
	.IoRead32  = (EFI_PEI_CPU_IO_PPI_IO_READ32)grantley_io_read32,
	.IoRead64  = (EFI_PEI_CPU_IO_PPI_IO_READ64)grantley_io_read64,
	.IoWrite8  = (EFI_PEI_CPU_IO_PPI_IO_WRITE8)grantley_io_write8,
	.IoWrite16 = (EFI_PEI_CPU_IO_PPI_IO_WRITE16)grantley_io_write16,
	.IoWrite32 = (EFI_PEI_CPU_IO_PPI_IO_WRITE32)grantley_io_write32,
	.IoWrite64 = (EFI_PEI_CPU_IO_PPI_IO_WRITE64)grantley_io_write64,
};

static const EFI_PEI_READ_ONLY_VARIABLE2_PPI g_read_only_var = {
	.GetVariable      = grantley_get_variable,
	.NextVariableName = grantley_get_next_variable_name,
};

/* The PEIM requests a permanent PEI workspace size, not installed RAM size. */
static EFI_STATUS EFIAPI grantley_platform_memory_size(
	const EFI_PEI_SERVICES **services, void *this, UINT64 *size)
{
	if (!size)
		return EFI_INVALID_PARAMETER;
	*size = 64ULL * 1024 * 1024;
	return EFI_SUCCESS;
}

static const struct {
	EFI_STATUS (EFIAPI *get)(const EFI_PEI_SERVICES **, void *, UINT64 *);
} g_memory_size = { grantley_platform_memory_size };

static EFI_STATUS grantley_memory_test_failure(EFI_STATUS status)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();
	if (state)
		state->memory_test_failed = TRUE;
	return status;
}

/* Operation values in the memory-test PPI: Ignore, Quick, Sparse, Extensive. */
static EFI_STATUS EFIAPI grantley_base_memory_test(
	const EFI_PEI_SERVICES **services, void *this, EFI_PHYSICAL_ADDRESS begin,
	UINT64 length, UINT32 operation, EFI_PHYSICAL_ADDRESS *error)
{
	if (!error || operation > 3 || begin > UINT64_MAX - length)
		return grantley_memory_test_failure(EFI_INVALID_PARAMETER);
	*error = 0;
	if (!operation || !length)
		return EFI_SUCCESS;
	/* No high-memory mapping exists while the IA-32 PEIM runs in CAR. */
	if (begin >= 0x100000000ULL || length > 0x100000000ULL - begin)
		return grantley_memory_test_failure(EFI_UNSUPPORTED);
	if ((begin & 3) || (length & 3))
		return grantley_memory_test_failure(EFI_INVALID_PARAMETER);

	UINT64 stride = operation == 3 ? sizeof(UINT32) : 4096;
	for (UINT64 offset = 0; offset < length; offset += stride) {
		void *address = (void *)(uintptr_t)(begin + offset);
		UINT32 saved = read32(address);
		write32(address, 0x55aa55aa);
		bool good = read32(address) == 0x55aa55aa;
		write32(address, 0xaa55aa55);
		good &= read32(address) == 0xaa55aa55;
		write32(address, saved);
		if (!good) {
			*error = begin + offset;
			return grantley_memory_test_failure(EFI_DEVICE_ERROR);
		}
	}
	return EFI_SUCCESS;
}

static const struct {
	EFI_STATUS (EFIAPI *test)(const EFI_PEI_SERVICES **, void *,
		EFI_PHYSICAL_ADDRESS, UINT64, UINT32, EFI_PHYSICAL_ADDRESS *);
} g_memory_test = { grantley_base_memory_test };

static const EFI_PEI_PPI_DESCRIPTOR g_initial_ppis[] = {
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_pci_cfg_guid, (void *)&g_pci_cfg },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_cpu_io_guid, (void *)0 },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_read_only_var_guid,
	  (void *)&g_read_only_var },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_mem_test_guid,
	  (void *)&g_memory_test },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
	  &g_mem_size_guid, (void *)&g_memory_size },
};

static const EFI_PEI_SERVICES g_pei_services_template = {
	.Hdr = {
		.Signature  = PEI_SERVICES_SIGNATURE,
		.Revision   = PEI_SERVICES_REVISION,
		.HeaderSize = sizeof(EFI_PEI_SERVICES),
	},
	.InstallPpi         = (EFI_PEI_INSTALL_PPI)grantley_install_ppi,
	.ReInstallPpi       = (EFI_PEI_REINSTALL_PPI)grantley_reinstall_ppi,
	.LocatePpi          = (EFI_PEI_LOCATE_PPI)grantley_locate_ppi,
	.NotifyPpi          = (EFI_PEI_NOTIFY_PPI)grantley_notify_ppi,
	.GetBootMode        = (EFI_PEI_GET_BOOT_MODE)grantley_get_boot_mode,
	.SetBootMode        = (EFI_PEI_SET_BOOT_MODE)grantley_set_boot_mode,
	.GetHobList         = (EFI_PEI_GET_HOB_LIST)grantley_get_hob_list,
	.CreateHob          = (EFI_PEI_CREATE_HOB)grantley_create_hob,
	.FfsFindNextVolume  = (EFI_PEI_FFS_FIND_NEXT_VOLUME2)grantley_ffs_stub,
	.FfsFindNextFile    = (EFI_PEI_FFS_FIND_NEXT_FILE2)grantley_ffs_stub,
	.FfsFindSectionData = (EFI_PEI_FFS_FIND_SECTION_DATA2)grantley_ffs_stub,
	.InstallPeiMemory   = (EFI_PEI_INSTALL_PEI_MEMORY)grantley_install_pei_memory,
	.AllocatePages      = (EFI_PEI_ALLOCATE_PAGES)grantley_allocate_pages,
	.AllocatePool       = (EFI_PEI_ALLOCATE_POOL)grantley_allocate_pool,
	.CopyMem            = (EFI_PEI_COPY_MEM)grantley_copy_mem,
	.SetMem             = (EFI_PEI_SET_MEM)grantley_set_mem,
	.ReportStatusCode   = (EFI_PEI_REPORT_STATUS_CODE)grantley_report_status_code,
	.ResetSystem        = (EFI_PEI_RESET_SYSTEM)grantley_reset_system,
};

static void grantley_init_hob_list(struct grantley_uncore_state *state, void *hob_buf)
{
	EFI_HOB_HANDOFF_INFO_TABLE *hit = (EFI_HOB_HANDOFF_INFO_TABLE *)hob_buf;
	*hit = (EFI_HOB_HANDOFF_INFO_TABLE){
		.Header = {
			.HobType   = EFI_HOB_TYPE_HANDOFF,
			.HobLength = sizeof(EFI_HOB_HANDOFF_INFO_TABLE),
		},
		.Version = EFI_HOB_HANDOFF_TABLE_VERSION,
		.BootMode = state->boot_mode,
		/* Only this arena is available for HOB growth, not the PEIM stack. */
		.EfiMemoryBottom = (uintptr_t)hob_buf,
		.EfiMemoryTop = (uintptr_t)hob_buf + state->hob_size,
		.EfiFreeMemoryTop = (uintptr_t)hob_buf + state->hob_size,
		.EfiFreeMemoryBottom = (uintptr_t)hob_buf +
			 sizeof(*hit) + sizeof(EFI_HOB_GENERIC_HEADER),
		.EfiEndOfHobList = (EFI_PHYSICAL_ADDRESS)(uintptr_t)hob_buf +
				   sizeof(EFI_HOB_HANDOFF_INFO_TABLE),
	};
	state->hob_allocated = sizeof(EFI_HOB_HANDOFF_INFO_TABLE);

	EFI_HOB_GENERIC_HEADER *end_hob =
		(EFI_HOB_GENERIC_HEADER *)((UINT8 *)hob_buf + state->hob_allocated);
	*end_hob = (EFI_HOB_GENERIC_HEADER){
		.HobType   = EFI_HOB_TYPE_END_OF_HOB_LIST,
		.HobLength = sizeof(EFI_HOB_GENERIC_HEADER),
	};
}

/*
 * Uncore blob requires EFI_PLATFORM_INFO GUID HOB in addition
 * to PLATFORM_INFO_PPI. Uncore blob routines (at 0x31254,
 * 0xb363c, 0xb364e, 0xb52aa, 0xb5dc0) call GetFirstGuidHob()
 * and dereference the result without checking for NULL.
 */
static EFI_STATUS grantley_init_platform_info_hob(struct grantley_uncore_state *state)
{
	void *hob = (void *)0;
	const UINT16 hob_data_len = 256;
	const UINT16 hob_total_len = sizeof(EFI_HOB_GUID_TYPE) + hob_data_len;
	const struct _EFI_PEI_SERVICES **s_ptr =
		(const struct _EFI_PEI_SERVICES **)&state->services_ptr;

	EFI_STATUS status = grantley_create_hob(s_ptr, EFI_HOB_TYPE_GUID_EXTENSION,
					      hob_total_len, &hob);
	if (status != EFI_SUCCESS)
		return status;

	EFI_HOB_GUID_TYPE *guid_hob = (EFI_HOB_GUID_TYPE *)hob;
	guid_hob->Name = g_platform_info_guid;
	uint8_t *data = (uint8_t *)(guid_hob + 1);
	memset(data, 0, hob_data_len);

	/* Signature "$PIT" (0x54495024) at offset 0x10 */
	*(uint32_t *)(data + 0x10) = 0x54495024;
	/* Table size at offset 0x14 */
	*(uint32_t *)(data + 0x14) = hob_data_len;
	/* Table revision 1 at offset 0x18 */
	*(uint16_t *)(data + 0x18) = 1;
	/* Platform Type 0 (Default / CRB) at offset 0x1a */
	*(uint16_t *)(data + 0x1a) = 0;
	/* ExtendedInfoValid = 1 at offset 0x2c */
	data[0x2c] = 1;
	/* Default BoardId policy; a mainboard may override it before PEIM entry. */
	data[0x46] = 0;
	/* MemTsegSize at offset 0x85 */
#if CONFIG(SMM_TSEG)
	*(uint32_t *)(data + 0x85) = CONFIG_SMM_TSEG_SIZE;
#endif
	/* MemIedSize at 0x89 stays zero: no debug-memory carveout is requested.
	 * The PEIM produces MemTolm/MaxTolm and MemMir0/1 during initialization;
	 * do not fabricate trained memory addresses in this input HOB.
	 */
	/* SysData.SysSioExist = 1 at offset 0x9e */
	data[0x9e] = 1;

	/* Readiness PPI: data lives in the GUID HOB; it has no callable methods. */
	state->platform_info_ppi = (EFI_PEI_PPI_DESCRIPTOR) {
		EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
		&g_platform_info_guid, data,
	};
	return grantley_install_ppi(s_ptr, &state->platform_info_ppi);
}

/* SMRAM GUID payload has an eight-byte header followed by 32-byte descriptors. */
struct grantley_smram_descriptor {
	UINT64 physical;
	UINT64 cpu;
	UINT64 size;
	UINT64 flags;
};

EFI_STATUS grantley_pei_memory_map(const struct grantley_uncore_state *state,
	uint32_t tolm_raw, uint32_t tohm_lo, uint32_t tohm_hi,
	struct grantley_memory_map *map)
{
	if (!state || !map || !state->hob_list ||
	    state->hob_allocated > state->hob_size ||
	    state->hob_size - state->hob_allocated < sizeof(EFI_HOB_GENERIC_HEADER))
		return EFI_INVALID_PARAMETER;
	if (!state->dram_initialized || state->memory_test_failed)
		return EFI_DEVICE_ERROR;
	uint64_t tolm = tolm_raw & 0xfc000000;
	uint64_t tohm = ((uint64_t)tohm_hi << 32) | (tohm_lo & 0xfc000000);
	if (tolm_raw == UINT32_MAX || tohm_lo == UINT32_MAX || tohm_hi == UINT32_MAX ||
	    tolm < 0x4000000 || tolm > 0x80000000 ||
	    (tohm && tohm < 0x100000000ULL) || tohm > (1ULL << 46))
		return EFI_DEVICE_ERROR;

	struct grantley_memory_range input[GRANTLEY_MAX_MEMORY_RANGES];
	uint64_t bounds[2 * GRANTLEY_MAX_MEMORY_RANGES];
	size_t count = 0;
	struct grantley_memory_map result = { 0 };
	static const EFI_GUID smram_guid = {
		0x6dadf1d1, 0xd4cc, 0x4910,
		{ 0xbb, 0x6e, 0x82, 0xb1, 0xfd, 0x80, 0xff, 0x3d }
	};
	const uint8_t *list = state->hob_list;
	const EFI_HOB_HANDOFF_INFO_TABLE *hit = state->hob_list;
	if (state->hob_allocated < sizeof(*hit) ||
	    hit->Header.HobType != EFI_HOB_TYPE_HANDOFF ||
	    hit->Header.HobLength != sizeof(*hit) ||
	    hit->EfiEndOfHobList != (uintptr_t)(list + state->hob_allocated))
		return EFI_VOLUME_CORRUPTED;

	for (size_t off = 0; off <= state->hob_allocated;) {
		const EFI_HOB_GENERIC_HEADER *hob = (const void *)(list + off);
		if (hob->HobLength < sizeof(*hob) || (hob->HobLength & 7) || hob->Reserved)
			return EFI_VOLUME_CORRUPTED;
		if (off == state->hob_allocated) {
			if (hob->HobType != EFI_HOB_TYPE_END_OF_HOB_LIST ||
			    hob->HobLength != sizeof(*hob))
				return EFI_VOLUME_CORRUPTED;
			break;
		}
		if (hob->HobType == EFI_HOB_TYPE_END_OF_HOB_LIST ||
		    hob->HobLength > state->hob_allocated - off)
			return EFI_VOLUME_CORRUPTED;

		if (hob->HobType == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR) {
			if (hob->HobLength < sizeof(EFI_HOB_RESOURCE_DESCRIPTOR))
				return EFI_VOLUME_CORRUPTED;
			const EFI_HOB_RESOURCE_DESCRIPTOR *r = (const void *)hob;
			if (r->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY ||
			    r->ResourceType == EFI_RESOURCE_MEMORY_RESERVED) {
				uint64_t base = r->PhysicalStart, size = r->ResourceLength;
				if (!size || base > UINT64_MAX - size ||
				    !((base < tolm && size <= tolm - base) ||
				      (base >= 0x100000000ULL && base < tohm && size <= tohm - base)))
					return EFI_DEVICE_ERROR;
				if (count == GRANTLEY_MAX_MEMORY_RANGES)
					return EFI_OUT_OF_RESOURCES;
				input[count] = (struct grantley_memory_range) {
					base, size, r->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY ?
					GRANTLEY_MEMORY_RAM : GRANTLEY_MEMORY_RESERVED_RAM
				};
				bounds[2 * count] = base;
				bounds[2 * count + 1] = base + size;
				count++;
			}
		} else if (hob->HobType == EFI_HOB_TYPE_MEMORY_ALLOCATION) {
			if (hob->HobLength < sizeof(EFI_HOB_MEMORY_ALLOCATION))
				return EFI_VOLUME_CORRUPTED;
			const EFI_HOB_MEMORY_ALLOCATION *allocation = (const void *)hob;
			const EFI_HOB_MEMORY_ALLOCATION_HEADER *a = &allocation->AllocDescriptor;
			uint64_t base = a->MemoryBaseAddress, size = a->MemoryLength;
			if (!size || base > UINT64_MAX - size ||
			    !((base < tolm && size <= tolm - base) ||
			      (base >= 0x100000000ULL && base < tohm && size <= tohm - base)))
				return EFI_DEVICE_ERROR;
			if (a->MemoryType != EfiConventionalMemory) {
				if (count == GRANTLEY_MAX_MEMORY_RANGES)
					return EFI_OUT_OF_RESOURCES;
				input[count] = (struct grantley_memory_range) {
					base, size, GRANTLEY_MEMORY_RESERVED_RAM
				};
				bounds[2 * count] = base;
				bounds[2 * count + 1] = base + size;
				count++;
			}
		} else if (hob->HobType == EFI_HOB_TYPE_GUID_EXTENSION) {
			if (hob->HobLength < sizeof(EFI_HOB_GUID_TYPE))
				return EFI_VOLUME_CORRUPTED;
			const EFI_HOB_GUID_TYPE *guid = (const void *)hob;
			if (!memcmp(&guid->Name, &smram_guid, sizeof(smram_guid))) {
				const uint8_t *data = (const void *)(guid + 1);
				size_t bytes = hob->HobLength - sizeof(*guid);
				if (bytes < 8 || result.smram_size)
					return EFI_VOLUME_CORRUPTED;
				uint32_t regions;
				memcpy(&regions, data, sizeof(regions));
				if (!regions || regions > (bytes - 8) / sizeof(struct grantley_smram_descriptor))
					return EFI_VOLUME_CORRUPTED;
				for (size_t i = 0; i < regions; i++) {
					struct grantley_smram_descriptor d;
					memcpy(&d, data + 8 + i * sizeof(d), sizeof(d));
					if (!d.size || d.physical != d.cpu || d.physical > UINT64_MAX - d.size)
						return EFI_DEVICE_ERROR;
					/* HSEG is not the relocatable TSEG used by coreboot. */
					if (d.physical < 0x100000)
						continue;
					if (result.smram_size || d.physical >= tolm || d.size > tolm - d.physical)
						return EFI_DEVICE_ERROR;
					result.smram_base = d.physical;
					result.smram_size = d.size;
				}
			}
		}
		off += hob->HobLength;
	}
	if (!count || !result.smram_size)
		return EFI_NOT_FOUND;

	/* Sweep all resource endpoints; reserved RAM overrides broad RAM descriptors. */
	for (size_t i = 1; i < 2 * count; i++) {
		uint64_t value = bounds[i];
		size_t j = i;
		while (j && bounds[j - 1] > value) {
			bounds[j] = bounds[j - 1];
			j--;
		}
		bounds[j] = value;
	}
	for (size_t i = 0; i + 1 < 2 * count; i++) {
		uint64_t base = bounds[i], end = bounds[i + 1], type = 0;
		if (base == end)
			continue;
		for (size_t j = 0; j < count; j++)
			if (input[j].base <= base && input[j].base + input[j].size >= end &&
			    input[j].type > type)
				type = input[j].type;
		if (!type)
			continue;
		struct grantley_memory_range *last =
			result.count ? &result.ranges[result.count - 1] : (void *)0;
		if (last && last->type == type && last->base + last->size == base) {
			last->size += end - base;
			continue;
		}
		if (result.count == GRANTLEY_MAX_MEMORY_RANGES)
			return EFI_OUT_OF_RESOURCES;
		result.ranges[result.count++] = (struct grantley_memory_range) { base, end - base, type };
	}
	bool covered = false, have_ram = false, pei_covered = false;
	for (size_t i = 0; i < result.count; i++) {
		const struct grantley_memory_range *r = &result.ranges[i];
		have_ram |= r->type == GRANTLEY_MEMORY_RAM && r->base < tolm;
		if (r->type == GRANTLEY_MEMORY_RAM && state->dram_size &&
		    state->dram_base >= r->base && state->dram_base < r->base + r->size &&
		    state->dram_size <= r->base + r->size - state->dram_base)
			pei_covered = true;
		if (r->type == GRANTLEY_MEMORY_RESERVED_RAM && r->base <= result.smram_base &&
		    r->base + r->size >= result.smram_base + result.smram_size)
			covered = true;
	}
	if (!covered || !have_ram || !pei_covered)
		return EFI_DEVICE_ERROR;
	*map = result;
	return EFI_SUCCESS;
}

EFI_STATUS grantley_pei_services_init(struct grantley_uncore_state *state,
				    void *pool_buf, UINTN pool_size,
				    void *hob_buf, UINTN hob_size)
{
	if (!state || !pool_buf || !hob_buf)
		return EFI_INVALID_PARAMETER;
	uintptr_t pool_start = (uintptr_t)pool_buf;
	uintptr_t hob_start = (uintptr_t)hob_buf;
	uintptr_t state_start = (uintptr_t)state;
	if ((pool_start & 7) || (hob_start & 7) ||
	    pool_size > UINTPTR_MAX - pool_start || hob_size > UINTPTR_MAX - hob_start ||
	    sizeof(*state) > UINTPTR_MAX - state_start)
		return EFI_INVALID_PARAMETER;
	uintptr_t pool_end = pool_start + pool_size;
	uintptr_t hob_end = hob_start + hob_size;
	uintptr_t state_end = state_start + sizeof(*state);
	if ((pool_start < hob_end && hob_start < pool_end) ||
	    (pool_start < state_end && state_start < pool_end) ||
	    (hob_start < state_end && state_start < hob_end))
		return EFI_INVALID_PARAMETER;

	if (hob_size < sizeof(EFI_HOB_HANDOFF_INFO_TABLE) + sizeof(EFI_HOB_GUID_TYPE) +
	    256 + sizeof(EFI_HOB_GENERIC_HEADER))
		return EFI_BUFFER_TOO_SMALL;

	memset(state, 0, sizeof(*state));
	g_state = state;

	state->services = g_pei_services_template;
	state->services_ptr = &state->services;
	state->services.CpuIo = (EFI_PEI_CPU_IO_PPI *)&g_cpu_io;
	state->services.PciCfg = (EFI_PEI_PCI_CFG2_PPI *)&g_pci_cfg;

	state->boot_mode = BOOT_WITH_FULL_CONFIGURATION;
	state->pool_base = (UINT8 *)pool_buf;
	state->pool_size = pool_size;
	state->hob_list = hob_buf;
	state->hob_size = hob_size;

	grantley_init_hob_list(state, hob_buf);
	EFI_STATUS status = grantley_install_ppi(&state->services_ptr, g_initial_ppis);
	if (status != EFI_SUCCESS)
		return status;
	return grantley_init_platform_info_hob(state);
}
