/* SPDX-License-Identifier: GPL-2.0-only */

#include <northbridge/intel/haswell_ep/platform.h>
#include <tests/test.h>

static void test_haswell_ep_model(void **state)
{
	for (unsigned int stepping = 0; stepping < 16; stepping++)
		assert_int_equal(grantley_cpu_model(0x306f0 | stepping), GRANTLEY_HASWELL_EP);
}

static void test_broadwell_ep_model(void **state)
{
	for (unsigned int stepping = 0; stepping < 16; stepping++)
		assert_int_equal(grantley_cpu_model(0x406f0 | stepping), GRANTLEY_BROADWELL_EP);
}

static void test_other_cpu_models_rejected(void **state)
{
	const uint32_t signatures[] = {
		0, 0xffffffff, 0x306c3, 0x40651, 0x306d4, 0x40671,
		0x206d7, 0x306e4, 0x50654, 0x106f2, 0x1306f2,
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(signatures); i++)
		assert_int_equal(grantley_cpu_model(signatures[i]), GRANTLEY_CPU_UNKNOWN);
}

static void test_qd4_pch(void **state)
{
	assert_true(grantley_pch_matches(0x8086, 0x8c4e, GRANTLEY_BOARD_QD4));
	assert_false(grantley_pch_matches(0x8086, 0x8c50, GRANTLEY_BOARD_QD4));
}

static void test_bd4_pch(void **state)
{
	assert_true(grantley_pch_matches(0x8086, 0x8c50, GRANTLEY_BOARD_BD4));
	assert_false(grantley_pch_matches(0x8086, 0x8c4e, GRANTLEY_BOARD_BD4));
}

static void test_unverified_pch_rejected(void **state)
{
	const uint16_t devices[] = {0, 0xffff, 0x8c44, 0x8cc4, 0x9c43};

	for (unsigned int board = GRANTLEY_BOARD_QD4; board <= GRANTLEY_BOARD_BD4; board++) {
		assert_false(grantley_pch_matches(0x1234, 0x8c4e, board));
		assert_false(grantley_pch_matches(0x1234, 0x8c50, board));
		for (unsigned int i = 0; i < ARRAY_SIZE(devices); i++)
			assert_false(grantley_pch_matches(0x8086, devices[i], board));
	}
	assert_false(grantley_pch_matches(0x8086, 0x8c4e, 99));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_haswell_ep_model),
		cmocka_unit_test(test_broadwell_ep_model),
		cmocka_unit_test(test_other_cpu_models_rejected),
		cmocka_unit_test(test_qd4_pch),
		cmocka_unit_test(test_bd4_pch),
		cmocka_unit_test(test_unverified_pch_rejected),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
