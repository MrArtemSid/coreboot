## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += bootblock.c
bootblock-y += gpio.c
romstage-y += gpio.c
romstage-y += romstage.c
ramstage-y += hda_verb.c
ramstage-y += ramstage.c

bootblock-y += numlock_beacon.c
romstage-y += numlock_beacon.c
postcar-y += numlock_beacon.c
ramstage-y += numlock_beacon.c
