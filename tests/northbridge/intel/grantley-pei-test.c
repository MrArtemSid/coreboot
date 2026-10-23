/* SPDX-License-Identifier: GPL-2.0-only */

#include <string.h>
#include <tests/test.h>
#include <device/pci_type.h>
#include <northbridge/intel/grantley/memmap.h>
#include <commonlib/region.h>
#undef NULL
#include <northbridge/intel/grantley/uncore_runner/pei_services.h>

static struct grantley_uncore_state state;
uint8_t *const pci_mmconf = NULL;
void post_code(uint8_t value) { }
void system_reset(void) { __builtin_trap(); }
struct route_register {
	uint32_t dev;
	uint16_t reg;
	uint32_t value;
};
static struct route_register route_registers[32];
static size_t route_count;
static bool route_present;
static uint32_t route_signature = 0x306f2;

uint32_t grantley_test_route_read32(uint32_t dev, uint16_t reg, uintptr_t base);
void grantley_test_route_write32(uint32_t dev, uint16_t reg, uintptr_t base, uint32_t value);
uint32_t grantley_test_route_cpuid(void);

uint32_t grantley_test_route_read32(uint32_t dev, uint16_t reg, uintptr_t base)
{
	if (!route_present)
		return 0xffffffff;
	for (size_t n = route_count; n > 0; n--)
		if (route_registers[n - 1].dev == dev && route_registers[n - 1].reg == reg)
			return route_registers[n - 1].value;
	return reg == 0 ? 0x2f008086 : 0;
}

void grantley_test_route_write32(uint32_t dev, uint16_t reg, uintptr_t base, uint32_t value)
{
	assert_true(route_count < ARRAY_SIZE(route_registers));
	route_registers[route_count++] = (struct route_register) { dev, reg, value };
}

uint32_t grantley_test_route_cpuid(void)
{
	return route_signature;
}

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
	route_present = false;
	route_count = 0;
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
	EFI_HOB_HANDOFF_INFO_TABLE *hit = (void *)hob_buffer;
	assert_int_equal(hit->EfiEndOfHobList, (uintptr_t)next_hdr);
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

	state.mmcfg_base = (uintptr_t)mock_ecam;
	state.mmcfg_enabled = TRUE;

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

	/* SSA BIOS Services must not resolve to a variable-access function table. */
	static const EFI_GUID ro_var1_guid = {
		0xbae29d7b, 0x89bb, 0x4223, { 0xaf, 0x76, 0x96, 0xd0, 0xb3, 0x24, 0x9b, 0x36 }
	};
	EFI_PEI_READ_ONLY_VARIABLE2_PPI *ro_var1 = NULL;
	status = (*services)->LocatePpi(
		services,
		&ro_var1_guid,
		0,
		NULL,
		(void **)&ro_var1);
	assert_int_equal(status, EFI_NOT_FOUND);

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
	assert_int_equal(status, EFI_UNSUPPORTED);
	assert_false(state.dram_initialized);
}

static void add_memory_resource(uint32_t type, uint64_t base, uint64_t size)
{
	EFI_HOB_RESOURCE_DESCRIPTOR *resource;
	const EFI_PEI_SERVICES **services = &state.services_ptr;
	assert_int_equal((*services)->CreateHob(services, EFI_HOB_TYPE_RESOURCE_DESCRIPTOR,
		 sizeof(*resource), (void **)&resource), EFI_SUCCESS);
	memset((uint8_t *)resource + sizeof(resource->Header), 0,
		 sizeof(*resource) - sizeof(resource->Header));
	resource->ResourceType = type;
	resource->ResourceAttribute = EFI_RESOURCE_ATTRIBUTE_PRESENT |
		 EFI_RESOURCE_ATTRIBUTE_INITIALIZED;
	resource->PhysicalStart = base;
	resource->ResourceLength = size;
}

static void add_smram_hob(void)
{
	struct smram_hob {
		EFI_HOB_GUID_TYPE hob;
		uint32_t count;
		uint32_t padding;
		uint64_t physical, cpu, size, flags;
	} *smram;
	const EFI_PEI_SERVICES **services = &state.services_ptr;
	assert_int_equal((*services)->CreateHob(services, EFI_HOB_TYPE_GUID_EXTENSION,
		 sizeof(*smram), (void **)&smram), EFI_SUCCESS);
	smram->hob.Name = (EFI_GUID) { 0x6dadf1d1, 0xd4cc, 0x4910,
		{ 0xbb, 0x6e, 0x82, 0xb1, 0xfd, 0x80, 0xff, 0x3d } };
	smram->count = 1;
	smram->padding = 0;
	smram->physical = 0x7f000000;
	smram->cpu = smram->physical;
	smram->size = 0x1000000;
	smram->flags = 0;
}

static void test_memory_test_ppi(void **s);

