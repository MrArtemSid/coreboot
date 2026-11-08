## SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_NORTHBRIDGE_INTEL_GRANTLEY),y)

bootblock-y += platform.c
romstage-y += platform.c memmap.c romstage.c
postcar-y += memmap.c
ramstage-y += platform.c memmap.c

ramstage-y += northbridge.c

endif
