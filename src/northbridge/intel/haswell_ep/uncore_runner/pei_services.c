/* SPDX-License-Identifier: GPL-2.0-only */

#include <string.h>
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
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI grantley_reset_system(
	const struct _EFI_PEI_SERVICES **services)
{
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI grantley_ffs_stub(void)
{
	return EFI_NOT_FOUND;
}

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

	return EFI_SUCCESS;
}
