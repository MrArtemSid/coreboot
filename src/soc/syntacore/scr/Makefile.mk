# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_SOC_SYNTACORE_SCR),y)

CPPFLAGS_common += -I$(src)/soc/syntacore/scr/include

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
ramstage-y += chip.c payload.c

SCR_HAL := $(if $(SC_HAL_PATH),$(SC_HAL_PATH),$(call strip_quotes,$(CONFIG_SCR_HAL_DIR)))
ifeq ($(wildcard $(SCR_HAL)/include/arch.h),)
$(error SCR-HAL missing: set SC_HAL_PATH or CONFIG_SCR_HAL_DIR to the SCR-HAL source directory)
endif

scr_hal_cflags = -march=$(CONFIG_RISCV_ARCH)$(MARCH_SUFFIX)$(if $(CONFIG_COMPILER_LLVM_CLANG),_zifencei) \
	-mabi=$(CONFIG_RISCV_ABI) \
	-mcmodel=$(CONFIG_RISCV_CODEMODEL) \
	-Os -ffreestanding -fno-common -fdata-sections -ffunction-sections -MMD -MP \
	-include $(src)/include/kconfig.h \
	-I$(SCR_HAL)/include -I$(SCR_HAL)/include/drivers \
	-I$(SCR_HAL)/platform/$(call strip_quotes,$(CONFIG_SCR_HAL_PLATFORM)) \
	-I$(SCR_HAL)/platform/common -I$(obj)

scr_hal_objs := $(obj)/scrhal/cache.o
scr_ipi_src-$(CONFIG_SCR_IPI_IMSIC) := aia/imsic_hal.c
scr_ipi_src-$(CONFIG_SCR_IPI_ICCM) := iccm/iccm_hal.c
scr_ipi_src-$(CONFIG_SCR_IPI_ICCM_L3) := iccm/iccm_l3_hal.c
ifneq ($(scr_ipi_src-y),)
scr_hal_objs += $(obj)/scrhal/ipi.o
endif

$(obj)/scrhal/cache.o: $(SCR_HAL)/src/drivers/cache.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

$(obj)/scrhal/ipi.o: $(src)/soc/syntacore/scr/$(scr_ipi_src-y) $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_ramstage) $(scr_hal_cflags) -c -o $@ $<

ifeq ($(CONFIG_SCR_BOOT_HAL),y)
scr_hal_init_objs := $(obj)/scrhal/hal_init.o $(obj)/scrhal/sys_init.o $(obj)/scrhal/arch.o

$(obj)/scrhal/hal_init.o: $(src)/soc/syntacore/scr/hal/hal_init.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_bootblock) $(scr_hal_cflags) -c -o $@ $<

$(obj)/scrhal/%.o: $(SCR_HAL)/src/sys/%.c $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CC_bootblock) $(scr_hal_cflags) -c -o $@ $<

bootblock-libs += $(scr_hal_init_objs)

# A single SC-DT firmware= image, in addition to the hardware/miniboot ROM.
# Use the final raw bootblock, including its BSS padding and L2 reset vector.
$(obj)/scrhal/firmware.o: $(src)/soc/syntacore/scr/hal/firmware.S \
		$(objcbfs)/bootblock.bin $(obj)/coreboot.rom | files_added
	@mkdir -p $(dir $@)
	$(CC_bootblock) $(riscv_asm_flags) -c -o $@ $< \
		-DSCR_BOOTBLOCK_FILE='"$(abspath $(objcbfs)/bootblock.bin)"' \
		-DSCR_ROM_FILE='"$(abspath $(obj)/coreboot.rom)"'

$(obj)/scrhal/firmware.ld: $(src)/soc/syntacore/scr/hal/firmware.ld \
		$(src)/soc/syntacore/scr/include/soc/addressmap.h $(obj)/config.h
	@mkdir -p $(dir $@)
	$(CPP_riscv) -P -x assembler-with-cpp -I$(obj) -I$(src)/include \
		-I$(src)/soc/syntacore/scr/include -include $(src)/include/kconfig.h -o $@ $<

$(obj)/coreboot.elf: $(obj)/scrhal/firmware.o $(obj)/scrhal/firmware.ld
	@printf "    LD         $(notdir $@)\n"
	$(LD_bootblock) -T $(obj)/scrhal/firmware.ld -o $@ $<

finalised_rom:: $(obj)/coreboot.elf
endif

$(scr_hal_objs) $(scr_hal_init_objs): $(src)/soc/syntacore/scr/Makefile.mk
-include $(scr_hal_objs:.o=.d) $(scr_hal_init_objs:.o=.d)
romstage-libs += $(scr_hal_objs)
ramstage-libs += $(scr_hal_objs)

endif
