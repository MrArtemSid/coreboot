/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootblock_common.h>
#include <program_loading.h>
#include <soc/scr_hal.h>

#if CONFIG(SCR_WAIT_KEY_ON_FIRST_BOOT)
#if CONFIG(SEPARATE_ROMSTAGE)
/* The MRT state belongs to bootblock; wait before entering romstage. */
void platform_prog_run(struct prog *prog)
{
	if (prog_type(prog) == PROG_ROMSTAGE && scr_is_cold_reset)
		scr_hal_wait_key_before_ddr();
}
#else
/* An integrated romstage is called directly, without prog_run(). */
void bootblock_soc_init(void)
{
	if (scr_is_cold_reset)
		scr_hal_wait_key_before_ddr();
}
#endif
#endif
