# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_SOC_SYNTACORE_SCR),y)

CPPFLAGS_common += -I$(src)/soc/syntacore/scr/include

all-y += uart.c mtime.c rom_media.c cbmem.c smp.c ipi.c cache.c
romstage-y += romstage.c
ramstage-y += chip.c

SCR_HAL := $(if $(SC_HAL_PATH),$(SC_HAL_PATH),$(call strip_quotes,$(CONFIG_SCR_HAL_DIR)))
ifeq ($(wildcard $(SCR_HAL)/include/arch.h),)
$(error SCR-HAL missing: set SC_HAL_PATH or CONFIG_SCR_HAL_DIR to the SCR-HAL source directory)
endif

scr_hal_cflags = -march=$(CONFIG_RISCV_ARCH)$(MARCH_SUFFIX) -mabi=$(CONFIG_RISCV_ABI) \
	-mcmodel=$(CONFIG_RISCV_CODEMODEL) \
	-Os -ffreestanding -fno-common -fdata-sections -ffunction-sections -MMD -MP \
	-include $(src)/include/kconfig.h \
	-I$(SCR_HAL)/include -I$(SCR_HAL)/include/drivers \
	-I$(SCR_HAL)/platform/$(call strip_quotes,$(CONFIG_SCR_HAL_PLATFORM)) \
	-I$(SCR_HAL)/platform/common -I$(obj)

scr_hal_objs := $(obj)/scrhal/cache.o

$(obj)/scrhal/cache.o: $(SCR_HAL)/src/drivers/cache.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

$(scr_hal_objs): $(src)/soc/syntacore/scr/Makefile.mk
-include $(scr_hal_objs:.o=.d)
romstage-libs += $(scr_hal_objs)
ramstage-libs += $(scr_hal_objs)

endif
