/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include "numlock_beacon.h"

static void beacon_ramstage(void *unused)
{
	numlock_beacon_stage(BEACON_RAMSTAGE);
}

/* Payload handoff: NumLock + ScrollLock (SeaBIOS will reset the keyboard). */
static void beacon_payload(void *unused)
{
	numlock_beacon_set(BEACON_LED_NUM | BEACON_LED_SCROLL);
}

BOOT_STATE_INIT_ENTRY(BS_PRE_DEVICE, BS_ON_ENTRY, beacon_ramstage, NULL);
BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY, beacon_payload, NULL);
