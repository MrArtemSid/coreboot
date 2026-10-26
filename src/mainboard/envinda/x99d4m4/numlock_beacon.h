/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_ENVINDA_X99D4M4_NUMLOCK_BEACON_H
#define MAINBOARD_ENVINDA_X99D4M4_NUMLOCK_BEACON_H

#include <stdint.h>

/*
 * PS/2 keyboard LED stage beacon (no POST display / serial on this board).
 * Each stage blinks NumLock, the later the stage the slower:
 *   bootblock: fast, romstage: medium, ramstage: slow.
 * CapsLock is lit while the vendor uncore blob (memory training) runs.
 * die() blinks NumLock+CapsLock forever at the current stage rate.
 */
enum beacon_stage {
	BEACON_BOOTBLOCK = 0,
	BEACON_ROMSTAGE,
	BEACON_RAMSTAGE,
};

#define BEACON_LED_SCROLL	(1 << 0)
#define BEACON_LED_NUM		(1 << 1)
#define BEACON_LED_CAPS		(1 << 2)

void numlock_beacon_stage(enum beacon_stage stage);
void numlock_beacon_set(uint8_t leds);

#endif
