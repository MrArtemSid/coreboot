/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Ramoops console mirror + fatal-error reset for msm89x7.
 *
 * The console log is mirrored into the ramoops console buffer that
 * lk2nd reserves at the top of its flash scratch area, in the pstore
 * persistent_ram format without ECC (see lk2nd/lk2nd/ramoops/
 * ramoops.c): a 12-byte header { sig 'DBGC', start, size } followed by
 * the raw log bytes. After a reset lk2nd serves the buffer with
 * "fastboot oem ramoops console" / "fastboot get_staged", which turns
 * any coreboot crash into a readable log on the host.
 *
 * With MSM89X7_DEBUG_WATCHDOG, die() sets the restart reason to fastboot
 * and arms the apps watchdog for a short bite, so a fatal error returns
 * the device to the lk2nd fastboot prompt instead of hanging. The
 * watchdog is also armed with a longer timeout during the boot, so an
 * endless loop is caught as well.
 */

#include <arch/barrier.h>
#include <arch/cache.h>
#include <arch/stages.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <console/ramoops_console.h>
#include <device/mmio.h>
#include <halt.h>
#include <rules.h>
#include <stdint.h>
#include <soc/addressmap.h>
#include <soc/ramoops.h>

#define RAMOOPS_SIG	0x43474244 /* 'DBGC' */

struct ramoops_header {
	uint32_t sig;
	uint32_t start;	/* write offset within data[] */
	uint32_t size;	/* valid bytes in data[] */
	uint8_t	 data[];
};

static struct ramoops_header *ramoops_hdr(void)
{
	return (struct ramoops_header *)(uintptr_t)CONFIG_MSM89X7_RAMOOPS_ADDR;
}

#if CONFIG(MSM89X7_DEBUG_WATCHDOG)
static void *ramoops_wdt_base(void)
{
	return (void *)MSM89X7_WDT_BASE;
}

void msm89x7_wdt_pet(void)
{
	write32(ramoops_wdt_base() + 0x04, 1);	/* WDT_RST */
}

void msm89x7_wdt_arm(unsigned int ms)
{
	uint32_t ticks = (uint64_t)ms * 32768 / 1000;

	ticks = MAX(1, MIN(ticks, 0xfffff));
	write32(ramoops_wdt_base() + 0x08, 0);	/* WDT_EN */
	write32(ramoops_wdt_base() + 0x04, 1);	/* WDT_RST */
	/* BARK (an interrupt TZ traps into a ramdump/EDL path) must never
	 * fire: keep it past the counter range so BITE always resets. */
	write32(ramoops_wdt_base() + 0x10, 0xfffff);	/* WDT_BARK_TIME */
	write32(ramoops_wdt_base() + 0x14, ticks);	/* WDT_BITE_TIME */
	write32(ramoops_wdt_base() + 0x08, 1);	/* WDT_EN */
}

void msm89x7_wdt_disable(void)
{
	write32(ramoops_wdt_base() + 0x08, 0);	/* WDT_EN */
	dsb();
}
#endif

static void ramoops_reset_header(void)
{
	struct ramoops_header *h = ramoops_hdr();

	h->sig = RAMOOPS_SIG;
	h->start = 0;
	h->size = 0;
}

void __ramoops_init(void)
{
	/* The bootblock starts a new log; later stages keep appending to
	 * what the previous stage wrote. */
	if (ENV_BOOTBLOCK)
		ramoops_reset_header();
	else if (read32(&ramoops_hdr()->sig) != RAMOOPS_SIG)
		ramoops_reset_header();
}

void __ramoops_tx_byte(unsigned char data)
{
	struct ramoops_header *h = ramoops_hdr();
	const uint32_t cap = CONFIG_MSM89X7_RAMOOPS_SIZE - sizeof(*h);

	/* Console output means progress, also without the display console. */
	msm89x7_wdt_pet();

	/* A byte can arrive before __ramoops_init() ran (early fault paths
	 * on a buffer full of 0xffffffff); recover the header instead of
	 * losing the whole log to an invalid signature. */
	if (read32(&h->sig) != RAMOOPS_SIG)
		ramoops_reset_header();

	if (h->start >= cap) {
		/* Ring wrap: keep writing from the start, the reader
		 * gets the last cap bytes rotated. */
		h->start = 0;
	}

	h->data[h->start++] = data;
	h->size = MIN(h->size + 1, cap);
}

void __ramoops_tx_flush(void)
{
}

#if CONFIG(MSM89X7_DEBUG_WATCHDOG)
void die_notify(void)
{
	/* 0x77665500 = FASTBOOT_MODE, read by lk2nd's check_reboot_mode()
	 * from IMEM after the reset. */
	write32((void *)MSM89X7_RESTART_REASON_ADDR,
		MSM89X7_RESTART_REASON_FASTBOOT);

	/* Belt and braces: the region is mapped non-cacheable (see mmu.c)
	 * and most stages write it with the MMU off, but a die() from an
	 * unexpectedly cacheable context must still leave a readable log. */
	dcache_clean_by_mva(ramoops_hdr(), CONFIG_MSM89X7_RAMOOPS_SIZE);
	dsb();
	isb();

	msm89x7_wdt_arm(200);

	for (;;)
		;
}
#endif
