## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += bootblock.c
bootblock-y += gpio.c
romstage-y += gpio.c
# azalia_device.c links against cim_verb_data/pc_beep_verbs even though this
# board declares no 1b.0 device; the file carries zero-length tables.
ramstage-y += hda_verb.c
