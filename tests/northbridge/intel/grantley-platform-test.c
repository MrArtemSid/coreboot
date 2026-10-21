/* SPDX-License-Identifier: GPL-2.0-only */

#include <northbridge/intel/grantley/platform.h>
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

static void test_supported_lynxpoint_pch(void **state)
{
	/* Verify all standard Lynx Point 8-series desktop and server PCH IDs */
	const uint16_t supported_8series[] = {
		0x8c44, /* Z87 */
		0x8c4a, /* H87 */
		0x8c4c, /* Q85 */
		0x8c4e, /* Q87 */
		0x8c50, /* B85 */
		0x8c52, /* C222 */
		0x8c54, /* C224 */
		0x8c56, /* C226 */
		0x8c5c, /* H81 */
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(supported_8series); i++)
		assert_true(grantley_pch_is_supported(0x8086, supported_8series[i]));
}

static void test_supported_wildcat_and_wellsburg_pch(void **state)
{
	/* Wildcat Point 9-series (Z97, H97) */
	assert_true(grantley_pch_is_supported(0x8086, 0x8cc4));
	assert_true(grantley_pch_is_supported(0x8086, 0x8cc6));

	/* Wellsburg C610/C612 / X99 */
	assert_true(grantley_pch_is_supported(0x8086, 0x8d44));
	assert_true(grantley_pch_is_supported(0x8086, 0x8d50));
}

static void test_unsupported_pch_rejected(void **state)
{
	const uint16_t invalid_devices[] = {
		0, 0xffff, 0x1e20, 0x2448, 0x9c43, 0xa145, 0x0680,
	};

	/* Non-Intel vendor must always fail */
	assert_false(grantley_pch_is_supported(0x1234, 0x8c4e));
	assert_false(grantley_pch_is_supported(0x10de, 0x8c50));

	/* Unsupported device IDs must fail */
	for (unsigned int i = 0; i < ARRAY_SIZE(invalid_devices); i++)
		assert_false(grantley_pch_is_supported(0x8086, invalid_devices[i]));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_haswell_ep_model),
		cmocka_unit_test(test_broadwell_ep_model),
		cmocka_unit_test(test_other_cpu_models_rejected),
		cmocka_unit_test(test_supported_lynxpoint_pch),
		cmocka_unit_test(test_supported_wildcat_and_wellsburg_pch),
		cmocka_unit_test(test_unsupported_pch_rejected),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
