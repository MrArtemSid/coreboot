## SPDX-License-Identifier: GPL-2.0-only

# Syntacore SCR SoC support.
#
# Platform services are consumed from SCR-HAL, which ships as part of
# the Syntacore Development Toolkit (SC-DT, workspace/scr-hal):
#   * console (16550 + clocks via SCR-HAL hwinfo) backs the coreboot
#     UART driver in src/drivers/uart/scr_hal.c
#   * the memory map comes from the SCR-HAL platform plf.h
# Only the small self-contained SCR-HAL drivers are compiled in (as the
# libscrhal archive); crt0, trap handling, SMP and cache setup stay
# coreboot's own (bootblock.S and friends), so the two firmwares do not
# fight over machine state.
#
# The SCR-HAL location is resolved from the SC_HAL_PATH environment
# variable exported by the SC-DT env.sh, falling back to the
# CONFIG_SCR_HAL_DIR Kconfig string.

SCR_HAL := $(if $(SC_HAL_PATH),$(SC_HAL_PATH),$(call strip_quotes,$(CONFIG_SCR_HAL_DIR)))
SCR_HAL := $(abspath $(SCR_HAL))

ifeq ($(wildcard $(SCR_HAL)/include/arch.h),)
$(error SCR-HAL not found at "$(SCR_HAL)": source the SC-DT env.sh (SC_HAL_PATH) or set CONFIG_SCR_HAL_DIR to the scr-hal directory)
endif

scr_hal_inc := \
	-I$(SCR_HAL)/include \
	-I$(SCR_HAL)/include/drivers \
	-I$(SCR_HAL)/platform/$(call strip_quotes,$(CONFIG_SCR_HAL_PLATFORM)) \
	-I$(SCR_HAL)/platform/common

# SCR-HAL objects are built with their own flags: they expect the
# toolchain's freestanding headers and SCR-HAL tunables, not coreboot's
# ENV_* environment.
scr_hal_cflags := \
	-march=$(call strip_quotes,$(CONFIG_RISCV_ARCH)) \
	-mabi=$(call strip_quotes,$(CONFIG_RISCV_ABI)) \
	-mcmodel=$(call strip_quotes,$(CONFIG_RISCV_CODEMODEL)) \
	-mstrict-align -Os -fno-common -fdata-sections -ffunction-sections \
	-DPRINTF_LEVEL=2 \
	$(scr_hal_inc)

# coreboot-side code (soc files, UART driver, memlayout) reaches the
# SCR-HAL platform headers through <soc/addressmap.h> and direct includes.
CPPFLAGS_common += $(scr_hal_inc) -I$(top)/src/soc/syntacore/include

# Self-contained SCR-HAL sources shared by every stage (console + hwinfo;
# the UART driver itself is header-only and pulled in through plf.h).
# Compiled once with the common flags and linked into each stage.
scr_hal_objs := $(obj)/scrhal/console.o \
		$(obj)/scrhal/cache.o \
		$(obj)/scrhal/hwinfo.o \
		$(obj)/scrhal/hwinfo_mmio.o \
		$(obj)/scrhal/sys_utils.o \
		$(obj)/scrhal/scr_hal_glue.o \
		$(obj)/scrhal/mrt.o

$(obj)/scrhal/console.o: $(SCR_HAL)/src/drivers/console.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

$(obj)/scrhal/cache.o: $(SCR_HAL)/src/drivers/cache.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

$(obj)/scrhal/hwinfo.o: $(SCR_HAL)/src/drivers/hwinfo/hwinfo.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

$(obj)/scrhal/hwinfo_mmio.o: $(SCR_HAL)/src/drivers/hwinfo/hwinfo_mmio.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

SCR_HAL_ARCHIVE := $(obj)/scrhal/libscrhal.a

$(obj)/scrhal/sys_utils.o: $(SCR_HAL)/src/sys/sys_utils.S $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

$(obj)/scrhal/scr_hal_glue.o: $(top)/src/soc/syntacore/scr_hal_glue.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

$(obj)/scrhal/mrt.o: $(SCR_HAL)/src/drivers/mrt.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

$(SCR_HAL_ARCHIVE): $(scr_hal_objs)
	printf "    AR         SCR-HAL\n"
	$(AR_ramstage) rcs $@ $(scr_hal_objs)

bootblock-libs += $(SCR_HAL_ARCHIVE)
romstage-libs += $(SCR_HAL_ARCHIVE)
ramstage-libs += $(SCR_HAL_ARCHIVE)

bootblock-y += cbmem.c
bootblock-y += rom_media.c
bootblock-y += mtime.c

romstage-y += cbmem.c
romstage-y += rom_media.c
romstage-y += mtime.c

ramstage-y += cbmem.c
ramstage-y += rom_media.c
ramstage-y += mtime.c
