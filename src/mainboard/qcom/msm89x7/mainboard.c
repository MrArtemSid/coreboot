/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <device/device.h>
#include <gpio.h>

/* Volume Up key, active low with the SoC pull-up (santoni and most
 * MSM89x7 reference designs). Volume Down is the PMIC RESIN line. */
#define GPIO_VOL_UP	GPIO(91)

static void mainboard_enable(struct device *dev)
{
	gpio_input_pullup(GPIO_VOL_UP);
	printk(BIOS_INFO, "MSM89X7: Volume Up %s\n",
	       gpio_get(GPIO_VOL_UP) ? "released" : "pressed");
}

struct chip_operations mainboard_ops = {
	.name = "QCOM MSM89x7 generic",
	.enable_dev = mainboard_enable,
};
