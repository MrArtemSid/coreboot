/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GRANTLEY_SMM_SHIM_H
#define GRANTLEY_SMM_SHIM_H
#include "grantley-init-shim.h"
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define __packed __attribute__((packed))
#define __nonstring __attribute__((nonstring))
#define MiB (1024 * 1024)
#define KiB 1024
#define SMM_DEFAULT_SIZE 0x10000
#define SMM_SAVE_STATE_BEGIN(x) (x)
#define MTRR_CAP_MSR 0xfe
#define MTRR_CAP_SMRR (1 << 11)
#define SMRR_PHYSBASE_MSR 0x1f2
#define SMRR_PHYSMASK_MSR 0x1f3
#define PCI_VENDOR_ID 0
#define PCI_DEV(bus, dev, fn) (((bus) << 20) | ((dev) << 15) | ((fn) << 12))
#define SMM_SUBREGION_HANDLER 0
#define SMM_SUBREGION_CHIPSET 1
struct ied_header { char __nonstring signature[10]; u32 size; u8 reserved[34]; } __packed;
uint16_t pci_s_read_config16(uint32_t dev, unsigned int reg);
uint32_t pci_s_read_config32(uint32_t dev, unsigned int reg);
uint8_t pci_s_read_config8(uint32_t dev, unsigned int reg);
void pci_s_write_config8(uint32_t dev, unsigned int reg, uint8_t value);
void pci_s_write_config32(uint32_t dev, unsigned int reg, uint32_t value);
void smm_region(uintptr_t *base, size_t *size);
int smm_subregion(int id, uintptr_t *base, size_t *size);
void smm_southbridge_clear_state(void);
void global_smi_enable(void);
#endif
