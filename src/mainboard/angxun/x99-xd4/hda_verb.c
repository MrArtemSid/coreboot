/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/azalia_device.h>

/*
 * No HD-audio pin configuration has been recovered for the Angxun X99-XD4:
 * neither the codec vendor/device ID nor the pin configuration is present in
 * any artifact this port was derived from.  The tables below are the tree-wide
 * zero-length idiom (``[0] = {}``), so sizeof() is 0 and no verb consumer can
 * read past them.
 *
 * The devicetree declares no 1b.0 device, so the Azalia driver never runs on
 * this board; the empty tables exist because src/device/azalia_device.c links
 * against cim_verb_data/pc_beep_verbs unconditionally, and
 * AZALIA_USE_LEGACY_VERB_TABLE is what declares their sizes.  That select is
 * a link-time requirement here, not a claim that audio is configured.
 */
const u32 cim_verb_data[0] = {};

const u32 pc_beep_verbs[0] = {};

AZALIA_ARRAY_SIZES;
