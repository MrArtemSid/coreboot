/* SPDX-License-Identifier: GPL-2.0-only */

#include <northbridge/intel/grantley/platform.h>
#include "numlock_beacon.h"

/*
 * Called by the Grantley romstage right before UncoreInitPeim is launched.
 * Blink the romstage rate, then keep NumLock+CapsLock lit while the vendor
 * blob trains memory. A hang here leaves both LEDs on.
 */
void mainboard_memory_init_params(struct grantley_memory_params *params)
{
	numlock_beacon_stage(BEACON_ROMSTAGE);
	numlock_beacon_set(BEACON_LED_NUM | BEACON_LED_CAPS);
}
