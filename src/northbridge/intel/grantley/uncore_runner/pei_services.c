/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <device/mmio.h>
#include <post.h>
#include <console/console.h>
#include <arch/io.h>
#include <cpu/cpu.h>
#include <device/pci_ops.h>
#undef NULL
#include "pei_services.h"
#include "pei_image.h"
#include "../memmap.h"

static struct grantley_uncore_state *g_state;

static struct grantley_uncore_state *grantley_get_uncore_state(void)
{
	return g_state;
}

static bool grantley_ppi_descriptor_valid(const EFI_PEI_PPI_DESCRIPTOR *desc)
{
	/* PI: InstallPpi() only accepts EFI_PEI_PPI_DESCRIPTOR_PPI entries;
	 * notification descriptors are rejected with EFI_INVALID_PARAMETER.
	 */
	return desc->Guid != (void *)0 &&
	       (desc->Flags & EFI_PEI_PPI_DESCRIPTOR_PPI) != 0 &&
	       (desc->Flags & EFI_PEI_PPI_DESCRIPTOR_NOTIFY_TYPES) == 0;
}

static EFI_STATUS EFIAPI grantley_install_ppi(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_PPI_DESCRIPTOR * ppi_list)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !ppi_list)
		return EFI_INVALID_PARAMETER;

	/* Validate the whole list before installing anything: a partial install
	 * would leave the PPI database in a state the caller cannot undo.
	 */
	const EFI_PEI_PPI_DESCRIPTOR *desc = ppi_list;
	UINTN count = 0;

	for (;;) {
		if (!grantley_ppi_descriptor_valid(desc))
			return EFI_INVALID_PARAMETER;
		count++;
		if (desc->Flags & EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST)
			break;
		desc++;
	}

	if (count > MAX_GRANTLEY_PPI_DESCRIPTORS - state->ppi_count)
		return EFI_OUT_OF_RESOURCES;

	/* No notification dispatch happens here: this runner has no end-of-PEI
	 * point and does not implement NotifyPpi() (see grantley_notify_ppi()).
	 */
	for (UINTN i = 0; i < count; i++)
		state->ppi_descriptors[state->ppi_count++] = &ppi_list[i];

	return EFI_SUCCESS;
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
	/* EDK2's PeiCreateHob zeroes the whole record: callers rely on a cleared
	 * payload and the storage may still contain the HOB it replaces.
	 */
	memset(new_hob, 0, aligned_length);
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

/*
 * The module does not allocate pages, so the region is only recorded here
 * and checked against the HOB memory map after the module returns.
 */
static EFI_STATUS EFIAPI grantley_install_pei_memory(
	const struct _EFI_PEI_SERVICES **services,
	EFI_PHYSICAL_ADDRESS memory_begin,
	UINT64 memory_length)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !memory_length || memory_begin < GRANTLEY_PEI_MEMORY_FLOOR ||
	    memory_begin >= GRANTLEY_PEI_MEMORY_LIMIT ||
	    memory_length > GRANTLEY_PEI_MEMORY_LIMIT - memory_begin)
		return EFI_INVALID_PARAMETER;
	if (state->dram_initialized)
		return EFI_SUCCESS;

	printk(BIOS_INFO, "PEIM: InstallPeiMemory(0x%llx, 0x%llx)\n",
	       (unsigned long long)memory_begin, (unsigned long long)memory_length);
	state->dram_base = memory_begin;
	state->dram_size = memory_length;
	state->dram_initialized = TRUE;
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

	if (size > (UINTN)-1 - 7)
		return EFI_OUT_OF_RESOURCES;

	UINTN aligned_size = (size + 7) & ~(UINTN)7;

	if (state->pool_allocated > state->pool_size)
		return EFI_OUT_OF_RESOURCES;
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

/* EFI_PEI_PCI_CFG_ADDRESS: register in bits 63:32 when non-zero, else 7:0. */
static pci_devfn_t pci_cfg_decode(UINT64 address, UINT16 *reg)
{
	*reg = (address >> 32) ? (UINT16)(address >> 32) : (UINT16)(address & 0xff);
	return PCI_DEV((address >> 24) & 0xff, (address >> 16) & 0x1f,
		       (address >> 8) & 0x07);
}

