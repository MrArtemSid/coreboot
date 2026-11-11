/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_PEI_SERVICES_H
#define NORTHBRIDGE_INTEL_GRANTLEY_PEI_SERVICES_H

#include <stddef.h>
#include <stdint.h>
#include <PiPei.h>
#include <Ppi/Pcd.h>
#include <Ppi/ReadOnlyVariable2.h>

#define MAX_GRANTLEY_PPI_DESCRIPTORS 16

/* Reserved stack budget, including margin over the observed ~440 KiB use. */
#define GRANTLEY_PEIM_STACK_SIZE (768 * 1024)
#define GRANTLEY_PEIM_STACK_HEADROOM 64

/* serialDebugMsgLvl; 1 is SDBG_MIN. */
#define GRANTLEY_RC_SERIAL_SDBG_MIN	1

/* AttemptFastBootCold in the IntelSetup memory policy: 0 off, 1 on, 2 auto. */
#define GRANTLEY_INTELSETUP_FAST_BOOT_COLD	0x1209

/* The module exports its trained configuration as chunks of at most 0xF000. */
#define GRANTLEY_CACHE_CHUNK		0xF000
#define GRANTLEY_CACHE_MAX_CHUNKS	8

/* Vendor GUID of the IntelSetup variable. */
#define GRANTLEY_INTELSETUP_VENDOR_GUID { 0xec87d643, 0xeba4, 0x4bb5, \
	{ 0xa1, 0xe5, 0x3f, 0x3e, 0x36, 0xb2, 0x0d, 0xa9 } }

/* InstallPeiMemory() must stay between 1 MiB and 4 GiB. */
#define GRANTLEY_PEI_MEMORY_FLOOR 0x100000ULL
#define GRANTLEY_PEI_MEMORY_LIMIT 0x100000000ULL

struct grantley_uncore_state {
	struct _EFI_PEI_SERVICES services;
	const struct _EFI_PEI_SERVICES *services_ptr;

	/* PPI Registry */
	const EFI_PEI_PPI_DESCRIPTOR *ppi_descriptors[MAX_GRANTLEY_PPI_DESCRIPTORS];
	UINTN ppi_count;
	UINT8 *platform_info;

	/* IntelSetup default variable served to the PEIM, or NULL. */
	const void *setup;
	size_t setup_size;

	uintptr_t saved_esp;

	/* HOB storage */
	void *hob_list;
	UINTN hob_size;
	UINTN hob_allocated;

	/* Pool memory */
	UINT8 *pool_base;
	UINTN pool_size;
	UINTN pool_allocated;

	/* Boot Mode */
	EFI_BOOT_MODE boot_mode;

	/* Region the module passed to InstallPeiMemory() */
	EFI_PHYSICAL_ADDRESS dram_base;
	UINT64 dram_size;
	BOOLEAN dram_initialized;

	/* Training-cache capture and restore (CACHE_MRC_SETTINGS). */
	uint8_t *cache_scratch;		/* DRAM scratch for oversized HOBs */
	UINTN cache_scratch_cap;
	UINTN cache_scratch_used;
	struct {
		void *hob;
		UINTN payload;
	} cache_chunk[GRANTLEY_CACHE_MAX_CHUNKS];
	UINTN cache_chunk_count;
	const uint8_t *cache_data;	/* restored cache, served on read */
	size_t cache_total;
	bool cache_loaded;
	UINTN cache_pool_mark;		/* pool slot shared by the read parts */
};

/* ME UMA interface the memory initializer uses to hand off the ME UMA. */
extern const EFI_PEI_PPI_DESCRIPTOR grantley_me_uma_ppi_desc;

/* Serve this payload as the IntelSetup variable; NULL means none. */
void grantley_pei_set_intelsetup(struct grantley_uncore_state *state,
				 const void *data, size_t size);

/* Update the existing PlatformInfo HOB payload before invoking the PEIM. */
EFI_STATUS grantley_pei_set_board_identity(struct grantley_uncore_state *state,
	uint8_t board_id, uint32_t policy_revision);

struct grantley_memory_map;
EFI_STATUS grantley_pei_memory_map(const struct grantley_uncore_state *state,
	uint32_t tolm, uint32_t tohm_lo, uint32_t tohm_hi,
	struct grantley_memory_map *map);

/* Copy DIMM information to CBMEM while the PEIM's HOB arena is live. */
void grantley_save_dimm_info(const struct grantley_uncore_state *state);

/* Execute UncoreInitPeim in place at its PE ImageBase. */
EFI_STATUS grantley_run_uncore_peim(const void *pe_base, size_t pe_size,
				    struct grantley_uncore_state *state,
				    uintptr_t stack_top);

/* The state and both arenas must stay live until the module returns. */
EFI_STATUS grantley_pei_services_init(struct grantley_uncore_state *state,
				    void *pool_buf, UINTN pool_size,
				    void *hob_buf, UINTN hob_size);

/* Training cache (training_cache.c). */
bool grantley_training_cache_is_export(const struct grantley_uncore_state *state,
				       UINT16 type, UINT16 length);
bool grantley_training_cache_redirect(struct grantley_uncore_state *state,
				      UINT16 length, UINT16 aligned_length,
				      void **hob);
void grantley_training_cache_save(struct grantley_uncore_state *state);
EFI_STATUS grantley_training_cache_get(struct grantley_uncore_state *state,
				       const CHAR16 *name, const EFI_GUID *guid,
				       UINTN *data_size, void *data);

#endif /* NORTHBRIDGE_INTEL_GRANTLEY_PEI_SERVICES_H */
