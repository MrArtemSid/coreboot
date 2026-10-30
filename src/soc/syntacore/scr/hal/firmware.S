/* SPDX-License-Identifier: GPL-2.0-only */

/* Package the existing images without changing their contents or layout. */
.section .scr_bootblock, "awx", %progbits
.incbin SCR_BOOTBLOCK_FILE

.section .scr_cbfs, "a", %progbits
.incbin SCR_ROM_FILE
