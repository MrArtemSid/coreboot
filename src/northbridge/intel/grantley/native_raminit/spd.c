/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/smbus_host.h>
#include <spd_bin.h>
#include <string.h>
#include "spd.h"

enum grantley_spd_status grantley_read_spd(uintptr_t base, uint8_t address,
					 struct dimm_attr_ddr4_st *dimm)
{
	spd_ddr4_raw_data raw;
	struct dimm_attr_ddr4_st decoded = {0};
	bool complete = false;

	if (!dimm || address < 0x50 || address > 0x57)
		return GRANTLEY_SPD_INVALID;

	memset(dimm, 0, sizeof(*dimm));
	/* EE1004 page selection is bus-wide, not per DIMM. Never trust its state. */
	if (do_smbus_send_byte(base, SPD_PAGE_0, 0) < 0)
		return GRANTLEY_SPD_IO_ERROR;

	for (unsigned int page = 0; page < 2; page++) {
		if (page && do_smbus_send_byte(base, SPD_PAGE_1, 0) < 0)
			goto restore_page;
		if (do_i2c_eeprom_read(base, address, 0, sizeof(raw) / 2,
				       &raw[page * sizeof(raw) / 2]) < 0)
			goto restore_page;
	}
	complete = true;

restore_page:
	/* Always attempt restoration, including after a failed page-one read. */
	if (do_smbus_send_byte(base, SPD_PAGE_0, 0) < 0 || !complete)
		return GRANTLEY_SPD_IO_ERROR;

	if (spd_decode_ddr4(&decoded, raw) != SPD_STATUS_OK)
		return GRANTLEY_SPD_INVALID;

	*dimm = decoded;
	return GRANTLEY_SPD_OK;
}
