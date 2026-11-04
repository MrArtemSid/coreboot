/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <cf9_reset.h>
#include <cpu/intel/grantley/grantley_routing.h>
#include <device/mmio.h>
#include <drivers/efi/efivars.h>
#include <post.h>
#include <console/console.h>
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
#if CONFIG(GRANTLEY_UNCORE_NATIVE_ENTRY)
#include "pei_image.h"
#include "pei_pcd.h"
#endif
#include "native_contract.h"
#include "../memmap.h"

static struct grantley_uncore_state *g_state;

struct grantley_uncore_state *grantley_get_uncore_state(void)
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

static EFI_STATUS EFIAPI grantley_reinstall_ppi(
	const struct _EFI_PEI_SERVICES **services,
	const EFI_PEI_PPI_DESCRIPTOR *old_ppi,
	const EFI_PEI_PPI_DESCRIPTOR *new_ppi)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();

	if (!state || !old_ppi || !new_ppi)
		return EFI_INVALID_PARAMETER;
	if (!grantley_ppi_descriptor_valid(new_ppi) || !old_ppi->Guid ||
	    !new_ppi->Guid)
		return EFI_INVALID_PARAMETER;
	/* PI: the replacement must describe the same interface. */
	if (memcmp(old_ppi->Guid, new_ppi->Guid, sizeof(EFI_GUID)) != 0)
		return EFI_INVALID_PARAMETER;

	for (UINTN i = 0; i < state->ppi_count; i++) {
		if (state->ppi_descriptors[i] != old_ppi)
			continue;
		state->ppi_descriptors[i] = new_ppi;
		return EFI_SUCCESS;
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
	/* This runner executes one PEIM and has no end-of-PEI dispatch point.
	 * It cannot honour either notification flavour, so it refuses the
	 * registration instead of reporting success and losing the callback.
	 * The vendor PEIM never calls NotifyPpi (0 `call *0x24` in the image).
	 */
	if (!notify_list || !notify_list->Guid || !notify_list->Notify)
		return EFI_INVALID_PARAMETER;
	if ((notify_list->Flags & EFI_PEI_PPI_DESCRIPTOR_NOTIFY_TYPES) == 0)
		return EFI_INVALID_PARAMETER;

	return EFI_UNSUPPORTED;
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

static UINT64 grantley_page_down(UINT64 value)
{
	return value & ~(UINT64)0xfff;
}

static UINT64 grantley_page_up(UINT64 value)
{
	return (value + 0xfff) & ~(UINT64)0xfff;
}

/*
 * Overlap test used on both sides of the allocator.  An unrepresentable range
 * (base + size past the end of the address space) cannot describe memory and
 * is reported as overlapping so that every caller refuses it instead of
 * silently skipping the check.
 */
static BOOLEAN grantley_ranges_overlap(UINT64 base_a, UINT64 size_a,
				       UINT64 base_b, UINT64 size_b)
{
	if (!size_a || !size_b)
		return FALSE;
	if (base_a > UINT64_MAX - size_a || base_b > UINT64_MAX - size_b)
		return TRUE;
	return base_a < base_b + size_b && base_b < base_a + size_a;
}

/* Regions the page allocator must never hand out once an arena exists. */
static BOOLEAN grantley_arena_region_excluded(const struct grantley_uncore_state *state,
					      UINT64 base, UINT64 size)
{
	return grantley_ranges_overlap(base, size, CONFIG_DCACHE_RAM_BASE,
				       CONFIG_DCACHE_RAM_SIZE) ||
	       grantley_ranges_overlap(base, size, (UINT64)(uintptr_t)state->pool_base,
				       state->pool_size) ||
	       grantley_ranges_overlap(base, size, (UINT64)(uintptr_t)state->hob_list,
				       state->hob_size) ||
	       grantley_ranges_overlap(base, size, (UINT64)(uintptr_t)state,
				       sizeof(*state));
}

EFI_STATUS grantley_pei_set_memory_window(struct grantley_uncore_state *state,
					  uint64_t ceiling,
					  EFI_PHYSICAL_ADDRESS reserved_base,
					  uint64_t reserved_size,
					  BOOLEAN reserved_known)
{
	if (!state || !ceiling)
		return EFI_INVALID_PARAMETER;
	if (state->dram_initialized)
		return EFI_ALREADY_STARTED;

	/*
	 * Nothing at or above the CAR base is claimable DRAM: the runner
	 * executes the PEIM from CONFIG_DCACHE_RAM_BASE and on a stack at
	 * CONFIG_DCACHE_RAM_BASE + CONFIG_DCACHE_RAM_SIZE - 64 (romstage.c).
	 * An arena that could reach either would hand the PEIM the very
	 * memory it is running from, so bound the window at the CAR base
	 * instead of validating the range later.
	 */
	if (ceiling > (uint64_t)CONFIG_DCACHE_RAM_BASE)
		return EFI_INVALID_PARAMETER;

	/* A reserved range that does not fit in the address space cannot
	 * describe memory; refuse it instead of wrapping in the overlap test. */
	if (reserved_known && reserved_size &&
	    reserved_base > UINT64_MAX - reserved_size)
		return EFI_INVALID_PARAMETER;

	/* A known exclusion with a base but no size is a half-resolved value:
	 * it would re-create the "no exclusion" fail-open while carrying the
	 * resolved flag.  Only (0, 0) may declare "positively none". */
	if (reserved_known && !reserved_size && reserved_base)
		return EFI_INVALID_PARAMETER;

	state->memory_ceiling = ceiling;
	state->arena_ceiling = ceiling;
	state->smram_base = reserved_size ? (uint64_t)reserved_base : 0;
	state->smram_size = reserved_size;
	/* Explicit: an exclusion nobody has resolved yet is not "no exclusion". */
	state->exclusion_known = reserved_known;
	return EFI_SUCCESS;
}

/* InstallPeiMemory() policy.
 *
 * This runner does not implement the generic PEI-core contract: the stock PEI
 * core records the region and raises SwitchStackSignal, after which the
 * dispatcher creates a Stack HOB, migrates the HOB heap and the stack into the
 * region, rewrites the PHIT and switches stacks.  None of that exists here
 * (no dispatcher, no stack migration, no PHIT rewrite).
 *
 * What it does instead: once the MRC has trained DRAM, the backing window is
 * pinned from the TOLM read back from hardware, and a claim is accepted only
 * when it fits that window; the accepted claim becomes the arena the service
 * hands out.  A malformed claim, or one that cannot be backed by a decoded
 * trained TOLM, is refused.  Refusal is fatal on the target because the PEIM
 * does not test the status: the arena is zeroed, post 0xe7 is issued and die()
 * is called, so no unvalidated memory is ever handed out.  Under ENV_TEST the
 * refusal is returned so the tests can observe it.
 */
void grantley_pei_set_smram(struct grantley_uncore_state *state,
			    uint64_t base, uint64_t size)
{
	if (!state)
		return;
	if (!size || (base & 0xfff) || (size & 0xfff) ||
	    base >= GRANTLEY_PEI_MEMORY_LIMIT ||
	    size > GRANTLEY_PEI_MEMORY_LIMIT - base) {
		state->smram_base = 0;
		state->smram_size = 0;
		/* An unusable window is not a resolved one. */
		state->exclusion_known = FALSE;
		return;
	}
	state->smram_base = base;
	state->smram_size = size;
	/* The same pair bounds a claim accepted through InstallPeiMemory(). */
	state->exclusion_known = TRUE;
}

const char *grantley_pei_memory_refusal_string(enum grantley_pei_memory_refusal why)
{
	switch (why) {
	case GRANTLEY_PEI_MEMORY_REGION_OK:
		return "region passes address/overlap checks";
	case GRANTLEY_PEI_MEMORY_REFUSED_INVALID:
		return "invalid length or address overflow";
	case GRANTLEY_PEI_MEMORY_REFUSED_UNREACHABLE:
		return "region is not fully inside 1 MiB..4 GiB";
	case GRANTLEY_PEI_MEMORY_REFUSED_LIVE_STORAGE:
		return "region overlaps state/pool/HOB/CAR storage";
	case GRANTLEY_PEI_MEMORY_REFUSED_SMRAM:
		return "region overlaps the configured SMRAM/TSEG window";
	case GRANTLEY_PEI_MEMORY_REFUSED_NO_VENDOR_CONTRACT:
		return "vendor InstallPeiMemory contract (HOB/stack/PHIT "
		       "migration) is not implemented";
	case GRANTLEY_PEI_MEMORY_REFUSED_WINDOW:
		return "claim does not fit the pinned DRAM window "
		       "(ceiling/exclusion/alignment)";
	case GRANTLEY_PEI_MEMORY_REFUSED_NO_TOLM:
		return "trained TOLM readback is absent or unusable (zero, "
		       "reserved bits set, above ECAM, or too small for TSEG)";
	}
	return "unknown memory refusal";
}

enum grantley_pei_memory_refusal
grantley_pei_memory_contract_refusal(const struct grantley_uncore_state *state,
				     uint64_t begin, uint64_t length)
{
	if (!state || !length || length > UINT64_MAX - begin)
		return GRANTLEY_PEI_MEMORY_REFUSED_INVALID;

	uint64_t end = begin + length;
	/* The whole region must be reachable by a flat IA-32 PEIM; clipping a
	 * region that straddles 4 GiB would be a false success.
	 */
	if (begin < GRANTLEY_PEI_MEMORY_FLOOR ||
	    begin >= GRANTLEY_PEI_MEMORY_LIMIT ||
	    end > GRANTLEY_PEI_MEMORY_LIMIT)
		return GRANTLEY_PEI_MEMORY_REFUSED_UNREACHABLE;

	const struct {
		uintptr_t start;
		uint64_t length;
	} live[] = {
		{ (uintptr_t)state, sizeof(*state) },
		{ (uintptr_t)state->pool_base, state->pool_size },
		{ (uintptr_t)state->hob_list, state->hob_size },
		{ (uintptr_t)CONFIG_DCACHE_RAM_BASE, CONFIG_DCACHE_RAM_SIZE },
	};

	for (size_t i = 0; i < ARRAY_SIZE(live); i++) {
		if (!live[i].length || !live[i].start)
			continue;
		uint64_t live_end = (uint64_t)live[i].start + live[i].length;
		if (begin < live_end && (uint64_t)live[i].start < end)
			return GRANTLEY_PEI_MEMORY_REFUSED_LIVE_STORAGE;
	}

	if (state->smram_size) {
		uint64_t smram_end = state->smram_base + state->smram_size;
		if (begin < smram_end && state->smram_base < end)
			return GRANTLEY_PEI_MEMORY_REFUSED_SMRAM;
	}

	return GRANTLEY_PEI_MEMORY_REFUSED_NO_VENDOR_CONTRACT;
}

EFI_STATUS grantley_pei_memory_region_check(const struct grantley_uncore_state *state,
					    uint64_t begin, uint64_t length)
{
	switch (grantley_pei_memory_contract_refusal(state, begin, length)) {
	case GRANTLEY_PEI_MEMORY_REGION_OK:
	case GRANTLEY_PEI_MEMORY_REFUSED_NO_VENDOR_CONTRACT:
		return EFI_SUCCESS;
	case GRANTLEY_PEI_MEMORY_REFUSED_INVALID:
		return EFI_INVALID_PARAMETER;
	case GRANTLEY_PEI_MEMORY_REFUSED_UNREACHABLE:
		return EFI_UNSUPPORTED;
	case GRANTLEY_PEI_MEMORY_REFUSED_LIVE_STORAGE:
	case GRANTLEY_PEI_MEMORY_REFUSED_SMRAM:
		return EFI_INVALID_PARAMETER;
	case GRANTLEY_PEI_MEMORY_REFUSED_WINDOW:
		/* Not produced by the classifier; a claim outside the pinned
		 * window is an unsupported range for this address check. */
		return EFI_UNSUPPORTED;
	case GRANTLEY_PEI_MEMORY_REFUSED_NO_TOLM:
		/* Also install-path only: no trained bound to check against. */
		return EFI_UNSUPPORTED;
	}
	return EFI_UNSUPPORTED;
}

/*
 * Validate and record the range a PEIM installs as permanent memory.
 *
 * Only reachable once a window is pinned; without one the service keeps its
 * E7 refusal.  The stock core does nothing of this: 0xFFDA26FF stores Begin,
 * Length and Begin + Length, sets PeiMemoryInstalled and returns EFI_SUCCESS.
 * Everything that keeps the arena from describing memory the platform cannot
 * hand out is therefore this provider's job.  A rejected range is never
 * recorded, and the first accepted range is never replaced.
 */
static EFI_STATUS grantley_pei_memory_install(struct grantley_uncore_state *state,
					      EFI_PHYSICAL_ADDRESS begin,
					      UINT64 length)
{
	UINT64 start = begin;
	UINT64 end;
	UINT64 floor;

	if (!state)
		return EFI_INVALID_PARAMETER;

	if (!length || start > UINT64_MAX - length)
		return EFI_INVALID_PARAMETER;
	end = start + length;

	/* Without a pinned window there is nothing to validate against; the
	 * caller refuses long before this point. */
	if (!state->memory_ceiling)
		return EFI_NOT_READY;
	/* The claim must be backed by the validated window (TOLM). */
	if (end > state->memory_ceiling)
		return EFI_INVALID_PARAMETER;

	floor = grantley_page_up(start);

	/* The registered span may reach past the allocatable top: the top of
	 * low DRAM is where the Uncore PEIM has already committed TSEG (its
	 * GetMemoryMap() runs before this callback).  A claim ending there is
	 * valid, but no page at or above arena_ceiling may ever be handed out,
	 * so the arena stops below it. */
	UINT64 top = end < state->arena_ceiling ? end : state->arena_ceiling;

	/* Pages are only exposed from the first page boundary at or after
	 * begin: the partial head is not handed out.  If no whole page remains
	 * the arena could never satisfy a single AllocatePages request, so the
	 * claim is refused instead of recording a range the allocator can only
	 * fail on.
	 */
	if (floor >= top || top - floor < 4096)
		return EFI_INVALID_PARAMETER;

	/* An exclusion that has not been resolved yet must not be read as "the
	 * platform has none": that is how a TSEG gets handed out as DRAM. */
	if (!state->exclusion_known)
		return EFI_NOT_READY;

	/* A TSEG/IED the platform has published is not DRAM at all.  The range
	 * may legitimately cover storage the runner is still using (the stock
	 * PEIM claims all of low DRAM); what must never happen is that a page
	 * is handed out over it, which grantley_allocate_pages() enforces.
	 */
	if (grantley_ranges_overlap(floor, end - floor, state->smram_base,
				    state->smram_size))
		return EFI_INVALID_PARAMETER;

	state->dram_base = floor;
	state->dram_size = top - floor;
	state->dram_free_top = top;
	state->dram_initialized = TRUE;
	return EFI_SUCCESS;
}

static EFI_STATUS grantley_pei_pin_window_from_hardware(struct grantley_uncore_state *state);

static EFI_STATUS EFIAPI grantley_install_pei_memory(
	const struct _EFI_PEI_SERVICES **services,
	EFI_PHYSICAL_ADDRESS memory_begin,
	UINT64 memory_length)
{
	struct grantley_uncore_state *state = grantley_get_uncore_state();
	EFI_STATUS status;
	enum grantley_pei_memory_refusal why;

	if (!state)
		return EFI_INVALID_PARAMETER;

	/* First accepted arena wins: the PEIM never tests this status, so a
	 * second claim may not silently replace memory already handed out. */
	if (state->dram_initialized)
		return EFI_SUCCESS;

	/* A malformed claim is refused before any register is read. */
	if (!memory_length || (uint64_t)memory_begin > UINT64_MAX - memory_length) {
		why = GRANTLEY_PEI_MEMORY_REFUSED_INVALID;
		status = EFI_INVALID_PARAMETER;
		goto refuse;
	}

	/* M4: the bounds exist at this moment, not before entry.  Pin them from
	 * the TOLM the MRC just programmed; a readback that cannot be decoded
	 * keeps the refusal (training did not publish a limit). */
	if (!state->memory_ceiling) {
		status = grantley_pei_pin_window_from_hardware(state);
		if (status != EFI_SUCCESS) {
			why = GRANTLEY_PEI_MEMORY_REFUSED_NO_TOLM;
			status = EFI_UNSUPPORTED;
			goto refuse;
		}
	}

	status = grantley_pei_memory_install(state, memory_begin, memory_length);
	if (status == EFI_SUCCESS) {
		state->memory_install_failed = FALSE;
		state->memory_refused = FALSE;
		printk(BIOS_INFO, "PEIM: InstallPeiMemory(begin=0x%llx "
		       "length=0x%llx) accepted: backing limit 0x%llx, "
		       "arena 0x%llx+0x%llx\n",
		       (unsigned long long)memory_begin,
		       (unsigned long long)memory_length,
		       (unsigned long long)state->memory_ceiling,
		       (unsigned long long)state->dram_base,
		       (unsigned long long)state->dram_size);
		return EFI_SUCCESS;
	}
	why = GRANTLEY_PEI_MEMORY_REFUSED_WINDOW;

refuse:

	state->memory_refused = TRUE;
	state->memory_refusal = why;

	/*
	 * The pinned PEIM does not test this status: 0xFFE60FFD and 0xFFE61513
	 * both fall straight through to ReportStatusCode with the same range,
	 * and 0xFFE6112F then allocates its workspace.  A refused first claim
	 * must therefore leave the allocators with nothing to hand out,
	 * instead of silently serving CAR as if the claim had worked.
	 */
	if (!state->dram_initialized) {
		state->memory_install_failed = TRUE;
		state->dram_base = 0;
		state->dram_size = 0;
		state->dram_free_top = 0;
	}

	printk(BIOS_ERR, "PEIM: InstallPeiMemory(begin=0x%llx length=0x%llx) "
	       "refused: %s; no permanent window or PHIT update was created\n",
	       (unsigned long long)memory_begin,
	       (unsigned long long)memory_length,
	       grantley_pei_memory_refusal_string(why));

#if ENV_TEST
	/* Host tests cannot unwind a PEIM; report the refusal as a status and
	 * let the caller assert it.  Firmware does not return to the blob:
	 * the PEIM ignores this status, so a status alone would not be a gate.
	 */
	return status;
#else
	post_code(0xe7);
	die("Grantley: InstallPeiMemory cannot be honored: the claim is not "
	    "backed by the trained TOLM window (and no HOB/stack/PHIT migration "
	    "exists); refusing to hand the PEIM unvalidated memory\n");
#endif
}

static bool grantley_page_type_valid(EFI_MEMORY_TYPE memory_type)
{
	switch (memory_type) {
	case EfiReservedMemoryType:
	case EfiLoaderCode:
	case EfiLoaderData:
	case EfiBootServicesCode:
	case EfiBootServicesData:
	case EfiRuntimeServicesCode:
	case EfiRuntimeServicesData:
	case EfiACPIReclaimMemory:
	case EfiACPIMemoryNVS:
		return true;
	default:
		return false;
	}
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
	if (!grantley_page_type_valid(memory_type))
		return EFI_INVALID_PARAMETER;
	if (pages > (UINTN)-1 / 4096)
		return EFI_OUT_OF_RESOURCES;

	/* A refused claim must not be papered over by handing out CAR: the PEIM
	 * ignores the status and treats what it gets as permanent. */
	if (state->memory_install_failed)
		return EFI_OUT_OF_RESOURCES;

	/*
	 * Once a PEIM has installed permanent memory the stock core stops using
	 * temporary RAM: 0xFFDA275E carves pages downwards from
	 * FreePhysicalMemoryTop and refuses to cross PhysicalMemoryBegin.  Do
	 * the same here, because the PEIM asks for pages after install and
	 * expects them outside CAR.
	 */
	if (state->dram_initialized) {
		UINT64 top = grantley_page_down(state->dram_free_top);
		UINT64 bytes = (UINT64)pages * 4096;
		UINT64 candidate;

		if (top < state->dram_base || bytes > top - state->dram_base)
			return EFI_OUT_OF_RESOURCES;
		candidate = top - bytes;

		/* The core still runs from CAR and keeps the HOB list and pool
		 * there until it migrates.  Handing those pages out as permanent
		 * memory would corrupt the very structures describing the
		 * allocation, so refuse instead of carving around them. */
		if (grantley_arena_region_excluded(state, candidate, bytes))
			return EFI_OUT_OF_RESOURCES;

		state->dram_free_top = candidate;
		*addr = candidate;
		return EFI_SUCCESS;
	}

	if (state->pool_allocated > state->pool_size)
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

	if (size > (UINTN)-1 - 7)
		return EFI_OUT_OF_RESOURCES;

	/* Same reason as AllocatePages: after a refused claim the PEIM's next
	 * act is AllocatePool (0xFFE6112F) for the arena it believes it owns,
	 * so handing out CAR here would be indistinguishable from success. */
	if (state->memory_install_failed)
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

/*
 * M4 - pin the DRAM window from the bounds the MRC just programmed.
 *
 * InstallPeiMemory is reached only after MrcStatus == 0 (gate at 0xFFE5F109)
 * and after step 64 of the MRC table ran (0xFFE33995 -> 0xFFE33874 ->
 * 0xFFE33741 = WriteTolmTohmCSRs, MemAddrMap.c:3096).  That step is the only
 * writer of the TOLM register (IIO_VTD 0x090040D0) and of
 * host->var.mem.lowMemBase/lowMemSize in the whole blob, so at this moment -
 * and not before entry - PCI 0:5.0 0xd0 holds the trained low-DRAM limit.
 * romstage reads the same register after the PEIM and grantley_pei_memory_map()
 * decodes it with the same rule; if the two disagreed the map check would fail,
 * so the encoding is reused here rather than given a new mask.
 *
 * TSEG is programmed by the PEIM itself, in the GetMemoryMap() call that
 * InstallEfiMemory() makes before InstallPeiMemory(): for every present socket
 * it read-modify-writes TSEG_N0/N1 (IIO 0x090040A8/AC) from
 * PlatformInfo->MemData.MemTsegSize (offset 0x85, which
 * grantley_init_platform_info_hob() sets to CONFIG_SMM_TSEG_SIZE).  The same
 * call trims the low main-memory descriptor down to the TSEG base, so the
 * descriptor this claim is cut from ends there and the claim cannot reach
 * SMRAM.  The arena cap below keeps that true even if a map ever offered a
 * descriptor ending on TOLM: no page at or above arena_ceiling is handed out.
 * CONFIG_SMM_TSEG_SIZE is the whole TSEG and already contains the IED at its
 * top (tseg_region.c), so it is reserved once: adding CONFIG_IED_REGION_SIZE
 * would take the IED out twice.
 *
 * Everything this function reads is a device register.  A zero or all-ones
 * readback, a limit above the ECAM window romstage routes, or no room for the
 * SMRAM reservation is a refusal - never a guessed window.
 */
static EFI_STATUS grantley_pei_pin_window_from_hardware(struct grantley_uncore_state *state)
{
	const uint64_t block = 64ULL * 1024ULL * 1024ULL;
	const uint64_t tseg = CONFIG_SMM_TSEG_SIZE;
	uint32_t raw;
	uint64_t tolm_end;

	if (!state)
		return EFI_INVALID_PARAMETER;
	if (state->dram_initialized)
		return EFI_ALREADY_STARTED;

	raw = route_read32(PCI_DEV(0, 5, 0), 0xd0, 0);
	/* TOLM[25:0] is reserved: a dead config cycle reads back all ones. */
	if (!raw || (raw & 0x03ffffffUL))
		return EFI_DEVICE_ERROR;
	tolm_end = (uint64_t)(raw & 0xfc000000UL) + block;
	if (tolm_end < block || tolm_end > GRANTLEY_PEI_MEMORY_LIMIT)
		return EFI_DEVICE_ERROR;
	/* The same clamp the map builder applies: DRAM decoded through the ECAM
	 * window would make config accesses land in RAM. */
	if (tolm_end > GRANTLEY_ECAM_BASE)
		return EFI_DEVICE_ERROR;
	/* No room for the SMM reservation at the top of low DRAM. */
	if (tolm_end <= tseg)
		return EFI_DEVICE_ERROR;

	if (state->exclusion_known) {
		/* A validated SMRAM window is already programmed; keep it and pin
		 * only the bounds.  The claim is then still checked against it. */
		state->memory_ceiling = tolm_end;
		state->arena_ceiling = tolm_end - tseg;
		return EFI_SUCCESS;
	}

	/* Pin the bounds.  This port has not pinned an exclusion, and the PEIM
	 * has just programmed the TSEG at the top of this same window: what
	 * keeps allocations out of SMRAM is the arena cap below plus
	 * GetMemoryMap()'s trim of the main descriptor, not an exclusion pair
	 * that could be read here. */
	EFI_STATUS status = grantley_pei_set_memory_window(state, tolm_end, 0, 0, TRUE);
	if (status != EFI_SUCCESS)
		return status;
	state->arena_ceiling = tolm_end - tseg;
	return EFI_SUCCESS;
}

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
		if ((route_read32(ubox, 0xd0, 0) & 0x8000ffff) != GRANTLEY_CPUBUSNO ||
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
		    (route_read32(pcu, 0x40, base) & 0x8000ffff) != GRANTLEY_CPUBUSNO ||
		    (route_read32(iio, 0x108, base) & 0x1ffff) != 0x1ff00)
			return EFI_DEVICE_ERROR;
		state->mmcfg_base = base;
		state->mmcfg_enabled = TRUE;
		return EFI_SUCCESS;
	}
	if ((route_read32(reset_ubox, 0, 0) & 0xffff) != 0x8086)
		return EFI_DEVICE_ERROR;

	/* CPUBUSNO changes the UBOX and SAD addresses immediately. */
	route_write32(reset_ubox, 0xd0, 0, GRANTLEY_CPUBUSNO);
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
	if ((route_read32(pcu, 0x40, base) & 0x8000ffff) != GRANTLEY_CPUBUSNO ||
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

/* One access of the requested width: an MMCONFIG address when routed,
 * otherwise CF8 with the encoded device. */
static EFI_STATUS pci_cfg_load(uintptr_t mmio, pci_devfn_t dev, UINT32 reg,
			       EFI_PEI_PCI_CFG_PPI_WIDTH width, void *buffer)
{
	switch (width) {
	case EfiPeiPciCfgWidthUint8:
		*(UINT8 *)buffer = mmio ? read8((void *)mmio)
					: pci_s_read_config8(dev, reg);
		return EFI_SUCCESS;
	case EfiPeiPciCfgWidthUint16:
		*(UINT16 *)buffer = mmio ? read16((void *)mmio)
					 : pci_s_read_config16(dev, reg);
		return EFI_SUCCESS;
	case EfiPeiPciCfgWidthUint32:
		*(UINT32 *)buffer = mmio ? read32((void *)mmio)
					 : pci_s_read_config32(dev, reg);
		return EFI_SUCCESS;
	default:
		return EFI_INVALID_PARAMETER;
	}
}

static EFI_STATUS pci_cfg_store(uintptr_t mmio, pci_devfn_t dev, UINT32 reg,
				EFI_PEI_PCI_CFG_PPI_WIDTH width, void *buffer)
{
	switch (width) {
	case EfiPeiPciCfgWidthUint8:
		mmio ? write8((void *)mmio, *(UINT8 *)buffer)
		     : pci_s_write_config8(dev, reg, *(UINT8 *)buffer);
		return EFI_SUCCESS;
	case EfiPeiPciCfgWidthUint16:
		mmio ? write16((void *)mmio, *(UINT16 *)buffer)
		     : pci_s_write_config16(dev, reg, *(UINT16 *)buffer);
		return EFI_SUCCESS;
	case EfiPeiPciCfgWidthUint32:
		mmio ? write32((void *)mmio, *(UINT32 *)buffer)
		     : pci_s_write_config32(dev, reg, *(UINT32 *)buffer);
		return EFI_SUCCESS;
	default:
		return EFI_INVALID_PARAMETER;
	}
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

	if (state->mmcfg_enabled)
		return pci_cfg_load(pci_cfg_mmio(state->mmcfg_base, bus, dev,
						  func, reg), 0, 0, width, buffer);

	if (reg >= 256)
		return EFI_UNSUPPORTED;

	return pci_cfg_load(0, pdev, reg, width, buffer);
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

	if (state->mmcfg_enabled)
		return pci_cfg_store(pci_cfg_mmio(state->mmcfg_base, bus, dev,
						   func, reg), 0, 0, width, buffer);

	if (reg >= 256)
		return EFI_UNSUPPORTED;

	return pci_cfg_store(0, pdev, reg, width, buffer);
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

	/* Only DdrFreqLimit and EccSupport have byte-proven offsets in the stock
	 * SYSTEM_CONFIGURATION (0x1248 and 0x11FF).  The timing/command-rate
	 * fields are not at 0x70..0x75 in the real structure and their true
	 * offsets are unproven, so they are refused instead of written blindly.
	 */
	if (tcl || trcd || trp || tras || command_rate)
		printk(BIOS_WARNING, "IntelSetup: timing overrides have no verified "
		       "SYSTEM_CONFIGURATION offset and are NOT applied\n");
}

const uint8_t *grantley_intelsetup_stock(void)
{
	const struct grantley_native_contract *contract = grantley_native_contract();

	return contract ? contract->intelsetup_payload : (const uint8_t *)0;
}

/* Fail closed: a board must call grantley_intelsetup_allow_stock(true) to
 * serve its own factory variable.  The release/native-off paths never do.
 */
static bool intelsetup_stock_allowed;

void grantley_intelsetup_allow_stock(bool enable)
{
	intelsetup_stock_allowed = enable;
}

/* DdrFreqLimit enum from MemHost.h; the values are the ones the stock setup
 * uses (0 = AUTO, 5 = DDR1333 ... 13 = DDR2400).
 */
/*
 * L"IntelSetup" (SYSTEM_CONFIGURATION) for UncoreInitPeim.
 *
 * The variable's size and bytes belong to the board contract: 6091 bytes on
 * QD4, 6090 on X99D4M4.  The two field offsets (DdrFreqLimit 0x1248,
 * EccSupport 0x11FF) are proven for QD4; on X99D4M4 only the payload identity
 * is proven (that board's own NVAR bytes), its field semantics are not, so
 * its contract sets intelsetup_offsets_proven = false and the payload is
 * served verbatim.  Nothing else is interpreted on either board.
 * Three cases:
 *
 *  - live variable in the attached EFI store: returned verbatim, but only at
 *    the contract's exact size (a short policy buffer is what the PEIM would
 *    trust blindly);
 *  - no live variable: the board's own stock default payload, and only when
 *    that board's Kconfig option compiled it in; where the contract says the
 *    offsets are proven, only EccSupport (0x11FF) can be overridden and
 *    DdrFreqLimit (0x1248) keeps the stock AUTO because the per-speed enum is
 *    not byte-proven; where they are not proven, no byte is touched;
 *  - anything else: EFI_NOT_FOUND / EFI_BUFFER_TOO_SMALL, never a fabricated
 *    size or a zero-filled buffer that silently means ECC_DIS, and never
 *    another board's bytes.
 */
static EFI_STATUS grantley_intelsetup_variable(struct grantley_uncore_state *state,
					       void *data, UINTN *data_size,
					       UINT32 *attributes, enum cb_err err,
					       uint32_t live_size)
{
	const struct grantley_native_contract *contract = grantley_native_contract();
	const uint32_t policy_size = contract ? contract->intelsetup_size : 0;
	const uint8_t *policy = contract ? contract->intelsetup_payload : (const uint8_t *)0;

	if (err == CB_EFI_BUFFER_TOO_SMALL) {
		if (!policy_size)
			return EFI_NOT_FOUND;
		*data_size = policy_size;
		return EFI_BUFFER_TOO_SMALL;
	}

	if (err == CB_SUCCESS) {
		if (!policy_size || live_size != policy_size) {
			printk(BIOS_ERR, "IntelSetup: live variable is 0x%x bytes, "
			       "expected 0x%x; refusing to hand it to the PEIM\n",
			       live_size, policy_size);
			return EFI_NOT_FOUND;
		}
		printk(BIOS_INFO, "IntelSetup: %u bytes from the live EFI variable\n",
		       live_size);
		*data_size = policy_size;
		if (attributes)
			*attributes = 0;
		return EFI_SUCCESS;
	}

	/* Platform defaults: the exact stock payload, not a synthetic struct.
	 * The payload is board-specific; when this board did not compile its own
	 * payload in, answer EFI_NOT_FOUND and let the PEIM use its own
	 * ZeroMem + IIO defaults.
	 */
	if (!intelsetup_stock_allowed || !policy || !policy_size)
		return EFI_NOT_FOUND;

	memcpy(data, policy, policy_size);

	uint8_t *setup = data;
	if (contract->intelsetup_offsets_proven) {
		/* DdrFreqLimit's offset is proven (0x1248, stock value 0 = AUTO)
		 * but the enum for a *specific* speed is not: the stock images
		 * only ever contain 0, so writing 0x05/0x07/0x0B/... for a
		 * requested speed would be an unverified enum guess.  Refuse it
		 * and keep AUTO.
		 */
		if (state->max_dram_speed_mts)
			printk(BIOS_WARNING, "IntelSetup: DdrFreqLimit=%u MT/s has no "
			       "byte-proven SYSTEM_CONFIGURATION enum; keeping AUTO (0)\n",
			       state->max_dram_speed_mts);
		if (state->ecc_en)
			setup[GRANTLEY_INTELSETUP_OFF_ECC_SUPPORT] =
				GRANTLEY_INTELSETUP_ECC_ENABLED;
	}

	printk(BIOS_INFO, "IntelSetup: %u stock default bytes, "
	       "DdrFreqLimit=%u EccSupport=%u\n", policy_size,
	       setup[GRANTLEY_INTELSETUP_OFF_DDR_FREQ_LIMIT],
	       setup[GRANTLEY_INTELSETUP_OFF_ECC_SUPPORT]);

	*data_size = policy_size;
	if (attributes)
		*attributes = 0;
	return EFI_SUCCESS;
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

	bool is_setup = strcmp(ascii_name, "IntelSetup") == 0;

	/* Stock GetVariable matches name AND vendor GUID.  Do this before the
	 * size query: answering BUFFER_TOO_SMALL for a foreign GUID would leak
	 * the platform's IntelSetup size and, worse, make a caller that then
	 * retries with a large buffer receive this board's defaults.
	 */
	static const EFI_GUID setup_vendor_guid = GRANTLEY_INTELSETUP_VENDOR_GUID;
	if (is_setup && memcmp(vendor_guid, &setup_vendor_guid,
			       sizeof(setup_vendor_guid)) != 0)
		return EFI_NOT_FOUND;

	/* Size discovery for SYSTEM_CONFIGURATION comes first: reporting a short
	 * buffer with EFI_SUCCESS makes the PEIM trust a truncated policy.
	 * Data == NULL is the standard size query too; it must report the
	 * required size rather than reading or filling a buffer.
	 *
	 * The size is the board contract's, because the vendor PEIMs do not all
	 * ask for the same one (QD4 0x17CB, X99D4M4 0x17CA).  When the board
	 * has not opted in to its own payload and no variable store is
	 * attached, the variable does not exist at all: answer EFI_NOT_FOUND
	 * instead of advertising a size.
	 */
	const struct grantley_native_contract *contract = grantley_native_contract();
	const uint32_t setup_size = contract ? contract->intelsetup_size : 0;

	if (is_setup && !intelsetup_stock_allowed && !state->var_store_rdev)
		return EFI_NOT_FOUND;
	if (is_setup && (!setup_size || *data_size < setup_size || !data)) {
		if (!setup_size)
			return EFI_NOT_FOUND;
		*data_size = setup_size;
		return EFI_BUFFER_TOO_SMALL;
	}

	enum cb_err err = CB_ERR;
	uint32_t size = (uint32_t)*data_size;
	if (state->var_store_rdev)
		err = efi_fv_get_option(state->var_store_rdev,
					vendor_guid, ascii_name,
					data, &size);

	if (is_setup)
		return grantley_intelsetup_variable(state, data, data_size,
						    attributes, err, size);

	if (err == CB_EFI_BUFFER_TOO_SMALL)
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

/* GUIDs from the UncoreInitPeim PEI_DEPEX, in the order the expression pushes
 * them (see pei_services.h).  Five are provided by this runner; 057a449a and
 * 01f34d25 are not, and no coreboot code publishes them.
 */
const EFI_GUID grantley_uncore_depex[GRANTLEY_UNCORE_DEPEX_COUNT] = {
	/* 1 gEfiPeiCpuIoPpiInstalledGuid (MdePkg/Include/Ppi/CpuIo.h) */
	{ 0xe6af1f7b, 0xfc3f, 0x46da,
	  { 0xa8, 0x28, 0xa3, 0xb4, 0x57, 0xa4, 0x42, 0x82 } },
	/* 2 gEfiPciCfg2PpiGuid - "EfiPciCfg2" in the stock PpiTableIA32.c */
	{ 0x057a449a, 0x1fdc, 0x4c06,
	  { 0xbf, 0xc9, 0xf5, 0x3f, 0x6a, 0x99, 0xbb, 0x92 } },
	/* 4 gEfiPeiReadOnlyVariable2PpiGuid (MdePkg/Include/Ppi/ReadOnlyVariable2.h) */
	{ 0x2ab86ef5, 0xecb5, 0x4134,
	  { 0xb5, 0x56, 0x38, 0x54, 0xca, 0x1f, 0xe1, 0xb4 } },
	/* 5 gPeiBaseMemoryTestPpiGuid (GrantleySocketPkg/Include/Ppi/BaseMemoryTest.h) */
	{ 0xb6ec423c, 0x21d2, 0x490d,
	  { 0x85, 0xc6, 0xdd, 0x58, 0x64, 0xea, 0xa6, 0x74 } },
	/* 6 gPeiPlatformMemorySizePpiGuid
	 *   (GrantleySocketPkg/Include/Ppi/PlatformMemorySize.h)
	 */
	{ 0x9a7ef41e, 0xc140, 0x4bd1,
	  { 0xb8, 0x84, 0x1e, 0x11, 0x24, 0x0b, 0x4c, 0xe6 } },
	/* 7 gEfiPlatformInfoGuid (platform info PEIM) */
	{ 0x1e2acc41, 0xe26a, 0x483d,
	  { 0xaf, 0xc7, 0xa0, 0x56, 0xc3, 0x4e, 0x08, 0x7b } },
	/* 7 (binary order: the DEPEX pushes the PCD PPI last)
	 * gEfiPeiPcdPpiGuid - "EfiPeiPcd" in the stock PpiTableIA32.c.  This is
	 * the one DEPEX PPI coreboot cannot provide (no PCD database).
	 */
	{ 0x01f34d25, 0x4de2, 0x23ad,
	  { 0x3f, 0xf3, 0x36, 0x35, 0x3f, 0xf3, 0x23, 0xf1 } },
};

/* PCD provider interfaces, both installed by this runner (the stock PcdPeim
 * installs both as well): the native value-returning PCD_PPI the module calls
 * at entry, and the PI 1.2 EFI_PEI_PCD_PPI its PEI_DEPEX names.
 */
#if CONFIG(GRANTLEY_UNCORE_NATIVE_ENTRY)
static EFI_GUID g_pcd_guid = {
	0x06e81c58, 0x4ad7, 0x44bc,
	{ 0x83, 0x90, 0xf1, 0x02, 0x65, 0xf7, 0x24, 0x80 }
};
static EFI_GUID g_efi_pei_pcd_guid = {
	0x01f34d25, 0x4de2, 0x23ad,
	{ 0x3f, 0xf3, 0x36, 0x35, 0x3f, 0xf3, 0x23, 0xf1 }
};
#endif

EFI_STATUS grantley_uncore_deps_check(const struct _EFI_PEI_SERVICES **services,
				      EFI_GUID *missing)
{
	if (!services)
		return EFI_INVALID_PARAMETER;

	for (size_t i = 0; i < GRANTLEY_UNCORE_DEPEX_COUNT; i++) {
		void *ppi = (void *)0;

		if (grantley_locate_ppi(services, &grantley_uncore_depex[i], 0,
					(void *)0, &ppi) != EFI_SUCCESS) {
			if (missing)
				*missing = grantley_uncore_depex[i];
			return EFI_NOT_STARTED;
		}
	}

	return EFI_SUCCESS;
}

EFI_STATUS grantley_run_uncore_peim(const void *pe_base, size_t pe_size,
				    struct grantley_uncore_state *state)
{
	if (!pe_base || !state)
		return EFI_INVALID_PARAMETER;

	state->stop_reason = GRANTLEY_STOP_NONE;

	uintptr_t exec_addr = (uintptr_t)pe_base;
	EFI_STATUS status = EFI_SUCCESS;
	grantley_peim_entry_t entry_point;

#if CONFIG(GRANTLEY_UNCORE_NATIVE_ENTRY)
	struct grantley_peim_image image;
	status = grantley_peim_verify(pe_base, pe_size, &image);
	if (status != EFI_SUCCESS) {
		state->stop_reason = GRANTLEY_STOP_IMAGE;
		return status;
	}

	/* Authorization, not merely structure: the bytes must be the pinned
	 * vendor blob.  This runs before any PPI lookup, PCD getter or MMIO so
	 * a valid PE with one changed byte cannot reach a single call.
	 */
	if (!grantley_peim_authorized(pe_base, pe_size)) {
		uint8_t digest[GRANTLEY_PEIM_SHA256_SIZE];
		if (grantley_peim_sha256(pe_base, pe_size, digest)) {
			printk(BIOS_ERR, "PEIM: sha256 mismatch, refusing to "
			       "execute: %02x%02x%02x%02x%02x%02x%02x%02x"
			       "%02x%02x%02x%02x%02x%02x%02x%02x"
			       "%02x%02x%02x%02x%02x%02x%02x%02x"
			       "%02x%02x%02x%02x%02x%02x%02x%02x\n",
			       digest[0], digest[1], digest[2], digest[3],
			       digest[4], digest[5], digest[6], digest[7],
			       digest[8], digest[9], digest[10], digest[11],
			       digest[12], digest[13], digest[14], digest[15],
			       digest[16], digest[17], digest[18], digest[19],
			       digest[20], digest[21], digest[22], digest[23],
			       digest[24], digest[25], digest[26], digest[27],
			       digest[28], digest[29], digest[30], digest[31]);
		}
		state->stop_reason = GRANTLEY_STOP_HASH;
		return EFI_SECURITY_VIOLATION;
	}

	/* The contract names the one blob this board may execute, including
	 * the ImageBase at which the stock firmware maps it.  A digest match
	 * for a different board's build is impossible, so this is a second,
	 * explicit identity check: it turns a silent wrong-board image into a
	 * named refusal before any PPI lookup or MMIO.
	 */
	const struct grantley_native_contract *contract = grantley_native_contract();
	if (!contract) {
		printk(BIOS_ERR, "PEIM: no native contract is configured for this "
		       "board, not dispatching\n");
		state->stop_reason = GRANTLEY_STOP_CONTRACT;
		return EFI_UNSUPPORTED;
	}
#if !ENV_TEST
	/* The host test cannot map an image at 0xFFDCxxxx.  On target this is
	 * the exact physical ImageBase the pinned blob carries; the audit in
	 * util/grantley/pei_native_entry_audit.py re-checks the real ELF.
	 */
	if (image.image_base != contract->image_base) {
		printk(BIOS_ERR, "PEIM: image base 0x%x is not the contract's 0x%x "
		       "for %s\n", image.image_base, contract->image_base,
		       contract->board);
		state->stop_reason = GRANTLEY_STOP_CONTRACT;
		return EFI_UNSUPPORTED;
	}
#endif
	const char *constant_fault =
		grantley_native_check_blob_ecam(pe_base, pe_size);
	if (constant_fault) {
		printk(BIOS_ERR, "PEIM: %s (contract of %s)\n", constant_fault,
		       contract->board);
		state->stop_reason = GRANTLEY_STOP_CONTRACT;
		return EFI_UNSUPPORTED;
	}

	/* A PI core would not dispatch this PEIM before its DEPEX is satisfied;
	 * calling the entry point anyway feeds it an environment the vendor
	 * declared insufficient.
	 */
	EFI_GUID missing;
	status = grantley_uncore_deps_check(&state->services_ptr, &missing);
	if (status != EFI_SUCCESS) {
		printk(BIOS_ERR,
		       "PEIM: dependency %08x-%04x-%04x missing, not dispatching\n",
		       missing.Data1, missing.Data2, missing.Data3);
		state->stop_reason = GRANTLEY_STOP_DEPEX;
		return status;
	}

	/* The module calls LocatePpi(06E81C58) at entry and does not check the
	 * status; the provider must be present and answer with a value.  It also
	 * must be this provider, not a placeholder installed by someone else.
	 */
	void *pcd_ppi = (void *)0;
	void *pcd_ex_ppi = (void *)0;
	if (grantley_locate_ppi(&state->services_ptr, &g_pcd_guid, 0, (void *)0,
				&pcd_ppi) != EFI_SUCCESS ||
	    pcd_ppi != (void *)&grantley_pcd_ppi ||
	    grantley_locate_ppi(&state->services_ptr, &g_efi_pei_pcd_guid, 0,
				(void *)0, &pcd_ex_ppi) != EFI_SUCCESS) {
		printk(BIOS_ERR, "PEIM: PCD provider PPI missing or not the runner's "
		       "provider, not dispatching\n");
		state->stop_reason = GRANTLEY_STOP_PCD;
		return EFI_UNSUPPORTED;
	}

	/* Fail closed BEFORE the entry point: a value-returning getter cannot
	 * report "unknown", so the decision has to happen here.  Token 1 is the
	 * CSR MMIO base the module dereferences unconditionally.
	 */
	const char *block = grantley_pcd_blocking_reason();
	if (block) {
		printk(BIOS_ERR, "PEIM: not entering: %s\n", block);
		printk(BIOS_ERR, "PEIM: PCD token 1 value=0x%llx fault=%s "
		       "(token %u)\n",
		       (unsigned long long)grantley_pcd_mmcfg_base(),
		       grantley_pcd_fault_string(grantley_pcd_fault()),
		       grantley_pcd_fault_token());
		state->stop_reason = GRANTLEY_STOP_PCD;
		return EFI_UNSUPPORTED;
	}

	/* EFI ImageBase is a 32-bit PE field.  On the IA-32 target this is the
	 * real in-place contract; the 64-bit host test binary cannot be mapped
	 * below 4 GiB, so there the low 32 bits are compared.
	 */
	if ((uint32_t)exec_addr != image.image_base) {
		printk(BIOS_ERR, "PEIM: image base 0x%x, mapped at 0x%lx; this port "
		       "executes the image in place and has no loader\n",
		       image.image_base, (unsigned long)exec_addr);
		state->stop_reason = GRANTLEY_STOP_IMAGE;
		return EFI_UNSUPPORTED;
	}
	if (!image.xip_compatible) {
		printk(BIOS_ERR, "PEIM: image sections are not laid out for in-place "
		       "execution\n");
		state->stop_reason = GRANTLEY_STOP_IMAGE;
		return EFI_UNSUPPORTED;
	}

	entry_point = (grantley_peim_entry_t)(exec_addr + image.entry_rva);
#elif CONFIG(GRANTLEY_UNCORE_REQUIRE_NATIVE_CONTRACT)
	/* This board declares that the packaged PEIM may only be executed
	 * through its native contract, and no contract is selected in this
	 * build.  Refuse before the parse: the pre-native path below would
	 * call the entry point with no PCD provider, no PEI_DEPEX check and no
	 * image pin, and the vendor module does not test LocatePpi's status
	 * before its first Get64(1).  A named refusal is the only defined
	 * outcome; it is not a degraded entry.
	 */
	printk(BIOS_ERR, "PEIM: this board requires its native contract and "
	       "none is selected, not dispatching\n");
	(void)exec_addr;	/* not inspected: the refusal precedes the image */
	state->stop_reason = GRANTLEY_STOP_CONTRACT;
	return EFI_UNSUPPORTED;
#else
	/* Pre-native runtime path: structure and ImageBase only.  The QD4 SHA
	 * pin, DEPEX set and PCD token-1 gate are compiled out, so builds that
	 * keep this path on purpose keep their previous behaviour instead of
	 * being rejected as a "security violation" by a foreign board's
	 * digest.  Boards that must not enter without a contract select
	 * GRANTLEY_UNCORE_REQUIRE_NATIVE_CONTRACT and are handled above.
	 */
	const uint8_t *bytes = pe_base;
	if (pe_size < 0x40)
		return EFI_UNSUPPORTED;
	if (bytes[0] != 'M' || bytes[1] != 'Z')
		return EFI_UNSUPPORTED;
	const uint32_t e_lfanew = (uint32_t)bytes[0x3c] |
		((uint32_t)bytes[0x3d] << 8) | ((uint32_t)bytes[0x3e] << 16) |
		((uint32_t)bytes[0x3f] << 24);
	if (e_lfanew > pe_size || pe_size - e_lfanew < 0x40)
		return EFI_UNSUPPORTED;
	const uint8_t *pe_hdr = bytes + e_lfanew;
	if (pe_hdr[0] != 'P' || pe_hdr[1] != 'E' ||
	    pe_hdr[2] != 0 || pe_hdr[3] != 0)
		return EFI_UNSUPPORTED;
	const uint32_t entry_rva = (uint32_t)pe_hdr[0x28] |
		((uint32_t)pe_hdr[0x29] << 8) | ((uint32_t)pe_hdr[0x2a] << 16) |
		((uint32_t)pe_hdr[0x2b] << 24);
	const uint32_t image_base = (uint32_t)pe_hdr[0x34] |
		((uint32_t)pe_hdr[0x35] << 8) | ((uint32_t)pe_hdr[0x36] << 16) |
		((uint32_t)pe_hdr[0x37] << 24);
	if (exec_addr != (uintptr_t)image_base)
		return EFI_UNSUPPORTED;
	entry_point = (grantley_peim_entry_t)(uintptr_t)
		(bytes + (intptr_t)(int32_t)entry_rva);
	(void)status;
#endif

	/*
	 * UncoreInitPeim allocates ~440 KB on the stack at entry
	 * (e.g. sub $0x6db38, %esp). Switch to a dedicated stack at
	 * top of CAR (0xfe1fffc0), providing >800 KB of headroom,
	 * then restore coreboot's esp upon return.
	 */
	uintptr_t car_stack_top =
		CONFIG_DCACHE_RAM_BASE + CONFIG_DCACHE_RAM_SIZE - 64;
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

/* PEI_PLATFORM_MEMORY_SIZE_PPI (GrantleySocketPkg/Include/Ppi/PlatformMemorySize.h)
 * is "the minimum platform memory size in order to successfully pass control
 * into DXE", with MemorySize as IN OUT.  The value itself is a platform policy
 * number that this port has not verified against stock; it must at least never
 * shrink a caller's larger request.  Stock 0x9A7EF41E is reported to overwrite
 * the requested size with a platform total (0x12C00000 plus MTI pages); that
 * value depends on the PlatformInfo producer/DRAM state and is intentionally
 * not guessed here.
 */
#define GRANTLEY_PEI_MIN_PERMANENT_MEMORY (64ULL * 1024 * 1024)

static EFI_STATUS EFIAPI grantley_platform_memory_size(
	const struct _EFI_PEI_SERVICES **services, void *this, UINT64 *size)
{
	if (!size)
		return EFI_INVALID_PARAMETER;
	if (*size < GRANTLEY_PEI_MIN_PERMANENT_MEMORY)
		*size = GRANTLEY_PEI_MIN_PERMANENT_MEMORY;
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
#if CONFIG(GRANTLEY_UNCORE_NATIVE_ENTRY)
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_mem_size_guid,
	  (void *)&g_memory_size },
	/* Both PCD interfaces are real value-returning tables; the provider
	 * refuses to enter the PEIM until token 1 has a byte-proven value.
	 */
	{ EFI_PEI_PPI_DESCRIPTOR_PPI, &g_pcd_guid, (void *)&grantley_pcd_ppi },
	{ EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
	  &g_efi_pei_pcd_guid, (void *)&grantley_efi_pei_pcd_ppi },
#else
	/* Pre-native release/diag path: no PCD provider and no QD4 payload. */
	{ EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
	  &g_mem_size_guid, (void *)&g_memory_size },
#endif
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
	state->stop_reason = GRANTLEY_STOP_NONE;
#if CONFIG(GRANTLEY_UNCORE_NATIVE_ENTRY)
	/* A new services instance starts with an unconfigured PCD provider; the
	 * platform supplies the token value (if it has a proven one) afterwards.
	 */
	grantley_pcd_reset();
#endif
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
