/* SPDX-License-Identifier: GPL-2.0-only */

#include "pei_pcd.h"

/*
 * The database is deliberately tiny: this port can only name the tokens the
 * stock PcdPeim database spells out in bytes.  Everything else is reported as
 * unsupported, never invented.  See pei_pcd.h for the ABI and entry gate.
 */

static uint64_t mmcfg_base;
static uint64_t platform_mmcfg_base;
static enum grantley_pcd_fault fault;
static uint32_t fault_token;
static uint32_t sku_id;
/* QD4's contract: entry needs a value.  A board whose pinned module reads a
 * compiled-in base instead clears this (see grantley_pcd_set_token1_required).
 */
static bool token1_required = true;

void grantley_pcd_reset(void)
{
	mmcfg_base = 0;
	platform_mmcfg_base = 0;
	fault = GRANTLEY_PCD_OK;
	fault_token = 0;
	sku_id = 0;
}

static void pcd_fault(enum grantley_pcd_fault code, uint32_t token)
{
	/* Keep the first fault: it is the one that explains the run. */
	if (fault == GRANTLEY_PCD_OK) {
		fault = code;
		fault_token = token;
	}
}

int grantley_pcd_set_mmcfg_base(uint64_t base, uint64_t platform_base)
{
	/* Equality, not alignment: every 4 KiB-aligned MMIO constant would pass
	 * an alignment test, and a wrong base silently retargets the PEIM's
	 * writes into unrelated fabric (0x40100000 hits CSR space).
	 */
	if (base == 0 || platform_base == 0 || base != platform_base ||
	    (base & 0xfff) != 0 ||
	    base > 0x100000000ULL - GRANTLEY_PCD_MMCFG_WINDOW_SIZE) {
		pcd_fault(GRANTLEY_PCD_FAULT_INVALID_VALUE, GRANTLEY_PCD_TOKEN_MMCFG_BASE);
		return -1;
	}

	mmcfg_base = base;
	platform_mmcfg_base = platform_base;
	/* A successful supply supersedes an earlier missing/rejected token 1
	 * fault: the value the PEIM would read is now the proven one.
	 */
	if (fault_token == GRANTLEY_PCD_TOKEN_MMCFG_BASE &&
	    (fault == GRANTLEY_PCD_FAULT_MISSING_VALUE ||
	     fault == GRANTLEY_PCD_FAULT_INVALID_VALUE)) {
		fault = GRANTLEY_PCD_OK;
		fault_token = 0;
	}
	return 0;
}

uint64_t grantley_pcd_mmcfg_base(void)
{
	return mmcfg_base;
}

void grantley_pcd_set_token1_required(bool required)
{
	token1_required = required;
}

enum grantley_pcd_fault grantley_pcd_fault(void)
{
	return fault;
}

uint32_t grantley_pcd_fault_token(void)
{
	return fault_token;
}

const char *grantley_pcd_fault_string(enum grantley_pcd_fault code)
{
	switch (code) {
	case GRANTLEY_PCD_OK:
		return "no fault";
	case GRANTLEY_PCD_FAULT_MISSING_VALUE:
		return "token 1 has no supplied value";
	case GRANTLEY_PCD_FAULT_INVALID_VALUE:
		return "supplied token 1 value was rejected";
	case GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN:
		return "token is not in this build's PCD database";
	case GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN_SPACE:
		return "no PI PCD token space is defined in this port";
	case GRANTLEY_PCD_FAULT_WRITE_UNSUPPORTED:
		return "PCD writes/enumeration are not supported";
	}
	return "unknown PCD fault";
}

/* Single lookup used by every value-returning getter. */
static uint64_t pcd_lookup(uint32_t token, int *found)
{
	*found = 0;

	switch (token) {
	case GRANTLEY_PCD_TOKEN_MMCFG_BASE:
		if (mmcfg_base == 0) {
			pcd_fault(GRANTLEY_PCD_FAULT_MISSING_VALUE, token);
			return 0;
		}
		*found = 1;
		return mmcfg_base;
	case GRANTLEY_PCD_TOKEN_BAUD:
		/* Stock db slot db+0x48: 00 c2 01 00 00 00 00 00 (115200). */
		*found = 1;
		return GRANTLEY_PCD_TOKEN_BAUD_VALUE;
	case GRANTLEY_PCD_TOKEN_4:
		/* Stock db slot db+0x50: 00 00 aa aa 22 aa 33 aa. */
		*found = 1;
		return GRANTLEY_PCD_TOKEN_4_VALUE;
	default:
		/* Tokens 3, 6 and 8 resolve into the five zero-fill bytes past the
		 * copied database: their runtime value is not in the image.  The
		 * ABI cannot return an error, so record it and return 0.  This is
		 * diagnostic only; it is not fail-closed after entry.
		 */
		pcd_fault(GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN, token);
		return 0;
	}
}

