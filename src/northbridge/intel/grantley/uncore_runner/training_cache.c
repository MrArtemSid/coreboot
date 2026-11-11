/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <commonlib/endian.h>
#include <mrc_cache.h>
#include <stdint.h>
#include <string.h>
#include <commonlib/helpers.h>
#undef NULL
#include "pei_services.h"

/*
 * UncoreInitPeim exports its trained memory configuration as raw GUID HOB
 * chunks and, on a later boot, reads it back from EFI variables
 * "MemoryConfig0..N" in EFI-compressed form. coreboot keeps the raw export in
 * RW_MRC_CACHE and serves it back verbatim so the module takes its fast path.
 *
 * The module validates the restored data itself (CRC16, PPIN, DIMM identity,
 * boot date); corrupt or stale data simply falls back to full training.
 */

#define GRANTLEY_MRC_CACHE_VERSION	1

/* Export HOB GUID (1de25879-6e2a-4d72-a768-288ccb9fa719). */
static const EFI_GUID grantley_export_guid = {
	0x1de25879, 0x6e2a, 0x4d72,
	{ 0xa7, 0x68, 0x28, 0x8c, 0xcb, 0x9f, 0xa7, 0x19 }
};

/* "MemoryConfigN" variable GUID (80dbd530-b74c-4f11-8c03-418665532831). */
static const EFI_GUID grantley_memcfg_guid = {
	0x80dbd530, 0xb74c, 0x4f11,
	{ 0x8c, 0x03, 0x41, 0x86, 0x65, 0x53, 0x28, 0x31 }
};

/*
 * The export is one uninterrupted run of GUID HOBs: full 0xF000-byte chunks
 * followed by one shorter tail. CreateHob() only sees the length (the module
 * writes the GUID afterwards), so recognise the run by its shape.
 */
bool grantley_training_cache_is_export(const struct grantley_uncore_state *state,
				       UINT16 type, UINT16 length)
{
	if (!state->cache_scratch || type != EFI_HOB_TYPE_GUID_EXTENSION ||
	    length <= sizeof(EFI_HOB_GUID_TYPE))
		return false;

	UINTN payload = length - sizeof(EFI_HOB_GUID_TYPE);
	UINTN n = state->cache_chunk_count;

	if (payload == GRANTLEY_CACHE_CHUNK)
		return !n || state->cache_chunk[n - 1].payload == GRANTLEY_CACHE_CHUNK;
	return n && payload < GRANTLEY_CACHE_CHUNK &&
	       state->cache_chunk[n - 1].payload == GRANTLEY_CACHE_CHUNK;
}

/*
 * Place an export chunk in the DRAM scratch and remember it; the module fills
 * in the GUID and payload after this returns.
 */
bool grantley_training_cache_redirect(struct grantley_uncore_state *state,
				      UINT16 length, UINT16 aligned_length,
				      void **hob)
{
	if (!state->cache_scratch || length < sizeof(EFI_HOB_GUID_TYPE) ||
	    state->cache_chunk_count >= GRANTLEY_CACHE_MAX_CHUNKS ||
	    state->cache_scratch_cap - state->cache_scratch_used < aligned_length)
		return false;

	uint8_t *p = state->cache_scratch + state->cache_scratch_used;
	/* Match CreateHob(): the module relies on a zeroed payload. */
	memset(p, 0, aligned_length);
	EFI_HOB_GENERIC_HEADER *h = (EFI_HOB_GENERIC_HEADER *)p;
	h->HobType = EFI_HOB_TYPE_GUID_EXTENSION;
	h->HobLength = aligned_length;
	h->Reserved = 0;

	state->cache_chunk[state->cache_chunk_count].hob = p;
	state->cache_chunk[state->cache_chunk_count].payload =
		length - sizeof(EFI_HOB_GUID_TYPE);
	state->cache_chunk_count++;
	state->cache_scratch_used += aligned_length;
	*hob = p;
	return true;
}

/* Concatenate the captured export chunks and stash them in RW_MRC_CACHE. */
void grantley_training_cache_save(struct grantley_uncore_state *state)
{
	if (!state->cache_scratch || !state->cache_chunk_count)
		return;

	uint8_t *dst = state->cache_scratch;
	UINTN total = 0;

	for (UINTN i = 0; i < state->cache_chunk_count; i++) {
		const EFI_HOB_GUID_TYPE *g =
			(const EFI_HOB_GUID_TYPE *)state->cache_chunk[i].hob;
		if (memcmp(&g->Name, &grantley_export_guid, sizeof(EFI_GUID)))
			continue;
		/* dst trails the source by the removed headers, so moving the
		 * payload down over the gap is always well defined. */
		memmove(dst + total, (const uint8_t *)g + sizeof(EFI_HOB_GUID_TYPE),
			state->cache_chunk[i].payload);
		total += state->cache_chunk[i].payload;
	}

	if (!total)
		return;

	if (mrc_cache_stash_data(MRC_TRAINING_DATA, GRANTLEY_MRC_CACHE_VERSION,
				 dst, total))
		printk(BIOS_ERR, "Grantley: failed to stash %llu training bytes\n",
		       (unsigned long long)total);
	else
		printk(BIOS_INFO, "Grantley: cached %llu training bytes\n",
		       (unsigned long long)total);
}

