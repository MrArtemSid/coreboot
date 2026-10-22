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

	if (!state || !hob || length < sizeof(EFI_HOB_GENERIC_HEADER))
		return EFI_INVALID_PARAMETER;

	/* Align length to 8 bytes */
	UINT16 aligned_length = (length + 7) & ~7;
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

	UINTN bytes = pages * 4096;

	if (state->pool_allocated + bytes > state->pool_size)
		return EFI_OUT_OF_RESOURCES;

	*addr = (EFI_PHYSICAL_ADDRESS)(uintptr_t)(state->pool_base + state->pool_allocated);
	state->pool_allocated += bytes;
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

	UINTN aligned_size = (size + 7) & ~7;

	if (state->pool_allocated + aligned_size > state->pool_size)
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

void grantley_pei_set_mmcfg(struct grantley_uncore_state *state, uintptr_t mmcfg_base)
{
	if (!state)
		return;

	state->mmcfg_base = mmcfg_base;
	state->mmcfg_enabled = (mmcfg_base != 0);

#if !ENV_TEST
	if (mmcfg_base) {
		const pci_devfn_t iio_dev5 = PCI_DEV(0, 5, 0);

		/* MMCFG Base at 0x84 (Haswell-EP) and 0x90 (Broadwell-EP) */
		pci_s_write_config32(iio_dev5, 0x84, (uint32_t)mmcfg_base | 1);
		pci_s_write_config32(iio_dev5, 0x90, (uint32_t)mmcfg_base | 1);

		/* MMCFG Limit at 0x88 (IVT) and 0x98 (HSX/BDX) covering 256 buses */
		pci_s_write_config32(iio_dev5, 0x88, (uint32_t)mmcfg_base + 0x03ffffff);
		pci_s_write_config32(iio_dev5, 0x98, (uint32_t)mmcfg_base + 0x03ffffff);

		/* CPUBUSNO at offset 0x108 through MMCFG: routes Uncore to Bus 0xff */
		write32((void *)(mmcfg_base + (5U << 15) + 0x108), 0x0001ff00);

		/* Internal bus routing: UBOX (1:16.7:0xd0) and Cbo (1:15.5:0xc0) */
		pci_s_write_config32(PCI_DEV(1, 16, 7), 0xd0, 0x8000ff00);
		pci_s_write_config32(PCI_DEV(1, 15, 5), 0xc0, (uint32_t)mmcfg_base | 1);
		pci_update_config32(PCI_DEV(1, 16, 7), 0xdc, ~7U, 1U);

		/* PCU CPU_BUS_NUMBER on Bus 0xff, Dev 30, Func 2, offset 0x40 */
		write32((void *)(mmcfg_base + (0xffU << 20) + (30U << 15) +
				 (2U << 12) + 0x40), 0x8000ff00);
	}
#endif
}

static inline void pci_cfg_decode(UINT64 address, pci_devfn_t *pdev,
				  UINT32 *reg, UINT8 *bus, UINT8 *dev,
				  UINT8 *func)
{
	*bus = (address >> 24) & 0xff;
	*dev = (address >> 16) & 0x1f;
	*func = (address >> 8) & 0x07;
	*reg = (address >> 32) != 0 ? (UINT32)(address >> 32) :
				       (UINT32)(address & 0xfff);
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
	const struct _EFI_PEI_SERVICES **services,
	const void *variable_name,
	const void *vendor_guid,
	UINT32 *attributes,
	UINTN *data_size,
	void *data)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !variable_name || !vendor_guid || !data_size)
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
	const struct _EFI_PEI_SERVICES **services,
	UINTN *variable_name_size,
	void *variable_name,
	void *vendor_guid)
{
	return EFI_NOT_FOUND;
}

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
static EFI_GUID g_cpu_io_guid = {
	0xe682ac4, 0x6bc, 0x4bb3,
	{ 0xa9, 0xa1, 0xdb, 0x4e, 0x70, 0xda, 0x9f, 0x30 }
};
static EFI_GUID g_read_only_var_guid = {
	0x2ab86ef5, 0xecb5, 0x4134,
	{ 0xb5, 0x56, 0x38, 0x54, 0xca, 0x1f, 0xe1, 0xb4 }
};
static EFI_GUID g_read_only_var1_guid = {
	0xbae29d7b, 0x89bb, 0x4223,
	{ 0xaf, 0x76, 0x96, 0xd0, 0xb3, 0x24, 0x9b, 0x36 }
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
	.GetVariable      = (EFI_PEI_GET_VARIABLE2)grantley_get_variable,
	.NextVariableName =
		(EFI_PEI_GET_NEXT_VARIABLE_NAME2)grantley_get_next_variable_name,
};

