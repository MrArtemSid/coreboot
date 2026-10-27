## SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_SOC_QUALCOMM_MSM89X7),y)

################################################################################
all-$(CONFIG_MSM89X7_DISPLAY_CONSOLE) += display_console.c font_8x16.c
all-$(CONFIG_MSM89X7_RAMOOPS_CONSOLE) += ramoops.c

################################################################################
bootblock-y += bootblock.c
bootblock-y += cbmem.c
bootblock-y += mmu.c

################################################################################
romstage-y += cbmem.c

################################################################################
ramstage-y += cbmem.c
ramstage-y += soc.c

################################################################################

CPPFLAGS_common += -Isrc/soc/qualcomm/msm89x7/include

endif