static EFI_STATUS EFIAPI grantley_pci_cfg_read(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_PCI_CFG2_PPI * this,
	EFI_PEI_PCI_CFG_PPI_WIDTH width,
	UINT64 address,
	void *buffer)
{
	UINT16 reg;
	pci_devfn_t dev = pci_cfg_decode(address, &reg);

	if (!buffer)
		return EFI_INVALID_PARAMETER;

	switch (width) {
	case EfiPeiPciCfgWidthUint8:
		*(UINT8 *)buffer = pci_s_read_config8(dev, reg);
		return EFI_SUCCESS;
	case EfiPeiPciCfgWidthUint16:
		*(UINT16 *)buffer = pci_s_read_config16(dev, reg);
		return EFI_SUCCESS;
	case EfiPeiPciCfgWidthUint32:
		*(UINT32 *)buffer = pci_s_read_config32(dev, reg);
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
	UINT16 reg;
	pci_devfn_t dev = pci_cfg_decode(address, &reg);

	if (!buffer)
		return EFI_INVALID_PARAMETER;

	switch (width) {
	case EfiPeiPciCfgWidthUint8:
		pci_s_write_config8(dev, reg, *(UINT8 *)buffer);
		return EFI_SUCCESS;
	case EfiPeiPciCfgWidthUint16:
		pci_s_write_config16(dev, reg, *(UINT16 *)buffer);
		return EFI_SUCCESS;
	case EfiPeiPciCfgWidthUint32:
		pci_s_write_config32(dev, reg, *(UINT32 *)buffer);
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

void grantley_pei_set_intelsetup(struct grantley_uncore_state *state,
				 const void *data, size_t size)
{
	if (!state)
		return;

	state->setup = data;
	state->setup_size = data ? size : 0;
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

static bool grantley_is_intelsetup(const CHAR16 *name)
{
	static const char setup_name[] = "IntelSetup";

	for (size_t i = 0; i < sizeof(setup_name); i++)
		if (name[i] != (CHAR16)setup_name[i])
			return false;
	return true;
}

static EFI_STATUS EFIAPI grantley_get_variable(
	const EFI_PEI_READ_ONLY_VARIABLE2_PPI *this,
	const CHAR16 *variable_name,
	const EFI_GUID *vendor_guid,
	UINT32 *attributes,
	UINTN *data_size,
	void *data)
{
	static const EFI_GUID setup_guid = GRANTLEY_INTELSETUP_VENDOR_GUID;
	const size_t serial_level_offset =
		CONFIG_GRANTLEY_INTELSETUP_SERIAL_LEVEL_OFFSET;
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !this || !variable_name || !vendor_guid || !data_size)
		return EFI_INVALID_PARAMETER;

	if (!state->setup || !grantley_is_intelsetup(variable_name) ||
	    memcmp(vendor_guid, &setup_guid, sizeof(setup_guid)) != 0)
		return EFI_NOT_FOUND;

	if (!data || *data_size < state->setup_size) {
		*data_size = state->setup_size;
		return EFI_BUFFER_TOO_SMALL;
	}

	memcpy(data, state->setup, state->setup_size);
	if (CONFIG(GRANTLEY_UNCORE_RC_SERIAL) && serial_level_offset &&
	    serial_level_offset < state->setup_size)
		((uint8_t *)data)[serial_level_offset] = GRANTLEY_RC_SERIAL_SDBG_MIN;

	*data_size = state->setup_size;
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

typedef EFI_STATUS (*grantley_peim_entry_t)(
	void *file_handle,
	const struct _EFI_PEI_SERVICES **services);

EFI_STATUS grantley_run_uncore_peim(const void *pe_base, size_t pe_size,
				    struct grantley_uncore_state *state)
{
	if (!pe_base || !state)
		return EFI_INVALID_PARAMETER;

	uintptr_t entry;
	EFI_STATUS status = grantley_peim_entry(pe_base, pe_size, &entry);
	if (status != EFI_SUCCESS)
		return status;
	grantley_peim_entry_t entry_point = (grantley_peim_entry_t)entry;

	/*
	 * UncoreInitPeim allocates ~440 KB on the stack at entry
	 * (e.g. sub $0x6db38, %esp). Switch to a dedicated stack at
	 * top of CAR (0xfe1fffc0), providing >800 KB of headroom,
	 * then restore coreboot's esp upon return.
	 */
	uintptr_t car_stack_top =
		CONFIG_DCACHE_RAM_BASE + CONFIG_DCACHE_RAM_SIZE - 64;
	const void *services_arg = &state->services_ptr;

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

	return status;
}

static EFI_STATUS EFIAPI grantley_ffs_stub(void)
{
	return EFI_NOT_FOUND;
}

/* The module never calls the remaining services. */
static EFI_STATUS EFIAPI grantley_unsupported_service(void)
{
	printk(BIOS_ERR, "PEIM: unsupported PEI service called from %p\n",
	       __builtin_return_address(0));
	return EFI_UNSUPPORTED;
}

/* The module only reads PCD token 1, the ECAM base. */
static UINT64 EFIAPI grantley_pcd_get64(UINTN token)
{
	if (token == 1)
		return CONFIG_ECAM_MMCONF_BASE_ADDRESS;
	printk(BIOS_ERR, "PEIM: unsupported PCD token %lu\n", (unsigned long)token);
	return 0;
}

static UINTN EFIAPI grantley_pcd_unsupported(void)
{
	printk(BIOS_ERR, "PEIM: unsupported PCD service\n");
	return 0;
}

static PCD_PPI g_pcd;
static EFI_GUID g_pcd_guid = PCD_PPI_GUID;

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

/* Stock D4M4 returns 0x12c00000 plus the default memory-type pages. */
#define GRANTLEY_PEI_PLATFORM_MEMORY_SIZE (0x12c00000ULL + (0xe30ULL << 12))

static EFI_STATUS EFIAPI grantley_platform_memory_size(
	const struct _EFI_PEI_SERVICES **services, void *this, UINT64 *size)
{
	if (!size)
		return EFI_INVALID_PARAMETER;
	*size = GRANTLEY_PEI_PLATFORM_MEMORY_SIZE;
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

/* Operation values in the memory-test PPI: Ignore, Quick, Sparse, Extensive.
 *
 * Stock 0xB6EC423C is reported to write 0x5A5A5A5A at 0x40000-byte strides
 * and to leave the pattern in place.  This port deliberately does not do that:
 * it runs only when the PEIM asks, saves/restores each word, and refuses
 * high-memory addresses while the IA-32 PEIM runs in CAR.  A destructive stock
 * shape needs a validated DRAM range first; that evidence is not in this
 * slice.
 */
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
	/* PI: the CpuIo PPI must expose the interface, not a NULL placeholder. */
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_cpu_io_guid, (void *)&g_cpu_io },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_read_only_var_guid,
	  (void *)&g_read_only_var },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_mem_test_guid,
	  (void *)&g_memory_test },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_mem_size_guid,
	  (void *)&g_memory_size },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
	  &g_pcd_guid, &g_pcd },
};

