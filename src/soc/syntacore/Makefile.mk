## SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_SOC_SYNTACORE_COMMON),y)

# Syntacore SCR SoC support.
#
# Platform services are consumed from SCR-HAL, which ships as part of
# the Syntacore Development Toolkit (SC-DT, workspace/scr-hal):
#   * console (16550 + clocks via SCR-HAL hwinfo) backs the coreboot
#     UART driver in src/drivers/uart/scr_hal.c
#   * the memory map comes from the SCR-HAL platform plf.h
# Only the small self-contained SCR-HAL drivers are compiled in (as the
# libscrhal archive); crt0, trap handling and SMP stay coreboot's own.
# Cache setup is called through the SoC bridge, not the SCR-HAL runtime.
#
# The SCR-HAL location is resolved from the SC_HAL_PATH environment
# variable exported by the SC-DT env.sh, falling back to the
# CONFIG_SCR_HAL_DIR Kconfig string.

SCR_HAL := $(if $(SC_HAL_PATH),$(SC_HAL_PATH),$(call strip_quotes,$(CONFIG_SCR_HAL_DIR)))
SCR_HAL := $(abspath $(SCR_HAL))

# Board device trees come straight from the Syntacore Linux tree
# (3rdparty/linux submodule, branch sc/main-6.12, shallow); the board
# wrappers in the mainboard directories include its dtsi files.
SCR_LINUX := 3rdparty/linux

CPPFLAGS_dts += \
	-I$(top)/$(SCR_LINUX)/arch/riscv/boot/dts/syntacore \
	-I$(top)/$(SCR_LINUX)/include

ifneq ($(strip $(CONFIG_RISCV_DTS_FILE)),)
ifeq ($(wildcard $(top)/$(call strip_quotes,$(CONFIG_RISCV_DTS_FILE))),)
$(error board DTS "$(call strip_quotes,$(CONFIG_RISCV_DTS_FILE))" not found - run: git submodule update --init --depth 1 3rdparty/linux)
endif
endif

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
scr_hal_cflags = \
	-march=$(call strip_quotes,$(CONFIG_RISCV_ARCH))$(MARCH_SUFFIX) \
	-mabi=$(call strip_quotes,$(CONFIG_RISCV_ABI)) \
	-mcmodel=$(call strip_quotes,$(CONFIG_RISCV_CODEMODEL)) \
	-mstrict-align -Os -fno-common -fdata-sections -ffunction-sections \
	-DPRINTF_LEVEL=2 \
	-include $(obj)/config.h \
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
	# L3 may already cache bootblock stacks/data; preserve dirty lines.
	$(CC_ramstage) $(scr_hal_cflags) -DPLF_SKIP_CACHE_INVALIDATION -c -o $@ $<

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

# With SEPARATE_ROMSTAGE = n the romstage (and its libs) is carried
# inside the bootblock via the postinclude hook; adding the archive to
# bootblock-libs as well would link it twice. With a separate romstage
# the bootblock needs its own copy (it talks to the UART early).
ifeq ($(CONFIG_SEPARATE_ROMSTAGE),y)
bootblock-libs += $(SCR_HAL_ARCHIVE)
endif
romstage-libs += $(SCR_HAL_ARCHIVE)
ramstage-libs += $(SCR_HAL_ARCHIVE)

all-y += iccm.c
all-y += cache.c

# The saved MRT state and both pre-romstage wait hooks live in bootblock.
bootblock-y += romstage.c

bootblock-y += cbmem.c
bootblock-y += rom_media.c
bootblock-y += mtime.c
bootblock-$(CONFIG_SCR_RESET_AT_TOP) += reset_stub.S

romstage-y += cbmem.c
romstage-y += rom_media.c
romstage-y += mtime.c

ramstage-y += cbmem.c
ramstage-y += rom_media.c
ramstage-y += mtime.c

# Reset vector for the L2-family machines (reset PC at the top of the
# address space): linked into the bootblock by the board memlayout and
# carried at the end of bootblock.bin. The standalone 4-byte extraction
# (rv32 loaders trim the bootblock to avoid crossing the end of the
# address space) is provided for such flows.
ifeq ($(CONFIG_SCR_RESET_AT_TOP),y)
$(obj)/scrhal/reset_stub.bin: $(objcbfs)/bootblock.elf
	printf "    RESET-STUB $(subst $(obj)/,,$(@))\n"
	$(OBJCOPY_bootblock) -O binary --only-section=.reset_vector $< $@

coreboot: $(obj)/scrhal/reset_stub.bin
endif # CONFIG_SCR_RESET_AT_TOP

endif # CONFIG_SOC_SYNTACORE_COMMON