static void EFIAPI pcd_set_sku(UINTN new_sku)
{
	/* No token in the table is SKU-enabled, so the PI contract says the SKU
	 * is remembered and ignored.  Keep it for diagnostics only.
	 */
	sku_id = (uint32_t)new_sku;
}

static UINT8 EFIAPI pcd_get8(UINTN token)
{
	int found;
	return (UINT8)pcd_lookup((uint32_t)token, &found);
}

static UINT16 EFIAPI pcd_get16(UINTN token)
{
	int found;
	return (UINT16)pcd_lookup((uint32_t)token, &found);
}

static UINT32 EFIAPI pcd_get32(UINTN token)
{
	int found;
	return (UINT32)pcd_lookup((uint32_t)token, &found);
}

static UINT64 EFIAPI pcd_get64(UINTN token)
{
	int found;
	return pcd_lookup((uint32_t)token, &found);
}

static VOID *EFIAPI pcd_get_ptr(UINTN token)
{
	int found;

	(void)pcd_lookup((uint32_t)token, &found);
	/* No token in this database is a buffer pointer. */
	return (VOID *)0;
}

static BOOLEAN EFIAPI pcd_get_bool(UINTN token)
{
	int found;
	return (BOOLEAN)(pcd_lookup((uint32_t)token, &found) != 0);
}

static UINTN EFIAPI pcd_get_size(UINTN token)
{
	int found;

	if (token == GRANTLEY_PCD_TOKEN_MMCFG_BASE && mmcfg_base != 0)
		return sizeof(mmcfg_base);
	if (token == GRANTLEY_PCD_TOKEN_BAUD || token == GRANTLEY_PCD_TOKEN_4)
		return sizeof(uint64_t);
	(void)pcd_lookup((uint32_t)token, &found);
	return 0;
}

/* EX getters: this port defines no PI PCD token space, so no EX token can be
 * named.  The value-returning ABI cannot say so; record the fault.
 */
static UINT8 EFIAPI pcd_get8_ex(const EFI_GUID *guid, UINTN token)
{
	pcd_fault(GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN_SPACE, (uint32_t)token);
	return 0;
}

static UINT16 EFIAPI pcd_get16_ex(const EFI_GUID *guid, UINTN token)
{
	pcd_fault(GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN_SPACE, (uint32_t)token);
	return 0;
}

static UINT32 EFIAPI pcd_get32_ex(const EFI_GUID *guid, UINTN token)
{
	pcd_fault(GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN_SPACE, (uint32_t)token);
	return 0;
}

static UINT64 EFIAPI pcd_get64_ex(const EFI_GUID *guid, UINTN token)
{
	pcd_fault(GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN_SPACE, (uint32_t)token);
	return 0;
}

static VOID *EFIAPI pcd_get_ptr_ex(const EFI_GUID *guid, UINTN token)
{
	pcd_fault(GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN_SPACE, (uint32_t)token);
	return (VOID *)0;
}

static BOOLEAN EFIAPI pcd_get_bool_ex(const EFI_GUID *guid, UINTN token)
{
	pcd_fault(GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN_SPACE, (uint32_t)token);
	return 0;
}

static UINTN EFIAPI pcd_get_size_ex(const EFI_GUID *guid, UINTN token)
{
	pcd_fault(GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN_SPACE, (uint32_t)token);
	return 0;
}

/* Writes have a status channel, so they refuse honestly instead of pretending
 * the value was stored.  A caller that needs a writable database must not use
 * this provider.
 */
