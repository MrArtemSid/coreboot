/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/encoding.h>
#include <console/console.h>
#include <soc/hal.h>
#include <soc/ipi.h>

/* The reset image supplies fresh flags; no BSS clearing is assumed here. */
static int cluster_ready __section(".data") = 0;
bool scr_mrt_was_uninitialized __section(".data") = false;

void scr_hart_init(void)
{
	scr_hal_early_init();
	if (read_csr(mhartid) == CONFIG_RISCV_WORKING_HARTID) {
		scr_hal_cache_shared_init();
		scr_hal_cache_local_init();
		scr_hal_memory_init();
		scr_mrt_was_uninitialized = !scr_hal_mrt_is_initialized();
		if (scr_mrt_was_uninitialized)
			scr_hal_mrt_init();
		scr_hal_features_init();
		scr_hal_timer_init();
		scr_ipi_init();
		__atomic_store_n(&cluster_ready, 1, __ATOMIC_RELEASE);
	} else {
		unsigned int spins = 100000000;

		while (!__atomic_load_n(&cluster_ready, __ATOMIC_ACQUIRE)) {
			if (!--spins)
				die("SCR cluster initialization timed out");
		}
		scr_hal_cache_local_init();
		scr_hal_memory_init();
		scr_hal_features_init();
	}
}
