/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_QUALCOMM_MSM89X7_MMU_H__
#define __SOC_QUALCOMM_MSM89X7_MMU_H__

#include <stdint.h>

void msm89x7_mmu_init(void);
void msm89x7_mmu_capture_vectors(void);
void msm89x7_mmu_live_probe(void);
void msm89x7_mmu_probe(unsigned int mode, volatile uint64_t *scratch);
void msm89x7_mmu_probe_fault(uint64_t vector, uint64_t esr, uint64_t far,
			   uint64_t elr, uint64_t sctlr) __attribute__((noreturn));
void mmu_diag_text(const char *text);

#endif /* __SOC_QUALCOMM_MSM89X7_MMU_H__ */
