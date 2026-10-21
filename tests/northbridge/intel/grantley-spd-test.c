/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/dram/ddr4.h>
#include <device/smbus_host.h>
#include <northbridge/intel/grantley/native_raminit/spd.h>
#include <spd_bin.h>
#include <string.h>
#include <tests/test.h>

/* Synthetic EE1004 contents, not a dump from a physical DIMM. */
static spd_ddr4_raw_data eeprom;
static unsigned int page;
static unsigned int selections[8];
static unsigned int selection_count;
static int fail_selection;
static int fail_read;
static unsigned int read_count;

int do_smbus_send_byte(uintptr_t base, uint8_t device, uint8_t value)
{
	assert_int_equal(base, 0x400);
	assert_int_equal(value, 0);
	assert_true(device == SPD_PAGE_0 || device == SPD_PAGE_1);
	assert_true(selection_count < ARRAY_SIZE(selections));
	selections[selection_count++] = device - SPD_PAGE_0;
	if ((int)selection_count == fail_selection)
		return -1;
	page = device - SPD_PAGE_0;
	return 0;
}

int do_smbus_read_byte(uintptr_t base, uint8_t device, uint8_t address)
{
	assert_int_equal(base, 0x400);
	assert_int_equal(device, 0x50);
	read_count++;
	unsigned int offset = page * 256 + address;
	if ((int)offset == fail_read)
		return -1;
	return eeprom[offset];
}

static void update_crc(void)
{
	for (unsigned int offset = 0; offset < 256; offset += 128) {
		uint16_t crc = ddr_crc16(eeprom + offset, 126);
		eeprom[offset + 126] = crc;
		eeprom[offset + 127] = crc >> 8;
	}
}

static int setup(void **state)
{
	memset(eeprom, 0, sizeof(eeprom));
	eeprom[0] = 0x23; /* 384 bytes used, 512 bytes available */
	eeprom[1] = 0x10;
	eeprom[2] = SPD_MEMORY_TYPE_DDR4_SDRAM;
	eeprom[3] = SPD_DDR4_DIMM_TYPE_UDIMM;
	eeprom[4] = 5; /* 8 Gbit per die */
	eeprom[12] = 1; /* one rank, x8 devices */
	eeprom[13] = 3; /* 64-bit primary bus */
	memcpy(eeprom + SPD_DDR4_PART_OFF, "SYNTHETIC-DDR4-TEST ", SPD_DDR4_PART_LEN);
	update_crc();
	page = 1; /* Do not assume that a previous user left page zero selected. */
	selection_count = 0;
	read_count = 0;
	fail_selection = -1;
	fail_read = -1;
	return 0;
}

static void test_read_and_decode_ddr4(void **state)
{
	struct dimm_attr_ddr4_st dimm;

	assert_int_equal(grantley_read_spd(0x400, 0x50, &dimm), GRANTLEY_SPD_OK);
	assert_int_equal(dimm.size_mb, 8192);
	assert_int_equal(dimm.ranks, 1);
	assert_int_equal(dimm.dimm_type, SPD_DDR4_DIMM_TYPE_UDIMM);
	assert_string_equal(dimm.part_number, "SYNTHETIC-DDR4-TEST ");
	assert_int_equal(read_count, 512);
	assert_int_equal(selection_count, 3);
	assert_int_equal(selections[0], 0);
	assert_int_equal(selections[1], 1);
	assert_int_equal(selections[2], 0);
	assert_int_equal(page, 0);
}

static void test_invalid_address_rejected(void **state)
{
	struct dimm_attr_ddr4_st dimm;

	assert_int_equal(grantley_read_spd(0x400, 0x58, &dimm), GRANTLEY_SPD_INVALID);
	assert_int_equal(grantley_read_spd(0x400, 0x50, NULL), GRANTLEY_SPD_INVALID);
	assert_int_equal(selection_count, 0);
	assert_int_equal(read_count, 0);
}

static void test_smbus_io_failure_and_page_restore(void **state)
{
	struct dimm_attr_ddr4_st dimm;

	/* Failure on initial page 0 selection */
	fail_selection = 1;
	assert_int_equal(grantley_read_spd(0x400, 0x50, &dimm), GRANTLEY_SPD_IO_ERROR);

	/* Failure during byte read on page 0 triggers restore to page 0 */
	setup(state);
	fail_read = 50;
	assert_int_equal(grantley_read_spd(0x400, 0x50, &dimm), GRANTLEY_SPD_IO_ERROR);
	assert_int_equal(page, 0);
	assert_int_equal(selection_count, 2); /* initial page 0, then restore page 0 */

	/* Failure during byte read on page 1 triggers restore to page 0 */
	setup(state);
	fail_read = 300;
	assert_int_equal(grantley_read_spd(0x400, 0x50, &dimm), GRANTLEY_SPD_IO_ERROR);
	assert_int_equal(page, 0);
	assert_int_equal(selection_count, 3); /* page 0, page 1, restore page 0 */
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_read_and_decode_ddr4, setup),
		cmocka_unit_test_setup(test_invalid_address_rejected, setup),
		cmocka_unit_test_setup(test_smbus_io_failure_and_page_restore, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
