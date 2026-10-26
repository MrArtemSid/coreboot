/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <console/console.h>
#include <delay.h>
#include <stdint.h>
#include <types.h>
#include "numlock_beacon.h"

#define KBD_DATA	0x60
#define KBD_STATUS	0x64
#define KBD_OBF		(1 << 0)
#define KBD_IBF		(1 << 1)

#define KBC_CMD_KBD_ENABLE	0xae
#define KBD_CMD_SET_LEDS	0xed
#define KBD_REPLY_ACK		0xfa

struct beacon_rate {
	unsigned int half_period_ms;
	unsigned int blinks;
};

/* Later stage -> slower blinking. */
static const struct beacon_rate rates[] = {
	[BEACON_BOOTBLOCK] = { 100, 8 },
	[BEACON_ROMSTAGE]  = { 300, 4 },
	[BEACON_RAMSTAGE]  = { 700, 3 },
};

static enum beacon_stage current_stage = BEACON_BOOTBLOCK;

static int kbc_wait_ibf_clear(unsigned int ms)
{
	for (unsigned int i = 0; i < ms * 10; i++) {
		if (!(inb(KBD_STATUS) & KBD_IBF))
			return 0;
		udelay(100);
	}
	return -1;
}

static int kbc_read(unsigned int ms, uint8_t *val)
{
	for (unsigned int i = 0; i < ms * 10; i++) {
		if (inb(KBD_STATUS) & KBD_OBF) {
			*val = inb(KBD_DATA);
			return 0;
		}
		udelay(100);
	}
	return -1;
}

static void kbc_flush(void)
{
	uint8_t junk;

	for (int i = 0; i < 16 && !kbc_read(1, &junk); i++)
		;
}

static int kbd_send(uint8_t byte)
{
	uint8_t reply;

	if (kbc_wait_ibf_clear(20))
		return -1;
	outb(byte, KBD_DATA);
	/* Skip anything that is not the ACK (e.g. late BAT 0xaa). */
	for (int i = 0; i < 4; i++) {
		if (kbc_read(30, &reply))
			return -1;
		if (reply == KBD_REPLY_ACK)
			return 0;
	}
	return -1;
}

static bool kbc_ready;

void numlock_beacon_set(uint8_t leds)
{
	/* No keyboard answered: do not burn time on timeouts. */
	if (!kbc_ready)
		return;
	kbc_flush();
	if (kbd_send(KBD_CMD_SET_LEDS))
		return;
	kbd_send(leds & 7);
}

static void kbc_prepare(void)
{
	if (kbc_ready)
		return;

	/* The keyboard may still be in its power-on BAT; give it up to ~1 s. */
	for (int tries = 0; tries < 10; tries++) {
		if (!kbc_wait_ibf_clear(20)) {
			outb(KBC_CMD_KBD_ENABLE, KBD_STATUS);
			kbc_flush();
			if (!kbd_send(KBD_CMD_SET_LEDS) && !kbd_send(0)) {
				kbc_ready = true;
				return;
			}
		}
		mdelay(100);
	}
}

static void blink(const struct beacon_rate *r, uint8_t extra, unsigned int count)
{
	for (unsigned int i = 0; i < count; i++) {
		numlock_beacon_set(BEACON_LED_NUM | extra);
		mdelay(r->half_period_ms);
		numlock_beacon_set(extra);
		mdelay(r->half_period_ms);
	}
}

void numlock_beacon_stage(enum beacon_stage stage)
{
	current_stage = stage;
	kbc_prepare();
	blink(&rates[stage], 0, rates[stage].blinks);
	/* Leave NumLock lit: "stage reached and still running". */
	numlock_beacon_set(BEACON_LED_NUM);
}

void die_notify(void)
{
	kbc_prepare();
	for (;;)
		blink(&rates[current_stage], BEACON_LED_CAPS, 1);
}
