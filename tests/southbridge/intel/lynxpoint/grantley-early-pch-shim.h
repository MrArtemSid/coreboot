/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GRANTLEY_EARLY_PCH_SHIM_H
#define GRANTLEY_EARLY_PCH_SHIM_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define SOUTHBRIDGE_INTEL_LYNXPOINT_PCH_H
#define SOUTHBRIDGE_INTEL_LYNXPOINT_CHIP_H
#define CONFIG(x) CONFIG_##x
#define CONFIG_INTEL_LYNXPOINT_LP 0
#ifndef CONFIG_NORTHBRIDGE_INTEL_GRANTLEY
#define CONFIG_NORTHBRIDGE_INTEL_GRANTLEY 1
#endif
#ifndef ENV_ROMSTAGE
#define ENV_ROMSTAGE 1
#endif
#define ENV_SEPARATE_ROMSTAGE ENV_ROMSTAGE
#define CONFIG_FIXED_RCBA_MMIO_BASE 0xfed1c000
#define CONFIG_PC_CMOS_BASE_PORT_BANK0 0x70
#define __weak __attribute__((weak))
#define BIT(x) (1U << (x))
#define printk(...) do { } while (0)
#define PCH_LPC_DEV 0xf8
#define PCI_VENDOR_ID 0
#define PCI_DEVICE_ID 2
#define PCI_VID_INTEL 0x8086
#define PCI_DID_INTEL_WBG_LPC_C610 0x8d40
#define PCI_DID_INTEL_WBG_LPC_C612 0x8d44
#define PCI_DID_INTEL_WBG_LPC_X99 0x8d4a
#define PCI_DID_INTEL_WBG_LPC_X99_ALT 0x8d50
#define RCBA 0xf0
#define PMBASE 0x40
#define DEFAULT_PMBASE 0x500
#define ACPI_CNTL 0x44
#define ACPI_EN 0x80
#define GPIOBASE 0x48
#define DEFAULT_GPIOBASE 0x480
#define GPIO_CNTL 0x4c
#define LPC_IO_DEC 0x80
#define LPC_EN 0x82
#define LPC_GEN1_DEC 0x84
#define LPC_GEN2_DEC 0x88
#define LPC_GEN3_DEC 0x8c
#define LPC_GEN4_DEC 0x90
#define CNF1_LPC_EN 1
#define CNF2_LPC_EN 2
#define GAMEL_LPC_EN 4
#define COMA_LPC_EN 8
#define KBC_LPC_EN 16
#define MC_LPC_EN 32
#define GCS 0x3410
#define PRSTS 0x3310
#define GEN_PMCON_3 0xa4
#define RTC_BATTERY_DEAD 4
#define RTC_REG_A 10
#define RTC_BASE_PORT_BANK0 0x70
#define RTC_REG_B 11
#define RTC_DIV_CTL 0x70
#define RTC_DIV_RESET1 0x60
#define RTC_REF_CLCK_32KHZ 0x20
#define RTC_SET 0x80
#define OIC 0x31fe
#define FD 0x3418
#define PCH_DISABLE_ALWAYS 0x00000002
static u32 rcba_space[0x4000 / 4];
#define RCBA32(r) rcba_space[(r) / 4]
#define RCBA16(r) (*(u16 *)((u8 *)rcba_space + (r)))
#define RCBA32_OR(r, v) (RCBA32(r) |= (v))
#define RCBA32_AND_OR(r, a, o) (RCBA32(r) = (RCBA32(r) & (a)) | (o))
struct device { void *chip_info; };
typedef struct { u32 gen1_dec, gen2_dec, gen3_dec, gen4_dec; } pch_config_t;
enum pch_platform_type { PCH_TYPE_ULT, PCH_TYPE_MOBILE, PCH_TYPE_DESKTOP };
static const int mainboard_gpio_map = 0;
static u8 pci[256], rtc[128], rtc_index;
static u8 rtc_writes[8], rtc_registers[8];
static unsigned int rtc_write_count, io_count, mmio_writes;
static bool at_smbus;
static u32 expected_prsts;
void early_pch_init_grantley(void);
static u8 expected_rtc_a, expected_rtc_b;
static inline u8 pci_read_config8(unsigned int d, unsigned int r) { return pci[r]; }
static inline u16 pci_read_config16(unsigned int d, unsigned int r)
{ u16 v; memcpy(&v, pci + r, 2); return v; }
static inline u32 pci_read_config32(unsigned int d, unsigned int r)
{ u32 v; memcpy(&v, pci + r, 4); return v; }
static inline void pci_write_config8(unsigned int d, unsigned int r, u8 v) { pci[r] = v; }
static inline void pci_write_config16(unsigned int d, unsigned int r, u16 v)
{ memcpy(pci + r, &v, 2); }
static inline void pci_write_config32(unsigned int d, unsigned int r, u32 v)
{ memcpy(pci + r, &v, 4); }
static inline const struct device *pcidev_on_root(int d, int f) { return NULL; }
static inline void setup_pch_gpios(const void *m) { }
static inline bool pch_is_lp(void) { return false; }
static inline void write_pmbase16(unsigned int r, u16 v) { }
static inline u32 read32(const volatile void *p)
{ return rcba_space[((uintptr_t)p - CONFIG_FIXED_RCBA_MMIO_BASE) / 4]; }
static inline void write32(volatile void *p, u32 v)
{
 unsigned int r = (uintptr_t)p - CONFIG_FIXED_RCBA_MMIO_BASE;
 assert(!at_smbus);
 mmio_writes++;
 if (r == PRSTS) RCBA32(PRSTS) &= ~v;
 else RCBA32(r) = v;
}
static inline u8 inb(unsigned int port)
{
 assert(++io_count < 32);
 if (port == 0x70) { assert(RCBA32(GCS) & BIT(4)); return rtc_index; }
 assert(port == 0x71);
 return rtc[rtc_index & 0x7f];
}
static inline void outb(u8 v, unsigned int port)
{
 assert(++io_count < 32);
 assert(!at_smbus);
 if (port == 0x70) { rtc_index = v; return; }
 assert(port == 0x71 && rtc_write_count < 8);
 rtc_registers[rtc_write_count] = rtc_index & 0x7f;
 rtc_writes[rtc_write_count++] = v;
 rtc[rtc_index & 0x7f] = v;
}
static inline void enable_smbus(void)
{
 assert(pci_read_config32(PCH_LPC_DEV, PMBASE) == (DEFAULT_PMBASE | 1));
 assert(pci_read_config8(PCH_LPC_DEV, ACPI_CNTL) & ACPI_EN);
 assert(rtc[RTC_REG_A] == expected_rtc_a);
 assert(rtc[RTC_REG_B] == expected_rtc_b);
 assert(RCBA32(PRSTS) == expected_prsts);
 at_smbus = true;
}
#endif
