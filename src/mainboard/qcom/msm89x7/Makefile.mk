## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += bootblock_custom.S

bootblock-y += media.c
romstage-y += media.c
ramstage-y += media.c
ramstage-y += reset.c
