/* SPDX-License-Identifier: GPL-2.0-only */

#include <string.h>
#include <tests/test.h>
#include <arch/romstage.h>
#include <arch/symbols.h>
#include <cbmem.h>
#include <cbfs.h>
#include <console/console.h>
#include <cpu/cpu.h>
#include <cpu/intel/grantley/grantley.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <device/smbus_host.h>
#include <northbridge/intel/grantley/native_raminit/spd.h>
#include <post.h>
#include <romstage_common.h>
#include <romstage_handoff.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <northbridge/intel/grantley/platform.h>
#include <northbridge/intel/grantley/memmap.h>
#undef NULL
#include <northbridge/intel/grantley/uncore_runner/pei_services.h>

static jmp_buf exit_point;
static EFI_STATUS route_status, peim_status, memory_status;
static bool install_ok;
static uintptr_t car_used_end;
static int peim_calls, map_calls, cbmem_calls, save_calls, install_calls;

static void __noreturn test_stop(int result)
{
	longjmp(exit_point, result);
}

uint8_t *const pci_mmconf = NULL;
void post_code(uint8_t value) { }
void system_reset(void) { __builtin_trap(); }
uint32_t grantley_test_route_read32(uint32_t dev, uint16_t reg, uintptr_t base);
void grantley_test_route_write32(uint32_t dev, uint16_t reg, uintptr_t base, uint32_t value);
uint32_t grantley_test_route_cpuid(void);
uint32_t grantley_test_route_read32(uint32_t dev, uint16_t reg, uintptr_t base) { return 0; }
void grantley_test_route_write32(uint32_t dev, uint16_t reg, uintptr_t base, uint32_t value) { }
uint32_t grantley_test_route_cpuid(void) { return 0x306f2; }

static EFI_STATUS test_run(struct grantley_uncore_state *state)
{
	EFI_HOB_GUID_TYPE *hob = (void *)((uint8_t *)state->hob_list +
					 sizeof(EFI_HOB_HANDOFF_INFO_TABLE));
	uint8_t *data = (void *)(hob + 1);
	assert_ptr_equal(data, state->platform_info_ppi.Ppi);
	assert_int_equal(data[0x46], TEST_BOARD_ID);
	uint32_t revision;
	memcpy(&revision, data + 0x1e, sizeof(revision));
	assert_int_equal(revision, TEST_BOARD_REVISION);
	peim_calls++;
	return peim_status;
}

static EFI_STATUS test_map(void)
{
	map_calls++;
	return memory_status;
}

static bool test_install(void)
{
	assert_int_equal(cbmem_calls, 0);
	install_calls++;
	return install_ok;
}

static void test_cbmem(void)
{
	assert_int_equal(install_calls, 1);
	cbmem_calls++;
}

static void test_save(void)
{
	assert_int_equal(cbmem_calls, 1);
	save_calls++;
}

#undef CONFIG_HAVE_INTEL_UNCORE_BIN
#define CONFIG_HAVE_INTEL_UNCORE_BIN 1
#define cpuid_eax(leaf) 0x306f2
#define pci_s_read_config32(dev, reg) 0
#define early_pch_init() do { } while (0)
#define enable_smbus() do { } while (0)
#define smbus_base() 0
#define grantley_read_spd(...) ((enum grantley_spd_status)-1)
#define cbfs_ro_map(...) ((void *)0x1000)
#define grantley_pei_set_mmcfg(...) route_status
#define grantley_pei_set_mem_params(...) do { } while (0)
#define post_code(...) do { } while (0)
#undef printk
#define printk(...) do { } while (0)
#define die(...) test_stop(1)
#define grantley_run_uncore_peim(bin, state) test_run(state)
#define grantley_pei_memory_map(...) test_map()
#define grantley_memmap_install(...) test_install()
#define cbmem_initialize_empty() test_cbmem()
#define grantley_memmap_save() test_save()
#define romstage_handoff_init(s3) assert_false(s3)
#define prepare_and_run_postcar() test_stop(2)
#define _car_unallocated_start ((char *)car_used_end)
#include "../../../src/northbridge/intel/grantley/romstage.c"

static int setup(void **unused)
{
	route_status = peim_status = memory_status = EFI_SUCCESS;
	install_ok = true;
	car_used_end = CONFIG_DCACHE_RAM_BASE + 0x20000;
	peim_calls = map_calls = cbmem_calls = save_calls = install_calls = 0;
	return 0;
}

static void test_static_storage_overlapping_peim_stack_stops(void **unused)
{
	car_used_end = CONFIG_DCACHE_RAM_BASE + CONFIG_DCACHE_RAM_SIZE - 0x10000;
	int result = setjmp(exit_point);
	if (!result)
		romstage_main();
	assert_int_equal(result, 1);
	assert_int_equal(peim_calls, 0);
	assert_int_equal(cbmem_calls, 0);
}

static void test_bad_routing_stops_before_peim(void **unused)
{
	route_status = EFI_DEVICE_ERROR;
	int result = setjmp(exit_point);
	if (!result)
		romstage_main();
	assert_int_equal(result, 1);
	assert_int_equal(peim_calls, 0);
	assert_int_equal(cbmem_calls, 0);
}

static void test_bad_memory_stops_before_cbmem(void **unused)
{
	memory_status = EFI_DEVICE_ERROR;
	int result = setjmp(exit_point);
	if (!result)
		romstage_main();
	assert_int_equal(result, 1);
	assert_int_equal(peim_calls, 1);
	assert_int_equal(cbmem_calls, 0);
}

static void test_map_install_failure_stops_before_cbmem(void **unused)
{
	install_ok = false;
	int result = setjmp(exit_point);
	if (!result)
		romstage_main();
	assert_int_equal(result, 1);
	assert_int_equal(cbmem_calls, 0);
}

static void test_map_saved_before_postcar(void **unused)
{
	int result = setjmp(exit_point);
	if (!result)
		romstage_main();
	assert_int_equal(result, 2);
	assert_int_equal(map_calls, 1);
	assert_int_equal(save_calls, 1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_static_storage_overlapping_peim_stack_stops, setup),
		cmocka_unit_test_setup(test_bad_routing_stops_before_peim, setup),
		cmocka_unit_test_setup(test_bad_memory_stops_before_cbmem, setup),
		cmocka_unit_test_setup(test_map_install_failure_stops_before_cbmem, setup),
		cmocka_unit_test_setup(test_map_saved_before_postcar, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