static EFI_STATUS pcd_write_unavailable(UINTN token)
{
	pcd_fault(GRANTLEY_PCD_FAULT_WRITE_UNSUPPORTED, (uint32_t)token);
	return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI pcd_set8(UINTN token, UINT8 value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_set16(UINTN token, UINT16 value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_set32(UINTN token, UINT32 value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_set64(UINTN token, UINT64 value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_set_ptr(UINTN token, UINTN *size_of_value, VOID *buffer)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_set_bool(UINTN token, BOOLEAN value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_set8_ex(const EFI_GUID *guid, UINTN token, UINT8 value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_set16_ex(const EFI_GUID *guid, UINTN token, UINT16 value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_set32_ex(const EFI_GUID *guid, UINTN token, UINT32 value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_set64_ex(const EFI_GUID *guid, UINTN token, UINT64 value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_set_ptr_ex(const EFI_GUID *guid, UINTN token,
					UINTN *size_of_value, VOID *buffer)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_set_bool_ex(const EFI_GUID *guid, UINTN token, BOOLEAN value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_callback_on_set(const EFI_GUID *guid, UINTN token,
					     PCD_PPI_CALLBACK callback)
{
	pcd_fault(GRANTLEY_PCD_FAULT_WRITE_UNSUPPORTED, (uint32_t)token);
	return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI pcd_cancel_callback(const EFI_GUID *guid, UINTN token,
					     PCD_PPI_CALLBACK callback)
{
	pcd_fault(GRANTLEY_PCD_FAULT_WRITE_UNSUPPORTED, (uint32_t)token);
	return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI pcd_get_next_token(const EFI_GUID *guid, UINTN *token)
{
	/* Token 1 was already reported by the getters; there is no second one. */
	return EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI pcd_get_next_token_space(const EFI_GUID **guid)
{
	return EFI_NOT_FOUND;
}

PCD_PPI grantley_pcd_ppi = {
	.SetSku = pcd_set_sku,
	.Get8 = pcd_get8,
	.Get16 = pcd_get16,
	.Get32 = pcd_get32,
	.Get64 = pcd_get64,
	.GetPtr = pcd_get_ptr,
	.GetBool = pcd_get_bool,
	.GetSize = pcd_get_size,
	.Get8Ex = pcd_get8_ex,
	.Get16Ex = pcd_get16_ex,
	.Get32Ex = pcd_get32_ex,
	.Get64Ex = pcd_get64_ex,
	.GetPtrEx = pcd_get_ptr_ex,
	.GetBoolEx = pcd_get_bool_ex,
	.GetSizeEx = pcd_get_size_ex,
	.Set8 = pcd_set8,
	.Set16 = pcd_set16,
	.Set32 = pcd_set32,
	.Set64 = pcd_set64,
	.SetPtr = pcd_set_ptr,
	.SetBool = pcd_set_bool,
	.Set8Ex = pcd_set8_ex,
	.Set16Ex = pcd_set16_ex,
	.Set32Ex = pcd_set32_ex,
	.Set64Ex = pcd_set64_ex,
	.SetPtrEx = pcd_set_ptr_ex,
	.SetBoolEx = pcd_set_bool_ex,
	.CallbackOnSet = pcd_callback_on_set,
	.CancelCallback = pcd_cancel_callback,
	.GetNextToken = pcd_get_next_token,
	.GetNextTokenSpace = pcd_get_next_token_space,
};

/*
 * PI 1.2 EFI_PEI_PCD_PPI.  Same getter order as the native table, but every
 * getter takes a token space GUID.  The stock PEIM never calls this PPI; it is
 * installed because the module's PEI_DEPEX names it, and it must still be a
 * real instance with a value-returning interface rather than a placeholder.
 */
static void EFIAPI pcd_pi_set_sku(UINTN new_sku)
{
	sku_id = (uint32_t)new_sku;
}

static UINT8 EFIAPI pcd_pi_get8(const EFI_GUID *guid, UINTN token)
{
	return pcd_get8_ex(guid, token);
}

static UINT16 EFIAPI pcd_pi_get16(const EFI_GUID *guid, UINTN token)
{
	return pcd_get16_ex(guid, token);
}

static UINT32 EFIAPI pcd_pi_get32(const EFI_GUID *guid, UINTN token)
{
	return pcd_get32_ex(guid, token);
}

static UINT64 EFIAPI pcd_pi_get64(const EFI_GUID *guid, UINTN token)
{
	return pcd_get64_ex(guid, token);
}

static VOID *EFIAPI pcd_pi_get_ptr(const EFI_GUID *guid, UINTN token)
{
	return pcd_get_ptr_ex(guid, token);
}

static BOOLEAN EFIAPI pcd_pi_get_bool(const EFI_GUID *guid, UINTN token)
{
	return pcd_get_bool_ex(guid, token);
}

static UINTN EFIAPI pcd_pi_get_size(const EFI_GUID *guid, UINTN token)
{
	return pcd_get_size_ex(guid, token);
}

static EFI_STATUS EFIAPI pcd_pi_set8(const EFI_GUID *guid, UINTN token, UINT8 value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_pi_set16(const EFI_GUID *guid, UINTN token, UINT16 value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_pi_set32(const EFI_GUID *guid, UINTN token, UINT32 value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_pi_set64(const EFI_GUID *guid, UINTN token, UINT64 value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_pi_set_ptr(const EFI_GUID *guid, UINTN token,
					UINTN *size_of_value, VOID *buffer)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_pi_set_bool(const EFI_GUID *guid, UINTN token, BOOLEAN value)
{
	return pcd_write_unavailable(token);
}

static EFI_STATUS EFIAPI pcd_pi_callback_on_set(const EFI_GUID *guid, UINTN token,
						EFI_PEI_PCD_PPI_CALLBACK callback)
{
	pcd_fault(GRANTLEY_PCD_FAULT_WRITE_UNSUPPORTED, (uint32_t)token);
	return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI pcd_pi_cancel_callback(const EFI_GUID *guid, UINTN token,
						EFI_PEI_PCD_PPI_CALLBACK callback)
{
	pcd_fault(GRANTLEY_PCD_FAULT_WRITE_UNSUPPORTED, (uint32_t)token);
	return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI pcd_pi_get_next_token(const EFI_GUID *guid, UINTN *token)
{
	return EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI pcd_pi_get_next_token_space(const EFI_GUID **guid)
{
	return EFI_NOT_FOUND;
}

EFI_PEI_PCD_PPI grantley_efi_pei_pcd_ppi = {
	.SetSku = pcd_pi_set_sku,
	.Get8 = pcd_pi_get8,
	.Get16 = pcd_pi_get16,
	.Get32 = pcd_pi_get32,
	.Get64 = pcd_pi_get64,
	.GetPtr = pcd_pi_get_ptr,
	.GetBool = pcd_pi_get_bool,
	.GetSize = pcd_pi_get_size,
	.Set8 = pcd_pi_set8,
	.Set16 = pcd_pi_set16,
	.Set32 = pcd_pi_set32,
	.Set64 = pcd_pi_set64,
	.SetPtr = pcd_pi_set_ptr,
	.SetBool = pcd_pi_set_bool,
	.CallbackOnSet = pcd_pi_callback_on_set,
	.CancelCallback = pcd_pi_cancel_callback,
	.GetNextToken = pcd_pi_get_next_token,
	.GetNextTokenSpace = pcd_pi_get_next_token_space,
};

const char *grantley_pcd_blocking_reason(void)
{
	if (fault == GRANTLEY_PCD_FAULT_INVALID_VALUE)
		return "the supplied PCD token 1 value was rejected (it must EQUAL the "
		       "programmed MMCFG base, be non-zero, 4 KiB aligned, and keep a "
		       "0xF8050 window below 4 GiB)";
	if (fault == GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN_SPACE)
		return "the PEIM asked for a PI PCD token space, which this port does not "
		       "define";
	if (fault == GRANTLEY_PCD_FAULT_WRITE_UNSUPPORTED)
		return "the PEIM tried to write or enumerate the PCD database, which this "
		       "port does not support";
	if (fault == GRANTLEY_PCD_FAULT_UNKNOWN_TOKEN)
		return "the PEIM asked for a PCD token that is not in this build's database";
	if (mmcfg_base == 0 && token1_required)
		return "PCD token 1 (MMCFG/ECAM base) has no supplied value: refusing to "
		       "enter the PEIM with a guessed or zero base";
	return (const char *)0;
}
