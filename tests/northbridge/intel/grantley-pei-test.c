/* SPDX-License-Identifier: GPL-2.0-only */

#include <string.h>
#include <tests/test.h>
#include <commonlib/region.h>
#undef NULL
#include <northbridge/intel/grantley/uncore_runner/pei_services.h>

static struct grantley_uncore_state state;
static UINT8 pool[4096];
static UINT8 hob_buffer[4096];

static const EFI_GUID test_guid_1 = {
	0x11111111, 0x1111, 0x1111, { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 }
};
static const EFI_GUID test_guid_2 = {
	0x22222222, 0x2222, 0x2222, { 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22 }
};
static const EFI_GUID dummy_guid = {
	0x99999999, 0x9999, 0x9999, { 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99 }
};

static int setup(void **s)
{
	memset(pool, 0, sizeof(pool));
	memset(hob_buffer, 0, sizeof(hob_buffer));
	assert_int_equal(grantley_pei_services_init(&state, pool, sizeof(pool),
						   hob_buffer, sizeof(hob_buffer)),
			 EFI_SUCCESS);
	return 0;
}

static void test_pei_services_init(void **s)
{
	CONST EFI_PEI_SERVICES **services = &state.services_ptr;
	assert_non_null(services);
	assert_non_null(*services);

	assert_int_equal((*services)->Hdr.Signature, PEI_SERVICES_SIGNATURE);
	assert_int_equal((*services)->Hdr.Revision, PEI_SERVICES_REVISION);
	assert_int_equal((*services)->Hdr.HeaderSize, sizeof(EFI_TABLE_HEADER));

	EFI_BOOT_MODE boot_mode = 0xff;
	assert_int_equal((*services)->GetBootMode(services, &boot_mode), EFI_SUCCESS);
	assert_int_equal(boot_mode, BOOT_WITH_FULL_CONFIGURATION);

	VOID *hob_list = NULL;
	assert_int_equal((*services)->GetHobList(services, &hob_list), EFI_SUCCESS);
	assert_ptr_equal(hob_list, hob_buffer);

	EFI_HOB_HANDOFF_INFO_TABLE *hit = (EFI_HOB_HANDOFF_INFO_TABLE *)hob_list;
	assert_int_equal(hit->Header.HobType, EFI_HOB_TYPE_HANDOFF);
	assert_int_equal(hit->Header.HobLength, sizeof(EFI_HOB_HANDOFF_INFO_TABLE));
}

static void test_ppi_install_locate_reinstall(void **s)
{
	CONST EFI_PEI_SERVICES **services = &state.services_ptr;
	int iface_1 = 42;
	int iface_2 = 84;
	int iface_2_new = 168;

	EFI_PEI_PPI_DESCRIPTOR list[] = {
		{
			EFI_PEI_PPI_DESCRIPTOR_PPI,
			(EFI_GUID *)&test_guid_1,
			&iface_1,
		},
		{
			EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
			(EFI_GUID *)&test_guid_2,
			&iface_2,
		},
	};

	assert_int_equal((*services)->InstallPpi(services, list), EFI_SUCCESS);

	void *found_iface = NULL;
	EFI_PEI_PPI_DESCRIPTOR *found_desc = NULL;
	assert_int_equal((*services)->LocatePpi(services, &test_guid_1, 0, &found_desc, &found_iface),
			 EFI_SUCCESS);
	assert_ptr_equal(found_iface, &iface_1);
	assert_ptr_equal(found_desc, &list[0]);

	assert_int_equal((*services)->LocatePpi(services, &test_guid_2, 0, &found_desc, &found_iface),
			 EFI_SUCCESS);
	assert_ptr_equal(found_iface, &iface_2);

	/* Unregistered GUID must return NOT_FOUND */
	assert_int_equal((*services)->LocatePpi(services, &dummy_guid, 0, NULL, &found_iface),
			 EFI_NOT_FOUND);

	/* Reinstall test */
	EFI_PEI_PPI_DESCRIPTOR new_desc = {
		EFI_PEI_PPI_DESCRIPTOR_PPI,
		(EFI_GUID *)&test_guid_2,
		&iface_2_new,
	};
	assert_int_equal((*services)->ReInstallPpi(services, &list[1], &new_desc), EFI_SUCCESS);

	assert_int_equal((*services)->LocatePpi(services, &test_guid_2, 0, NULL, &found_iface),
			 EFI_SUCCESS);
	assert_ptr_equal(found_iface, &iface_2_new);
}

