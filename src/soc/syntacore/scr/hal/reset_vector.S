/* SPDX-License-Identifier: GPL-2.0-only */

.section .reset_vector, "ax", %progbits
.globl _scr_reset_vector
_scr_reset_vector:
	j _start
