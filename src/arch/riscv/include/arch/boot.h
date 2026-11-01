/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef ARCH_RISCV_INCLUDE_ARCH_BOOT_H
#define ARCH_RISCV_INCLUDE_ARCH_BOOT_H

#include <stdbool.h>
#include <stdint.h>

#define RISCV_PAYLOAD_MODE_U 0
#define RISCV_PAYLOAD_MODE_S 1
#define RISCV_PAYLOAD_MODE_M 3

struct prog;
void run_payload(struct prog *prog, void *fdt, int payload_mode);
void run_payload_opensbi(struct prog *prog, void *fdt, struct prog *opensbi, int payload_mode);

/*
 * Optional platform hook to obtain preloaded OpenSBI.
 * If true, *sbi is populated with entry and type, bypassing selfload_check.
 */
bool platform_get_preloaded_opensbi(struct prog *sbi);

void run_opensbi(const int hart_id,
		 const void *opensbi,
		 const void *fdt,
		 const void *payload,
		 const int payload_mode);

/*
 * Optional SoC hook, run by every hart in the bootblock after its stack and
 * hart-local storage are set up and before the non-working harts are parked
 * in smp_pause(). Traps are not set up yet. The harts run it concurrently;
 * any ordering between them is up to the SoC.
 */
void soc_hart_early_init(void);
#endif
