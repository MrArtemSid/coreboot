/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/azalia_device.h>

/*
 * The PCH HD-Audio function (00:1b.0) is absent on this board: there is no
 * codec on the PCH link and no verb table to program.  The symbols still have
 * to exist because the generic Azalia code is linked in for the Lynx Point
 * southbridge.
 */
static struct azalia_codec mainboard_azalia_codecs[0] = { };

const u32 pc_beep_verbs[0] = { };

AZALIA_ARRAY_SIZES;