static void test_pool_and_memory_helpers(void **s)
{
	CONST EFI_PEI_SERVICES **services = &state.services_ptr;

	void *buf1 = NULL;
	void *buf2 = NULL;
	assert_int_equal((*services)->AllocatePool(services, 30, &buf1), EFI_SUCCESS);
	assert_non_null(buf1);
	assert_int_equal(((uintptr_t)buf1) & 7, 0); /* 8-byte aligned */

	assert_int_equal((*services)->AllocatePool(services, 64, &buf2), EFI_SUCCESS);
	assert_non_null(buf2);
	assert_true((uint8_t *)buf2 >= (uint8_t *)buf1 + 32);

	/* SetMem and CopyMem check */
	(*services)->SetMem(buf1, 30, 0xaa);
	for (int i = 0; i < 30; i++)
		assert_int_equal(((uint8_t *)buf1)[i], 0xaa);

	(*services)->CopyMem(buf2, buf1, 30);
	for (int i = 0; i < 30; i++)
		assert_int_equal(((uint8_t *)buf2)[i], 0xaa);

	/* Excessive allocation fails cleanly */
	void *huge_buf = NULL;
	assert_int_equal((*services)->AllocatePool(services, 100000, &huge_buf),
			 EFI_OUT_OF_RESOURCES);
}

static void test_create_hob(void **s)
{
	CONST EFI_PEI_SERVICES **services = &state.services_ptr;

	void *new_hob = NULL;
	assert_int_equal((*services)->CreateHob(services, EFI_HOB_TYPE_RESOURCE_DESCRIPTOR,
					       sizeof(EFI_HOB_RESOURCE_DESCRIPTOR), &new_hob),
			 EFI_SUCCESS);
	assert_non_null(new_hob);

	EFI_HOB_GENERIC_HEADER *hdr = (EFI_HOB_GENERIC_HEADER *)new_hob;
	assert_int_equal(hdr->HobType, EFI_HOB_TYPE_RESOURCE_DESCRIPTOR);
	assert_true(hdr->HobLength >= sizeof(EFI_HOB_RESOURCE_DESCRIPTOR));

	/* Verify the next HOB in list is END_OF_HOB_LIST */
	EFI_HOB_GENERIC_HEADER *next_hdr = (EFI_HOB_GENERIC_HEADER *)((uint8_t *)new_hob + hdr->HobLength);
	assert_int_equal(next_hdr->HobType, EFI_HOB_TYPE_END_OF_HOB_LIST);
}

static void test_install_pei_memory(void **s)
{
	CONST EFI_PEI_SERVICES **services = &state.services_ptr;

	assert_false(state.dram_initialized);

	EFI_PHYSICAL_ADDRESS base = 0x0;
	UINT64 size = 0x400000000ULL; /* 16 GiB */

	assert_int_equal((*services)->InstallPeiMemory(services, base, size), EFI_SUCCESS);

	assert_true(state.dram_initialized);
	assert_int_equal(state.dram_base, 0);
	assert_int_equal(state.dram_size, 0x400000000ULL);
}

UINT64 LShiftU64(UINT64 Operand, UINTN Count);
UINT64 LShiftU64(UINT64 Operand, UINTN Count)
{
	return Operand << Count;
}