static void test_post_peim_memory_map(void **s)
{
	struct grantley_memory_map map;
	add_memory_resource(EFI_RESOURCE_SYSTEM_MEMORY, 0, 0xa0000);
	add_memory_resource(EFI_RESOURCE_SYSTEM_MEMORY, 0x100000, 0x7ff00000);
	add_memory_resource(EFI_RESOURCE_MEMORY_RESERVED, 0x7f000000, 0x1000000);
	add_memory_resource(EFI_RESOURCE_SYSTEM_MEMORY, 0x100000000ULL, 0x100000000ULL);
	add_smram_hob();
	EFI_HOB_MEMORY_ALLOCATION *allocation;
	const EFI_PEI_SERVICES **services = &state.services_ptr;
	assert_int_equal((*services)->CreateHob(services, EFI_HOB_TYPE_MEMORY_ALLOCATION,
		 sizeof(*allocation), (void **)&allocation), EFI_SUCCESS);
	memset(&allocation->AllocDescriptor, 0, sizeof(allocation->AllocDescriptor));
	allocation->AllocDescriptor.MemoryBaseAddress = 0x100100000ULL;
	allocation->AllocDescriptor.MemoryLength = 0x200000;
	allocation->AllocDescriptor.MemoryType = EfiReservedMemoryType;
	state.dram_initialized = TRUE;
	state.dram_base = 0x70000000;
	state.dram_size = 0x4000000;
	assert_int_equal(grantley_pei_memory_map(&state, 0x80000000, 0, 2, &map),
			 EFI_SUCCESS);
	assert_int_equal(map.count, 6);
	assert_int_equal(map.ranges[4].type, GRANTLEY_MEMORY_RESERVED_RAM);
	assert_int_equal(map.ranges[4].base, 0x100100000ULL);
	assert_int_equal(map.ranges[1].base, 0x100000);
	assert_int_equal(map.ranges[1].size, 0x7ef00000);
	assert_int_equal(map.ranges[2].type, GRANTLEY_MEMORY_RESERVED_RAM);
	assert_int_equal(map.smram_base, 0x7f000000);
	assert_int_equal(map.smram_size, 0x1000000);
	state.dram_initialized = FALSE;
	assert_int_equal(grantley_pei_memory_map(&state, 0x80000000, 0, 2, &map),
			 EFI_DEVICE_ERROR);
	state.dram_initialized = TRUE;
	state.dram_base = 0x7f000000;
	assert_int_equal(grantley_pei_memory_map(&state, 0x80000000, 0, 2, &map),
			 EFI_DEVICE_ERROR);
	state.dram_base = 0x70000000;
	assert_int_equal(grantley_pei_memory_map(&state, 0xffffffff, 0, 2, &map),
			 EFI_DEVICE_ERROR);
	test_memory_test_ppi(s);
	assert_int_equal(grantley_pei_memory_map(&state, 0x80000000, 0, 2, &map),
			 EFI_DEVICE_ERROR);
}

static void test_platform_hob_storage_required(void **s)
{
	assert_int_equal(grantley_pei_services_init(&state, pool, sizeof(pool),
		 hob_buffer, sizeof(EFI_HOB_HANDOFF_INFO_TABLE) + sizeof(EFI_HOB_GENERIC_HEADER)),
		 EFI_BUFFER_TOO_SMALL);
}

static void test_hob_length_overflow(void **s)
{
	const EFI_PEI_SERVICES **services = &state.services_ptr;
	void *hob = (void *)0x1234;
	UINTN used = state.hob_allocated;
	assert_int_equal((*services)->CreateHob(services, EFI_HOB_TYPE_GUID_EXTENSION,
					       65535, &hob), EFI_INVALID_PARAMETER);
	assert_int_equal(state.hob_allocated, used);
	assert_ptr_equal(hob, (void *)0x1234);
}

static void test_page_allocation_alignment(void **s)
{
	static uint8_t large_pool[16384] __aligned(4096);
	state.pool_base = large_pool + 8;
	state.pool_size = sizeof(large_pool) - 8;
	const EFI_PEI_SERVICES **services = &state.services_ptr;
	void *small;
	EFI_PHYSICAL_ADDRESS page = 0;
	assert_int_equal((*services)->AllocatePool(services, 8, &small), EFI_SUCCESS);
	assert_int_equal((*services)->AllocatePages(services, EfiBootServicesData,
						 1, &page), EFI_SUCCESS);
	assert_int_equal(page & 4095, 0);
	assert_true(page >= (uintptr_t)small + 8);
	UINTN allocated = state.pool_allocated;
	assert_int_equal((*services)->AllocatePages(services, EfiBootServicesData,
						 (UINTN)-1, &page), EFI_OUT_OF_RESOURCES);
	assert_int_equal(state.pool_allocated, allocated);
	assert_int_equal((*services)->AllocatePool(services, (UINTN)-1, &small),
			 EFI_OUT_OF_RESOURCES);
	assert_int_equal(state.pool_allocated, allocated);
}

