## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += bootblock_custom.S

bootblock-y += media.c
romstage-y += media.c
ramstage-y += media.c
ramstage-y += reset.c

ifeq ($(CONFIG_MSM89X7_BOOTIMG),y)

MSM89X7_QCDT := $(call strip_quotes,$(CONFIG_MSM89X7_QCDT_FILE))
MSM89X7_APPEND_DTB := $(call strip_quotes,$(CONFIG_MSM89X7_APPEND_DTB_FILE))

$(obj)/boot.img: $(obj)/coreboot.rom $(top)/util/msm89x7-bootimg/msm89x7-bootimg.py
	printf "    MSM89X7    Creating $(subst $(obj)/,,$@)\n"
	$(top)/util/msm89x7-bootimg/msm89x7-bootimg.py make \
		--kernel $< \
		$(if $(MSM89X7_APPEND_DTB),--append-dtb $(MSM89X7_APPEND_DTB)) \
		$(if $(MSM89X7_QCDT),--qcdt $(MSM89X7_QCDT)) \
		-o $@

real-all: $(obj)/boot.img

endif