static void test_pci_cfg_and_cpu_io_ppi(void **s)
{
	CONST EFI_PEI_SERVICES **services = &state.services_ptr;

	/* Check that PciCfg2 is located properly */
	static const EFI_GUID pci_cfg_guid = {
		0x57a449a, 0x1fdc, 0x4c06, { 0xbf, 0xc9, 0xf5, 0x3f, 0x6a, 0x99, 0xbb, 0x92 }
	};
	EFI_PEI_PCI_CFG2_PPI *pci_cfg = NULL;

	EFI_STATUS status = (*services)->LocatePpi(
		services,
		&pci_cfg_guid,
		0,
		NULL,
		(void **)&pci_cfg);
	assert_int_equal(status, EFI_SUCCESS);
	assert_non_null(pci_cfg);

	/* Check that CpuIo is located properly */
	static const EFI_GUID cpu_io_guid = {
		0xe682ac4, 0x6bc, 0x4bb3, { 0xa9, 0xa1, 0xdb, 0x4e, 0x70, 0xda, 0x9f, 0x30 }
	};
	EFI_PEI_CPU_IO_PPI *cpu_io = NULL;

	status = (*services)->LocatePpi(
		services,
		&cpu_io_guid,
		0,
		NULL,
		(void **)&cpu_io);
	assert_int_equal(status, EFI_SUCCESS);
	assert_non_null(cpu_io);

	/* Test MMCFG read/write through PciCfg2 */
	uint8_t mock_ecam[4096];
	memset(mock_ecam, 0, sizeof(mock_ecam));
	/* Set vendor/device ID at offset 0 of bus 0 dev 0 func 0 */
	*(uint32_t *)&mock_ecam[0] = 0x2f008086;
	/* Set a register at offset 0x108 (cpubusno) */
	*(uint32_t *)&mock_ecam[0x108] = 0x0001ff00;

	grantley_pei_set_mmcfg(&state, (uintptr_t)mock_ecam);

	uint32_t val32 = 0;
	UINT64 pci_addr = EFI_PEI_PCI_CFG_ADDRESS(0, 0, 0, 0);
	status = pci_cfg->Read(services, pci_cfg, EfiPeiPciCfgWidthUint32, pci_addr, &val32);
	assert_int_equal(status, EFI_SUCCESS);
	assert_int_equal(val32, 0x2f008086);

	/* Read extended register 0x108 */
	pci_addr = EFI_PEI_PCI_CFG_ADDRESS(0, 0, 0, 0x108);
	status = pci_cfg->Read(services, pci_cfg, EfiPeiPciCfgWidthUint32, pci_addr, &val32);
	assert_int_equal(status, EFI_SUCCESS);
	assert_int_equal(val32, 0x0001ff00);

	/* Write and modify extended register */
	uint32_t new_val = 0x12345678;
	status = pci_cfg->Write(services, pci_cfg, EfiPeiPciCfgWidthUint32, pci_addr, &new_val);
	assert_int_equal(status, EFI_SUCCESS);
	assert_int_equal(*(uint32_t *)&mock_ecam[0x108], 0x12345678);
}

static void test_read_only_variable2_ppi(void **s)
{
	CONST EFI_PEI_SERVICES **services = &state.services_ptr;

	/* Check that ReadOnlyVariable2 is located properly */
	static const EFI_GUID ro_var_guid = {
		0x2ab86ef5, 0xecb5, 0x4134, { 0xb5, 0x56, 0x38, 0x54, 0xca, 0x1f, 0xe1, 0xb4 }
	};
	EFI_PEI_READ_ONLY_VARIABLE2_PPI *ro_var = NULL;

	EFI_STATUS status = (*services)->LocatePpi(
		services,
		&ro_var_guid,
		0,
		NULL,
		(void **)&ro_var);
	assert_int_equal(status, EFI_SUCCESS);
	assert_non_null(ro_var);

	/* When no backing store is attached, variable lookup returns EFI_NOT_FOUND */
	CHAR16 var_name[] = { 'I', 'n', 't', 'e', 'l', 'S', 'e', 't', 'u', 'p', 0 };
	static const EFI_GUID setup_guid = {
		0xec87d643, 0xeba4, 0x4bb5, { 0xa1, 0xe5, 0x3f, 0x3e, 0x36, 0xb2, 0x0d, 0xa9 }
	};
	UINT8 buf[64];
	UINTN buf_size = sizeof(buf);

	status = ro_var->GetVariable(ro_var, var_name, &setup_guid, NULL, &buf_size, buf);
	assert_int_equal(status, EFI_NOT_FOUND);

	/* Attach a mock region device formatted with an EFI variable store */
	uint8_t mock_nvram[0x200];
	memset(mock_nvram, 0xff, sizeof(mock_nvram));
	struct region_device rdev;
	rdev_chain_mem(&rdev, mock_nvram, sizeof(mock_nvram));
	grantley_pei_set_var_store(&state, &rdev);

	/* Formatted empty store still returns EFI_NOT_FOUND cleanly */
	status = ro_var->GetVariable(ro_var, var_name, &setup_guid, NULL, &buf_size, buf);
	assert_int_equal(status, EFI_NOT_FOUND);

	/* Test manual memory overrides through grantley_pei_set_mem_params */
	grantley_pei_set_mem_params(&state, 2133, 15, 15, 15, 35, 1, true);
	buf_size = sizeof(buf);
	status = ro_var->GetVariable(ro_var, var_name, &setup_guid, NULL, &buf_size, buf);
	assert_int_equal(status, EFI_BUFFER_TOO_SMALL);
	assert_int_equal(buf_size, 0x200);

	uint8_t large_buf[0x200];
	buf_size = sizeof(large_buf);
	status = ro_var->GetVariable(ro_var, var_name, &setup_guid, NULL, &buf_size, large_buf);
	assert_int_equal(status, EFI_SUCCESS);
	assert_int_equal(large_buf[0x6e], 0x0b); /* DDR_2133 */
	assert_int_equal(large_buf[0x70], 15);   /* tCL */
	assert_int_equal(large_buf[0x71], 15);   /* tRCD */
	assert_int_equal(large_buf[0x72], 15);   /* tRP */
	assert_int_equal(large_buf[0x73], 35);   /* tRAS */
	assert_int_equal(large_buf[0x74], 1);    /* 1T */
	assert_int_equal(large_buf[0x75], 1);    /* ECC en */
}

