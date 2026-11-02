/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Reuse the in-tree vboot SHA-256 core instead of adding another crypto
 * implementation.  coreboot already builds this source for CBFS hashing when
 * CONFIG_VBOOT_LIB is enabled; UncoreInitPeim authorization needs the same
 * digest but is unconditional for this board, so the translation unit is
 * compiled into romstage directly.
 */
#include "../../../../../3rdparty/vboot/firmware/2lib/2sha256.c"
