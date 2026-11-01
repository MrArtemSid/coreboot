# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_SOC_SYNTACORE_SCR),y)

CPPFLAGS_common += -I$(src)/soc/syntacore/scr/include

all-y += uart.c mtime.c rom_media.c cbmem.c smp.c ipi.c cache.c
all-$(CONFIG_SCR_IPI_IMSIC) += aia/imsic.c
all-$(CONFIG_SCR_IPI_ICCM) += iccm/iccm.c
all-$(CONFIG_SCR_IPI_ICCM_L3) += iccm/iccm_l3.c
romstage-y += romstage.c
ramstage-y += chip.c

SCR_HAL := $(if $(SC_HAL_PATH),$(SC_HAL_PATH),$(call strip_quotes,$(CONFIG_SCR_HAL_DIR)))
ifeq ($(wildcard $(SCR_HAL)/include/arch.h),)
$(error SCR-HAL missing: set SC_HAL_PATH or CONFIG_SCR_HAL_DIR to the SCR-HAL source directory)
endif

# SCR-HAL objects are compiled outside coreboot's rules, so take the cross
# target from coreboot: CPPFLAGS_riscv carries -target riscv64-none-elf for
# clang, without it the isolated compile falls back to the host target.
scr_hal_cflags = -march=$(CONFIG_RISCV_ARCH)$(MARCH_SUFFIX) -mabi=$(CONFIG_RISCV_ABI) \
	-mcmodel=$(CONFIG_RISCV_CODEMODEL) \
	-Os -ffreestanding -fno-common -fdata-sections -ffunction-sections -MMD -MP \
	-include $(src)/include/kconfig.h \
	-I$(SCR_HAL)/include -I$(SCR_HAL)/include/drivers \
	-I$(SCR_HAL)/platform/$(call strip_quotes,$(CONFIG_SCR_HAL_PLATFORM)) \
	-I$(SCR_HAL)/platform/common -I$(obj) \
	-I$(src)/soc/syntacore/scr/include \
	$(CPPFLAGS_riscv)

scr_hal_objs := $(obj)/scrhal/cache.o $(obj)/scrhal/mmio.o
scr_ipi_src-$(CONFIG_SCR_IPI_IMSIC) := aia/imsic_hal.c
scr_ipi_src-$(CONFIG_SCR_IPI_ICCM) := iccm/iccm_hal.c
scr_ipi_src-$(CONFIG_SCR_IPI_ICCM_L3) := iccm/iccm_l3_hal.c
ifneq ($(scr_ipi_src-y),)
scr_hal_objs += $(obj)/scrhal/ipi.o
endif

# Try the IMSIC guest-index API first, then fall back to the older signature.
scr_ipi_recipe = $(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<
ifeq ($(CONFIG_SCR_IPI_IMSIC),y)
scr_ipi_recipe = $(CC_ramstage) $(scr_hal_cflags) -DSCR_IMSIC_HAS_GUEST -c -o $@ $< 2>/dev/null || \
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<
endif

$(obj)/scrhal/cache.o: $(SCR_HAL)/src/drivers/cache.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

$(obj)/scrhal/mmio.o: $(src)/soc/syntacore/scr/hal/mmio_hal.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

$(obj)/scrhal/ipi.o: $(src)/soc/syntacore/scr/$(scr_ipi_src-y) $(obj)/config.h
	@mkdir -p $(dir $@)
	$(scr_ipi_recipe)

# Read PLF_MEM_BASE from the existing HAL memory-map object. The linker
# scripts need a plain number rather than the platform header's C suffixes.
$(obj)/scr_ddr_base.h: $(obj)/scrhal/mmio.o
	@printf "    GEN        $(subst $(obj)/,,$@)\n"
	$(NM_bootblock) --defined-only $< | \
		awk '$$3 == "_scr_hal_ddr_base" { v = "0x" $$1; n++ } \
		END { if (n != 1) exit 1; print "#define SCR_DDR_BASE " v }' > $@.tmp
	@mv $@.tmp $@

# Every stage includes addressmap.h and the linker scripts are preprocessed
# before any object is compiled, so name the dependencies up front.
bootblock-c-deps += $(obj)/scr_ddr_base.h
romstage-c-deps += $(obj)/scr_ddr_base.h
ramstage-c-deps += $(obj)/scr_ddr_base.h
$(foreach stage,bootblock romstage ramstage, \
	$(call src-to-obj,$(stage),$(CONFIG_MEMLAYOUT_LD_FILE))): $(obj)/scr_ddr_base.h

$(scr_hal_objs): $(src)/soc/syntacore/scr/Makefile.mk
-include $(scr_hal_objs:.o=.d)
romstage-libs += $(scr_hal_objs)
ramstage-libs += $(scr_hal_objs)

endif
