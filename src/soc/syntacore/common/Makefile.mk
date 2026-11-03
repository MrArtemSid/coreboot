# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_SOC_SYNTACORE_COMMON),y)

CPPFLAGS_common += -I$(src)/soc/syntacore/common/include

all-y += uart.c mtime.c rom_media.c cbmem.c smp.c ipi.c cache.c
all-$(CONFIG_SCR_IPI_IMSIC) += aia/imsic.c
all-$(CONFIG_SCR_IPI_ICCM) += iccm/iccm.c
all-$(CONFIG_SCR_IPI_ICCM_L3) += iccm/iccm_l3.c
bootblock-$(CONFIG_SCR_BOOT_HAL) += hal/hal.c
romstage-$(CONFIG_SCR_WAIT_KEY_ON_FIRST_BOOT) += hal/wait_key.c
ifeq ($(CONFIG_SCR_BOOT_HAL)$(CONFIG_SCR_CLUSTER_L2),yy)
bootblock-y += hal/reset_vector.S
endif
ifeq ($(CONFIG_SCR_OCRAM_64K),y)
ifeq ($(CONFIG_COMPILER_LLVM_CLANG),y)
# Leave room for eight hart stacks in the 64 KiB OCRAM.
CFLAGS_bootblock += -Oz
endif
endif
romstage-y += romstage.c
ramstage-y += soc.c payload.c

SCR_HAL := $(if $(SC_HAL_PATH),$(SC_HAL_PATH),$(call strip_quotes,$(CONFIG_SCR_HAL_DIR)))
ifeq ($(wildcard $(SCR_HAL)/include/arch.h),)
$(error SCR-HAL missing: set SC_HAL_PATH or CONFIG_SCR_HAL_DIR to the SCR-HAL source directory)
endif

# SCR-HAL objects are compiled outside coreboot's rules, so take the cross
# target from coreboot: CPPFLAGS_riscv carries -target riscv64-none-elf for
# clang, without it the isolated compile falls back to the host target.
scr_hal_cflags = -march=$(CONFIG_RISCV_ARCH)$(MARCH_SUFFIX)$(if $(CONFIG_COMPILER_LLVM_CLANG),_zifencei) \
	-mabi=$(CONFIG_RISCV_ABI) \
	-mcmodel=$(CONFIG_RISCV_CODEMODEL) \
	-Os -ffreestanding -fno-common -fdata-sections -ffunction-sections -MMD -MP \
	-include $(src)/include/kconfig.h \
	-I$(SCR_HAL)/include -I$(SCR_HAL)/include/drivers \
	-I$(SCR_HAL)/platform/$(call strip_quotes,$(CONFIG_SCR_HAL_PLATFORM)) \
	-I$(SCR_HAL)/platform/common -I$(obj) \
	-I$(src)/soc/syntacore/common/include \
	$(CPPFLAGS_riscv)

scr_hal_objs := $(obj)/scrhal/cache.o $(obj)/scrhal/generic_cache.o $(obj)/scrhal/mmio.o
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

$(obj)/scrhal/generic_cache.o: $(SCR_HAL)/src/drivers/generic_cache.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

$(obj)/scrhal/mmio.o: $(src)/soc/syntacore/common/hal/mmio_hal.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

$(obj)/scrhal/ipi.o: $(src)/soc/syntacore/common/$(scr_ipi_src-y) $(obj)/config.h
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

ifeq ($(CONFIG_SCR_BOOT_HAL),y)
scr_hal_init_objs := $(obj)/scrhal/hal_init.o $(obj)/scrhal/sys_init.o $(obj)/scrhal/arch.o

$(obj)/scrhal/hal_init.o: $(src)/soc/syntacore/common/hal/hal_init.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_bootblock) $(scr_hal_cflags) -c -o $@ $<

$(obj)/scrhal/%.o: $(SCR_HAL)/src/sys/%.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_bootblock) $(scr_hal_cflags) -c -o $@ $<

bootblock-libs += $(scr_hal_init_objs)

# A single SC-DT firmware= image, in addition to the hardware/miniboot ROM.
# Use the final raw bootblock, including its BSS padding and L2 reset vector.
$(obj)/scrhal/firmware.o: $(src)/soc/syntacore/common/hal/firmware.S \
		$(objcbfs)/bootblock.bin $(obj)/coreboot.rom | files_added
	@mkdir -p $(dir $@)
	$(CC_bootblock) $(riscv_asm_flags) -c -o $@ $< \
		-DSCR_BOOTBLOCK_FILE='"$(abspath $(objcbfs)/bootblock.bin)"' \
		-DSCR_ROM_FILE='"$(abspath $(obj)/coreboot.rom)"'

$(obj)/scrhal/firmware.ld: $(src)/soc/syntacore/common/hal/firmware.ld \
		$(src)/soc/syntacore/common/include/soc/addressmap.h \
		$(obj)/scr_ddr_base.h $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CPP_riscv) -P -x assembler-with-cpp -I$(obj) -I$(src)/include \
		-I$(src)/soc/syntacore/common/include -include $(src)/include/kconfig.h -o $@ $<

$(obj)/coreboot.elf: $(obj)/scrhal/firmware.o $(obj)/scrhal/firmware.ld
	@printf "    LD         $(notdir $@)\n"
	$(LD_bootblock) -T $(obj)/scrhal/firmware.ld -o $@ $<

finalised_rom:: $(obj)/coreboot.elf
endif

$(scr_hal_objs) $(scr_hal_init_objs): $(src)/soc/syntacore/common/Makefile.mk
-include $(scr_hal_objs:.o=.d) $(scr_hal_init_objs:.o=.d)
romstage-libs += $(scr_hal_objs)
ramstage-libs += $(scr_hal_objs)

endif