static int grantley_memoryconfig_index(const CHAR16 *name)
{
	static const char prefix[] = "MemoryConfig";

	for (size_t i = 0; i < sizeof(prefix) - 1; i++)
		if (name[i] != (CHAR16)prefix[i])
			return -1;
	if (name[sizeof(prefix) - 1] < '0' || name[sizeof(prefix) - 1] > '9' ||
	    name[sizeof(prefix)] != 0)
		return -1;
	return name[sizeof(prefix) - 1] - '0';
}

/*
 * Wrap raw bytes in a valid EFI-compressed stream with no back references: a
 * single block where all 256 literals share an 8-bit code, so each literal is
 * emitted as its own byte. The module's decompressor restores the bytes
 * unchanged (verified against the board blob).
 */
struct bit_writer {
	uint8_t *out;
	UINTN pos;
	uint8_t acc;
	int nbits;
};

/* Append the low 'bits' bits of 'val', most significant first. */
static void put_bits(struct bit_writer *w, uint32_t val, int bits)
{
	while (bits--) {
		w->acc = (w->acc << 1) | ((val >> bits) & 1);
		if (++w->nbits == 8) {
			w->out[w->pos++] = w->acc;
			w->acc = 0;
			w->nbits = 0;
		}
	}
}

static UINTN grantley_efi_store(const uint8_t *src, uint16_t len, uint8_t *dst)
{
	struct bit_writer w = { .out = dst + 8 };

	put_bits(&w, len, 16);	/* block size, in symbols */
	put_bits(&w, 0, 5);	/* extra set: one implied code length */
	put_bits(&w, 10, 5);
	put_bits(&w, 256, 9);	/* char&length set: 256 literals, length 8 */
	put_bits(&w, 0, 4);	/* position set: unused */
	put_bits(&w, 0, 4);
	for (uint16_t i = 0; i < len; i++)
		put_bits(&w, src[i], 8);
	if (w.nbits)
		put_bits(&w, 0, 8 - w.nbits);

	write_le32(dst, w.pos);		/* compressed size */
	write_le32(dst + 4, len);	/* original size */
	return w.pos + 8;
}

/* Minimum output for grantley_efi_store(): 8-byte header plus 43 stream bits. */
static UINTN grantley_store_size(uint16_t len)
{
	return 8 + (43 + 8 * (UINTN)len + 7) / 8;
}

EFI_STATUS grantley_training_cache_get(struct grantley_uncore_state *state,
				       const CHAR16 *name, const EFI_GUID *guid,
				       UINTN *data_size, void *data)
{
	int index = grantley_memoryconfig_index(name);
	if (index < 0 || memcmp(guid, &grantley_memcfg_guid, sizeof(EFI_GUID)))
		return EFI_NOT_FOUND;

	if (!state->cache_loaded) {
		size_t size = 0;
		state->cache_data = mrc_cache_current_mmap_leak(
			MRC_TRAINING_DATA, GRANTLEY_MRC_CACHE_VERSION, &size);
		state->cache_total = state->cache_data ? size : 0;
		state->cache_loaded = true;
	}
	if (!state->cache_data || !state->cache_total)
		return EFI_NOT_FOUND;

	/*
	 * The module reads the parts in order until it has the whole export, and
	 * the variable name carries a single digit, so split into ten parts.
	 */
	UINTN part = DIV_ROUND_UP(state->cache_total, 10);
	UINTN offset = (UINTN)index * part;
	if (offset >= state->cache_total)
		return EFI_NOT_FOUND;

	UINTN chunk = MIN(state->cache_total - offset, part);
	UINTN needed = grantley_store_size(chunk);

	if (!data || *data_size < needed) {
		/*
		 * The module takes a pool buffer for every part and never frees
		 * it; the previous part is already decompressed when the next
		 * size is queried, so all parts share one pool slot.
		 */
		if (!index)
			state->cache_pool_mark = state->pool_allocated;
		else
			state->pool_allocated = state->cache_pool_mark;
		*data_size = needed;
		return EFI_BUFFER_TOO_SMALL;
	}
	grantley_efi_store(state->cache_data + offset, chunk, data);
	*data_size = needed;
	if (!index)
		printk(BIOS_DEBUG, "Grantley: serving %zu cached training bytes\n",
		       state->cache_total);
	return EFI_SUCCESS;
}