static EFI_STATUS mock_peim_entry(
	void *file_handle,
	const struct _EFI_PEI_SERVICES **services)
{
	/* Simulate PEIM completing memory training and calling InstallPeiMemory */
	return (*services)->InstallPeiMemory(services, 0, 0x800000000ULL);
}

static uint8_t mock_pe_buffer[512] __aligned(16);

static void test_run_uncore_peim(void **s)
{
	/* Build a minimal valid PE header that points to mock_peim_entry */
	memset(mock_pe_buffer, 0, sizeof(mock_pe_buffer));

	/* DOS header: 'MZ' and e_lfanew at 0x3c */
	mock_pe_buffer[0] = 'M';
	mock_pe_buffer[1] = 'N'; /* deliberately corrupt to test signature rejection */
	*(uint32_t *)&mock_pe_buffer[0x3c] = 0x80;

	EFI_STATUS status = grantley_run_uncore_peim(mock_pe_buffer, &state);
	assert_int_equal(status, EFI_UNSUPPORTED);

	/* Fix MZ signature */
	mock_pe_buffer[1] = 'Z';

	/* Corrupt PE signature */
	mock_pe_buffer[0x80] = 'N';
	mock_pe_buffer[0x81] = 'O';
	status = grantley_run_uncore_peim(mock_pe_buffer, &state);
	assert_int_equal(status, EFI_UNSUPPORTED);

	/* Set valid PE\0\0 signature */
	mock_pe_buffer[0x80] = 'P';
	mock_pe_buffer[0x81] = 'E';
	mock_pe_buffer[0x82] = '\0';
	mock_pe_buffer[0x83] = '\0';

	/* Test dynamic entry RVA calculation (as seen in QD4: 0x195c0 vs BD4: 0x195a0) */
	intptr_t diff = (intptr_t)mock_peim_entry - (intptr_t)mock_pe_buffer;
	*(int32_t *)&mock_pe_buffer[0x80 + 0x28] = (int32_t)diff;

	status = grantley_run_uncore_peim(mock_pe_buffer, &state);
	assert_int_equal(status, EFI_SUCCESS);
	assert_true(state.dram_initialized);
	assert_int_equal(state.dram_size, 0x800000000ULL);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_pei_services_init, setup),
		cmocka_unit_test_setup(test_ppi_install_locate_reinstall, setup),
		cmocka_unit_test_setup(test_pool_and_memory_helpers, setup),
		cmocka_unit_test_setup(test_create_hob, setup),
		cmocka_unit_test_setup(test_install_pei_memory, setup),
		cmocka_unit_test_setup(test_pci_cfg_and_cpu_io_ppi, setup),
		cmocka_unit_test_setup(test_read_only_variable2_ppi, setup),
		cmocka_unit_test_setup(test_run_uncore_peim, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