static const EFI_PEI_SERVICES g_pei_services_template = {
	.Hdr = {
		.Signature  = PEI_SERVICES_SIGNATURE,
		.Revision   = PEI_SERVICES_REVISION,
		.HeaderSize = sizeof(EFI_PEI_SERVICES),
	},
	.InstallPpi         = (EFI_PEI_INSTALL_PPI)grantley_install_ppi,
	.ReInstallPpi       = (EFI_PEI_REINSTALL_PPI)grantley_unsupported_service,
	.LocatePpi          = (EFI_PEI_LOCATE_PPI)grantley_locate_ppi,
	.NotifyPpi          = (EFI_PEI_NOTIFY_PPI)grantley_unsupported_service,
	.GetBootMode        = (EFI_PEI_GET_BOOT_MODE)grantley_get_boot_mode,
	.SetBootMode        = (EFI_PEI_SET_BOOT_MODE)grantley_unsupported_service,
	.GetHobList         = (EFI_PEI_GET_HOB_LIST)grantley_get_hob_list,
	.CreateHob          = (EFI_PEI_CREATE_HOB)grantley_create_hob,
	.FfsFindNextVolume  = (EFI_PEI_FFS_FIND_NEXT_VOLUME2)grantley_ffs_stub,
	.FfsFindNextFile    = (EFI_PEI_FFS_FIND_NEXT_FILE2)grantley_ffs_stub,
	.FfsFindSectionData = (EFI_PEI_FFS_FIND_SECTION_DATA2)grantley_ffs_stub,
	.InstallPeiMemory   = (EFI_PEI_INSTALL_PEI_MEMORY)grantley_install_pei_memory,
	.AllocatePages      = (EFI_PEI_ALLOCATE_PAGES)grantley_unsupported_service,
	.AllocatePool       = (EFI_PEI_ALLOCATE_POOL)grantley_allocate_pool,
	.CopyMem            = (EFI_PEI_COPY_MEM)grantley_copy_mem,
	.SetMem             = (EFI_PEI_SET_MEM)grantley_unsupported_service,
	.ReportStatusCode   = (EFI_PEI_REPORT_STATUS_CODE)grantley_report_status_code,
	.ResetSystem        = (EFI_PEI_RESET_SYSTEM)grantley_unsupported_service,
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
	/* Temporary HEDT profile, policy revision zero from the cleared payload.
	 * A mainboard can override both fields before PEIM entry.
	 */
	data[0x46] = 0x13;
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

EFI_STATUS grantley_pei_services_init(struct grantley_uncore_state *state,
				    void *pool_buf, UINTN pool_size,
				    void *hob_buf, UINTN hob_size)
{
	if (!state || !pool_buf || !hob_buf)
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
	UINTN (EFIAPI **pcd_slot)(void) = (void *)&g_pcd;
	for (size_t i = 0; i < sizeof(g_pcd) / sizeof(*pcd_slot); i++)
		pcd_slot[i] = grantley_pcd_unsupported;
	g_pcd.Get64 = grantley_pcd_get64;
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
