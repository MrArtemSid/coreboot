/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/boot.h>
#include <arch/encoding.h>
#include <console/console.h>
#include <mcall.h>
#include <soc/ipi.h>
#include <soc/hal.h>

void __weak scr_ipi_init(void) {}
void __weak scr_ipi_clear(void) {}

void __weak scr_ipi_send(unsigned int hart)
{
	die("No SCR IPI backend");
}

void soc_hart_early_init(void)
{
	if (CONFIG(SCR_BOOT_HAL)) {
		scr_hart_init();
		return;
	}
	if (read_csr(mhartid) == CONFIG_RISCV_WORKING_HARTID)
		scr_ipi_init();
}

void set_msip(int hartid, int val)
{
	if (hartid < 0 || hartid >= CONFIG_MAX_CPUS)
		die("Invalid IPI hart");
	if (val)
		scr_ipi_send(hartid);
	else
		scr_ipi_clear();
}