static void test_mmcfg_routing_sequence(void **s)
{
	route_present = true;
	grantley_pei_set_mmcfg(&state, 0x80000000UL);
	assert_true(state.mmcfg_enabled);
	assert_true(route_count >= 7);
	/* Only the first UBOX access may use the reset-default bus 1. */
	assert_int_equal(route_registers[0].dev, PCI_DEV(1, 16, 7));
	assert_int_equal(route_registers[0].reg, 0xd0);
	assert_int_equal(route_registers[0].value, 0x8000ff00);
	assert_int_equal(route_registers[1].dev, PCI_DEV(0xff, 15, 5));
	assert_int_equal(route_registers[1].reg, 0xe4);
	assert_int_equal(route_registers[1].value, 0);
	assert_int_equal(grantley_test_route_read32(PCI_DEV(0, 5, 0), 0x98, 0),
			 0x8c000000);
	assert_int_equal(grantley_test_route_read32(PCI_DEV(0, 5, 0), 0x108, 0),
			 0x0001ff00);
}

static void test_mmcfg_routing_failure(void **s)
{
	grantley_pei_set_mmcfg(&state, 0x80000000UL);
	assert_false(state.mmcfg_enabled);
}

static void test_platform_info_ppi(void **s)
{
	static const EFI_GUID guid = {
		0x1e2acc41, 0xe26a, 0x483d,
		{ 0xaf, 0xc7, 0xa0, 0x56, 0xc3, 0x4e, 0x08, 0x7b }
	};
	const EFI_PEI_SERVICES **services = &state.services_ptr;
	void *info = NULL;
	assert_int_equal((*services)->LocatePpi(services, &guid, 0, NULL, &info),
			 EFI_SUCCESS);
	assert_non_null(info);
	EFI_HOB_GUID_TYPE *hob = (void *)(hob_buffer + sizeof(EFI_HOB_HANDOFF_INFO_TABLE));
	assert_ptr_equal(info, hob + 1);
	assert_memory_equal(&hob->Name, &guid, sizeof(guid));
	assert_int_equal(*(uint32_t *)((uint8_t *)info + 0x10), 0x54495024);
}

static void test_memory_test_ppi(void **s)
{
	static const EFI_GUID guid = {
		0xb6ec423c, 0x21d2, 0x490d,
		{ 0x85, 0xc6, 0xdd, 0x58, 0x64, 0xea, 0xa6, 0x74 }
	};
	struct test_ppi {
		EFI_STATUS (EFIAPI *test)(const EFI_PEI_SERVICES **, void *,
			EFI_PHYSICAL_ADDRESS, UINT64, UINT32, EFI_PHYSICAL_ADDRESS *);
	} *ppi;
	const EFI_PEI_SERVICES **services = &state.services_ptr;
	EFI_PHYSICAL_ADDRESS error = 0xdeadbeef;
	assert_int_equal((*services)->LocatePpi(services, &guid, 0, NULL,
					     (void **)&ppi), EFI_SUCCESS);
	memset(pool, 0xa5, sizeof(pool));
	assert_int_equal(ppi->test(services, ppi, (uintptr_t)pool, sizeof(pool),
				 3, &error), EFI_SUCCESS);
	assert_int_equal(error, 0);
	for (size_t i = 0; i < sizeof(pool); i++)
		assert_int_equal(pool[i], 0xa5);
	assert_int_equal(ppi->test(services, ppi, 0, 4096, 99, &error),
			 EFI_INVALID_PARAMETER);
	assert_int_equal(ppi->test(services, ppi, 0x100000000ULL, 4096, 1, &error),
			 EFI_UNSUPPORTED);
	assert_int_equal(ppi->test(services, ppi, 0x100000000ULL, 4096, 0, &error),
			 EFI_SUCCESS);
}

static void test_memory_size_ppi(void **s)
{
	static const EFI_GUID guid = {
		0x9a7ef41e, 0xc140, 0x4bd1,
		{ 0xb8, 0x84, 0x1e, 0x11, 0x24, 0x0b, 0x4c, 0xe6 }
	};
	struct size_ppi {
		EFI_STATUS (EFIAPI *get)(const EFI_PEI_SERVICES **, void *, UINT64 *);
	} *ppi;
	const EFI_PEI_SERVICES **services = &state.services_ptr;
	assert_int_equal((*services)->LocatePpi(services, &guid, 0, NULL,
					     (void **)&ppi), EFI_SUCCESS);
	UINT64 size = 0xdeadbeef;
	assert_int_equal(ppi->get(services, ppi, &size), EFI_SUCCESS);
	assert_int_equal(size, 64ULL * 1024 * 1024);
	assert_int_equal(ppi->get(services, ppi, NULL), EFI_INVALID_PARAMETER);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_post_peim_memory_map, setup),
		cmocka_unit_test_setup(test_platform_hob_storage_required, setup),
		cmocka_unit_test_setup(test_hob_length_overflow, setup),
		cmocka_unit_test_setup(test_page_allocation_alignment, setup),
		cmocka_unit_test_setup(test_mmcfg_routing_sequence, setup),
		cmocka_unit_test_setup(test_mmcfg_routing_failure, setup),
		cmocka_unit_test_setup(test_platform_info_ppi, setup),
		cmocka_unit_test_setup(test_memory_test_ppi, setup),
		cmocka_unit_test_setup(test_memory_size_ppi, setup),
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
