## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += bootblock.c
bootblock-y += mainboard.c
bootblock-y += chip.c

romstage-y += romstage.c
romstage-y += mainboard.c
romstage-y += chip.c

ramstage-y += mainboard.c
ramstage-y += chip.c
