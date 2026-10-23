/* SPDX-License-Identifier: GPL-2.0-only */
#include "grantley-smm-shim.h"
#include <cpu/intel/grantley/smm.c>
#include <cpu/intel/grantley/smm_finalize.c>

static msr_t registers[0x300];
static uint32_t feature_control, hw_base = 0x7f800000, hw_limit = 0x7ff00000;
static uint8_t smram_control = SMRAM_OPEN;
static uint16_t vendor = 0x8086;
static bool drop_writes, expect_die;
static int enabled, cleared;
static jmp_buf halt_target;
static uint8_t save_window[SMM_DEFAULT_SIZE];

msr_t rdmsr(unsigned int reg) { assert(reg < 0x300); return registers[reg]; }
void wrmsr(unsigned int reg, msr_t value)
{
	assert(reg < 0x300);
	if (!drop_writes)
		registers[reg] = value;
}
uint16_t pci_s_read_config16(uint32_t dev, unsigned int reg)
{
	assert(dev == SAD_DEV || dev == UBOX_DFX_DEV);
	assert(reg == PCI_VENDOR_ID);
	return vendor;
}
uint32_t pci_s_read_config32(uint32_t dev, unsigned int reg)
{
	if (dev == IIO_DEV) {
		assert(reg == 0xa8 || reg == 0xac);
		return reg == 0xa8 ? hw_base : hw_limit;
	}
	assert(dev == UBOX_DFX_DEV && reg == SMM_FEATURE_CONTROL);
	return feature_control;
}
uint8_t pci_s_read_config8(uint32_t dev, unsigned int reg)
{
	assert(dev == SAD_DEV && reg == SAD_SMRAMC);
	return smram_control;
}
void pci_s_write_config8(uint32_t dev, unsigned int reg, uint8_t value)
{
	assert(dev == SAD_DEV && reg == SAD_SMRAMC);
	if (!drop_writes)
		smram_control = value;
}
void pci_s_write_config32(uint32_t dev, unsigned int reg, uint32_t value)
{
	assert(dev == UBOX_DFX_DEV && reg == SMM_FEATURE_CONTROL);
	if (!drop_writes)
		feature_control = value;
}
void smm_region(uintptr_t *base, size_t *size) { *base = 0x7f800000; *size = 8 * MiB; }
int smm_subregion(int id, uintptr_t *base, size_t *size)
{
	*base = 0x7f800000;
	*size = id == SMM_SUBREGION_HANDLER ? 3 * MiB : 0;
	return 0;
}
void smm_southbridge_clear_state(void) { cleared++; }
void global_smi_enable(void)
{
	assert((smram_control & (SMRAM_OPEN | SMRAM_ENABLE | SMRAM_LOCK)) ==
	       (SMRAM_ENABLE | SMRAM_LOCK));
	enabled++;
}
void printk(int level, const char *format, ...) { (void)level; (void)format; }
_Noreturn void die(const char *message)
{
	(void)message;
	assert(expect_die);
	longjmp(halt_target, 1);
}
#define EXPECT_HALT(call) do { \
	expect_die = true; \
	if (!setjmp(halt_target)) { call; abort(); } \
	expect_die = false; \
} while (0)

int main(void)
{
	uintptr_t base;
	size_t size, state_size;
	registers[MTRR_CAP_MSR].lo = MTRR_CAP_SMRR;
	grantley_get_smm_info(&base, &size, &state_size);
	assert(base == 0x7f800000 && size == 3 * MiB);
	assert(state_size == sizeof(em64t101_smm_state_save_area_t));
	grantley_smm_initialize();
	assert(cleared == 1);
	grantley_smm_relocation_handler(0, (uintptr_t)save_window, 0x7fa00000);
	em64t101_smm_state_save_area_t *state =
		(void *)(save_window + sizeof(save_window) - state_size);
	assert(state->smbase == 0x7fa00000 && state->iedbase == UINT32_MAX);
	assert(registers[SMRR_PHYSBASE_MSR].raw == 0x7f800006);
	assert(registers[SMRR_PHYSMASK_MSR].raw == 0xff800800);

	hw_base = 0x7f900000;
	EXPECT_HALT(grantley_get_smm_info(&base, &size, &state_size));
	hw_base = 0x7f800000;
	hw_limit = UINT32_MAX;
	EXPECT_HALT(grantley_get_smm_info(&base, &size, &state_size));
	hw_limit = 0x7ff00000;
	feature_control = SMM_SAVE_STATE_MSR;
	EXPECT_HALT(grantley_get_smm_info(&base, &size, &state_size));
	feature_control = 0;
	vendor = 0xffff;
	EXPECT_HALT(grantley_get_smm_info(&base, &size, &state_size));
	vendor = 0x8086;
	registers[MTRR_CAP_MSR].lo = 0;
	EXPECT_HALT(grantley_get_smm_info(&base, &size, &state_size));
	EXPECT_HALT(grantley_smm_relocation_handler(1, (uintptr_t)save_window, 0x7fa00400));
	registers[MTRR_CAP_MSR].lo = MTRR_CAP_SMRR;

	drop_writes = true;
	registers[SMRR_PHYSMASK_MSR].raw = 0;
	EXPECT_HALT(grantley_smm_relocation_handler(1, (uintptr_t)save_window, 0x7fa00400));
	EXPECT_HALT(grantley_smm_lock());
	assert(!enabled);
	drop_writes = false;
	grantley_smm_lock();
	assert(enabled == 1);
	registers[GRANTLEY_MSR_LT_CONTROL].raw = 1ULL << 40;
	grantley_cpu_finalize(NULL);
	assert(registers[GRANTLEY_MSR_LT_CONTROL].raw == ((1ULL << 40) | 1));

	/* Unsupported feature must not access the server CSR or a client MSR. */
	enable_smm_code_access_check();
	assert(!feature_control);
	registers[IA32_SMM_MCA_CAP].hi = SMM_CODE_CHECK_CAP_HI;
	enable_smm_code_access_check();
	assert(feature_control == (SMM_CODE_CHECK | SMM_FEATURE_LOCK));
	puts("Grantley SMM callbacks: PASS");
	return 0;
}
