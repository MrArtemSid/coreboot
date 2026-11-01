/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_SYNTACORE_SCR_IPI_H
#define SOC_SYNTACORE_SCR_IPI_H

void scr_ipi_init(void);
void scr_ipi_send(unsigned int hart);
void scr_ipi_clear(void);
int scr_iccm_send(unsigned int slot);
int scr_iccm_clear(void);
void scr_imsic_send(unsigned int hart);
void scr_imsic_arm(void);

#endif