static void *const g_dummy_mem_ops[1] = {
	(void *)grantley_report_status_code,
};

static const EFI_PEI_PPI_DESCRIPTOR g_initial_ppis[] = {
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_pci_cfg_guid, (void *)&g_pci_cfg },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_cpu_io_guid, (void *)&g_cpu_io },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_read_only_var_guid,
	  (void *)&g_read_only_var },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_read_only_var1_guid,
	  (void *)&g_read_only_var },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_mem_test_guid,
	  (void *)g_dummy_mem_ops },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_mem_size_guid,
	  (void *)g_dummy_mem_ops },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
	  &g_platform_info_guid, (void *)0 },
};

EFI_STATUS grantley_pei_services_init(struct grantley_uncore_state *state,
				    void *pool_buf, UINTN pool_size,
				    void *hob_buf, UINTN hob_size)
{
	if (!state || !pool_buf || !hob_buf)
		return EFI_INVALID_PARAMETER;

	if (hob_size < sizeof(EFI_HOB_HANDOFF_INFO_TABLE) + sizeof(EFI_HOB_GENERIC_HEADER))
		return EFI_BUFFER_TOO_SMALL;

	memset(state, 0, sizeof(*state));
	g_state = state;

	state->services_ptr = &state->services;
	state->boot_mode = BOOT_WITH_FULL_CONFIGURATION;

	state->pool_base = (UINT8 *)pool_buf;
	state->pool_size = pool_size;
	state->pool_allocated = 0;

	state->hob_list = hob_buf;
	state->hob_size = hob_size;

	/* Setup initial Handoff Information Table (HIT) at the head of HOB list */
	EFI_HOB_HANDOFF_INFO_TABLE *hit = (EFI_HOB_HANDOFF_INFO_TABLE *)hob_buf;

	hit->Header.HobType = EFI_HOB_TYPE_HANDOFF;
	hit->Header.HobLength = sizeof(EFI_HOB_HANDOFF_INFO_TABLE);
	hit->Header.Reserved = 0;
	hit->Version = EFI_HOB_HANDOFF_TABLE_VERSION;
	hit->BootMode = state->boot_mode;
	hit->EfiMemoryTop = 0;
	hit->EfiMemoryBottom = 0;
	hit->EfiFreeMemoryTop = 0;
	hit->EfiFreeMemoryBottom = 0;
	hit->EfiEndOfHobList =
		(EFI_PHYSICAL_ADDRESS)(uintptr_t)hob_buf + sizeof(EFI_HOB_HANDOFF_INFO_TABLE);

	state->hob_allocated = sizeof(EFI_HOB_HANDOFF_INFO_TABLE);

	/* Place the terminating END_OF_HOB_LIST */
	EFI_HOB_GENERIC_HEADER *end_hob =
		(EFI_HOB_GENERIC_HEADER *)((UINT8 *)hob_buf + state->hob_allocated);
	end_hob->HobType = EFI_HOB_TYPE_END_OF_HOB_LIST;
	end_hob->HobLength = sizeof(EFI_HOB_GENERIC_HEADER);
	end_hob->Reserved = 0;

	/* Initialize PEI services table header */
	state->services.Hdr.Signature = PEI_SERVICES_SIGNATURE;
	state->services.Hdr.Revision = PEI_SERVICES_REVISION;
	state->services.Hdr.HeaderSize = sizeof(EFI_TABLE_HEADER);
	state->services.Hdr.CRC32 = 0;
	state->services.Hdr.Reserved = 0;

	/* Install function pointers */
	state->services.InstallPpi = (EFI_PEI_INSTALL_PPI)grantley_install_ppi;
	state->services.ReInstallPpi = (EFI_PEI_REINSTALL_PPI)grantley_reinstall_ppi;
	state->services.LocatePpi = (EFI_PEI_LOCATE_PPI)grantley_locate_ppi;
	state->services.NotifyPpi = (EFI_PEI_NOTIFY_PPI)grantley_notify_ppi;
	state->services.GetBootMode = (EFI_PEI_GET_BOOT_MODE)grantley_get_boot_mode;
	state->services.SetBootMode = (EFI_PEI_SET_BOOT_MODE)grantley_set_boot_mode;
	state->services.GetHobList = (EFI_PEI_GET_HOB_LIST)grantley_get_hob_list;
	state->services.CreateHob = (EFI_PEI_CREATE_HOB)grantley_create_hob;
	state->services.FfsFindNextVolume = (EFI_PEI_FFS_FIND_NEXT_VOLUME2)grantley_ffs_stub;
	state->services.FfsFindNextFile = (EFI_PEI_FFS_FIND_NEXT_FILE2)grantley_ffs_stub;
	state->services.FfsFindSectionData = (EFI_PEI_FFS_FIND_SECTION_DATA2)grantley_ffs_stub;
	state->services.InstallPeiMemory = (EFI_PEI_INSTALL_PEI_MEMORY)grantley_install_pei_memory;
	state->services.AllocatePages = (EFI_PEI_ALLOCATE_PAGES)grantley_allocate_pages;
	state->services.AllocatePool = (EFI_PEI_ALLOCATE_POOL)grantley_allocate_pool;
	state->services.CopyMem = (EFI_PEI_COPY_MEM)grantley_copy_mem;
	state->services.SetMem = (EFI_PEI_SET_MEM)grantley_set_mem;
	state->services.ReportStatusCode = (EFI_PEI_REPORT_STATUS_CODE)grantley_report_status_code;
	state->services.ResetSystem = (EFI_PEI_RESET_SYSTEM)grantley_reset_system;

	/* Install core PPIs into services */
	state->services.CpuIo = (EFI_PEI_CPU_IO_PPI *)&g_cpu_io;
	state->services.PciCfg = (EFI_PEI_PCI_CFG2_PPI *)&g_pci_cfg;
	grantley_install_ppi(&state->services_ptr, g_initial_ppis);

	/*
	 * Uncore blob requires EFI_PLATFORM_INFO GUID HOB in addition
	 * to PLATFORM_INFO_PPI. Uncore blob routines (at 0x31254,
	 * 0xb363c, 0xb364e, 0xb52aa, 0xb5dc0) call GetFirstGuidHob()
	 * and dereference the result without checking for NULL.
	 */
	void *hob = (void *)0;
	const UINT16 hob_data_len = 256;
	const UINT16 hob_total_len =
		sizeof(EFI_HOB_GUID_TYPE) + hob_data_len;
	const struct _EFI_PEI_SERVICES **s_ptr =
		(const struct _EFI_PEI_SERVICES **)&state->services_ptr;
	if (grantley_create_hob(s_ptr, EFI_HOB_TYPE_GUID_EXTENSION,
				hob_total_len, &hob) == EFI_SUCCESS &&
	    hob) {
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
		/* BoardId = 0 (Default CRB) at offset 0x46 */
		data[0x46] = 0;
		/* MemTsegSize at offset 0x85 */
#if CONFIG(SMM_TSEG)
		*(uint32_t *)(data + 0x85) = CONFIG_SMM_TSEG_SIZE;
#endif
		/* SysData.SysSioExist = 1 at offset 0x9e */
		data[0x9e] = 1;
	}

	return EFI_SUCCESS;
}
