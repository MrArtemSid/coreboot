# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_SOC_SYNTACORE_SCR),y)

# The shared objects, headers and linker layouts are built by
# soc/syntacore/common. Only the chip_operations entry is SoC-specific.
ramstage-y += chip.c

endif
