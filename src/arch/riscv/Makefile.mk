## SPDX-License-Identifier: GPL-2.0-only

################################################################################
## RISC-V specific options
################################################################################
ifeq ($(CONFIG_ARCH_RISCV),y)

riscv_flags = -I$(src)/arch/riscv/

ifeq ($(CONFIG_ARCH_RISCV_RV64),y)
_rv_flags += -D__riscv -D__riscv_xlen=64 -D__riscv_flen=64
else
ifeq ($(CONFIG_ARCH_RISCV_RV32),y)
_rv_flags += -D__riscv -D__riscv_xlen=32 -D__riscv_flen=32
else
$(error "You need to select ARCH_RISCV_RV64 or ARCH_RISCV_RV32")
endif
endif

# Needed for -print-libgcc-file-name which gets confused about all those arch
# suffixes in ARCH_SUFFIX_riscv.
simple_riscv_flags = $(riscv_flags)

ifeq ($(CONFIG_COMPILER_GCC),y)
MARCH_SUFFIX=$(ARCH_SUFFIX_riscv)
else
MARCH_SUFFIX=
endif

ifeq ($(CLANG_TIDY),)
riscv_flags += -march=$(CONFIG_RISCV_ARCH)$(MARCH_SUFFIX) -mabi=$(CONFIG_RISCV_ABI) -mcmodel=$(CONFIG_RISCV_CODEMODEL)
simple_riscv_flags += -march=$(CONFIG_RISCV_ARCH) -mabi=$(CONFIG_RISCV_ABI) -mcmodel=$(CONFIG_RISCV_CODEMODEL)
else
riscv_flags += $(_rv_flags)
simple_riscv_flags += $(_rv_flags)
endif

riscv_asm_flags = -march=$(CONFIG_RISCV_ARCH)$(MARCH_SUFFIX) -mabi=$(CONFIG_RISCV_ABI)

COMPILER_RT_bootblock = $(shell $(GCC_bootblock) $(simple_riscv_flags) -print-libgcc-file-name)

COMPILER_RT_romstage  = $(shell  $(GCC_romstage) $(simple_riscv_flags) -print-libgcc-file-name)

COMPILER_RT_ramstage  = $(shell  $(GCC_ramstage) $(simple_riscv_flags) -print-libgcc-file-name)

## All stages

all-y += trap_util.S
all-y += trap_handler.c
all-y += fp_asm.S
all-y += sbi.c
all-y += mcall.c
all-y += virtual_memory.c
all-y += boot.c
all-y += cache.c
all-y += smp.c
all-y += misc.c
all-$(CONFIG_ARCH_RISCV_PMP) += pmp.c
all-y += \
	$(top)/src/lib/memchr.c \
	$(top)/src/lib/memcmp.c \
	$(top)/src/lib/memcpy.c \
	$(top)/src/lib/memmove.c \
	$(top)/src/lib/memset.c
all-$(CONFIG_RISCV_USE_ARCH_TIMER) += arch_timer.c

## FDT (Flattened Devicetree) inclusion

ifeq ($(CONFIG_RISCV_DTS),y)

# at some point dtc may be compiled by our toolchain
DTC ?= dtc
CPPFLAGS_dts += -nostdinc -P -x assembler-with-cpp -I src/arch/riscv/include

$(obj)/preprocessed.dts: $(call strip_quotes, $(CONFIG_RISCV_DTS_FILE))
	$(CPP_riscv) $(CPPFLAGS_dts) -o $@ $<

$(obj)/dtb: $(obj)/preprocessed.dts
	$(DTC) -I dts -O dtb -o $@ $<

# This may be optimized in the future by letting cbfstool parse our FDT into a unflattened
# devicetree blob in build time, so that we only need to flatten it in runtime instead of
# unflatten and flatten it in runtime.
cbfs-files-y += DTB
DTB-file := $(obj)/dtb
DTB-type := raw
DTB-align := 8 # according to spec device trees needs to be 8 byte aligned

endif # CONFIG_RISCV_DTS

################################################################################
## bootblock
################################################################################
ifeq ($(CONFIG_ARCH_BOOTBLOCK_RISCV),y)

bootblock-y = bootblock.S

$(eval $(call link_stage,bootblock))

bootblock-c-ccopts += $(riscv_flags)
bootblock-S-ccopts += $(riscv_asm_flags)

ifeq ($(CONFIG_ARCH_RISCV_RV32),y)
LDFLAGS_bootblock += -m elf32lriscv
endif #CONFIG_ARCH_RISCV_RV32

endif #CONFIG_ARCH_BOOTBLOCK_RISCV

################################################################################
## romstage
################################################################################
ifeq ($(CONFIG_ARCH_ROMSTAGE_RISCV),y)

romstage-$(CONFIG_SEPARATE_ROMSTAGE) += romstage.S

# Build the romstage

$(eval $(call link_stage,romstage))

romstage-c-ccopts += $(riscv_flags)
romstage-S-ccopts += $(riscv_asm_flags)

ifeq ($(CONFIG_ARCH_RISCV_RV32),y)
LDFLAGS_romstage += -m elf32lriscv
endif #CONFIG_ARCH_RISCV_RV32

endif #CONFIG_ARCH_ROMSTAGE_RISCV

################################################################################
## ramstage
################################################################################
ifeq ($(CONFIG_ARCH_RAMSTAGE_RISCV),y)

check-ramstage-overlap-regions += stack ramstage

ramstage-y =
ramstage-y += ramstage.S
ramstage-y += tables.c
ramstage-y += payload.c
ramstage-y += fit_payload.c

$(eval $(call create_class_compiler,rmodules,riscv))

ramstage-srcs += src/mainboard/$(MAINBOARDDIR)/mainboard.c

# Build the ramstage

$(eval $(call link_stage,ramstage))

ramstage-c-ccopts += $(riscv_flags)
ramstage-S-ccopts += $(riscv_asm_flags)

ifeq ($(CONFIG_ARCH_RISCV_RV32),y)
LDFLAGS_ramstage += -m elf32lriscv
endif #CONFIG_ARCH_RISCV_RV32

endif #CONFIG_ARCH_RAMSTAGE_RISCV

ifeq ($(CONFIG_RISCV_OPENSBI),y)

ramstage-y += opensbi.c

# A preloaded OpenSBI is already in memory before coreboot runs: it is neither
# built from source, copied from a blob, nor added to CBFS. boot.c asks
# platform_get_preloaded_opensbi() for its entry point instead of loading it.
ifneq ($(CONFIG_OPENSBI_PRELOADED),y)

OPENSBI_SOURCE := $(top)/$(call strip_quotes,$(CONFIG_OPENSBI_SOURCE_DIR))
OPENSBI_BUILD  := $(abspath $(obj)/3rdparty/opensbi)
OPENSBI_TARGET := $(OPENSBI_BUILD)/platform/$(CONFIG_OPENSBI_PLATFORM)/firmware/fw_dynamic.elf
OPENSBI := $(obj)/opensbi.elf

# OpenSBI must be built as a PIE. Clang uses ld.lld, llvm-ar and llvm-objcopy.
# Bare-metal GCC uses CC_ramstage, AR_ramstage, OBJCOPY_ramstage, and a
# PIE-capable linker (configured via CONFIG_OPENSBI_GCC_LINKER, defaulting to
# ld.lld since upstream bare-metal GNU ld.bfd does not support PIE).
# Pass the tools explicitly instead of an LLVM= / CROSS_COMPILE= prefix.
ifeq ($(CONFIG_COMPILER_LLVM_CLANG),y)
OPENSBI_CC := $(lastword $(CC_ramstage))
OPENSBI_BIN := $(dir $(OPENSBI_CC))
ifeq ($(OPENSBI_BIN),./)
OPENSBI_BIN :=
endif
OPENSBI_LD := $(OPENSBI_BIN)ld.lld
OPENSBI_AR := $(OPENSBI_BIN)llvm-ar
OPENSBI_OBJCOPY := $(OPENSBI_BIN)llvm-objcopy
else
OPENSBI_CC := $(lastword $(CC_ramstage))
OPENSBI_LD := $(call strip_quotes,$(CONFIG_OPENSBI_GCC_LINKER))
ifeq ($(OPENSBI_LD),)
OPENSBI_LD := ld.lld
endif
OPENSBI_AR := $(lastword $(AR_ramstage))
OPENSBI_OBJCOPY := $(lastword $(OBJCOPY_ramstage))
endif

$(OPENSBI_TARGET): $(obj)/config.h | $(OPENSBI_SOURCE)
	printf "    MAKE       $(subst $(obj)/,,$(@))\n"
	mkdir -p $(OPENSBI_BUILD)
	$(MAKE) \
		-C "$(OPENSBI_SOURCE)" \
		CC="$(OPENSBI_CC)" \
		LD="$(OPENSBI_LD)" \
		AR="$(OPENSBI_AR)" \
		OBJCOPY="$(OPENSBI_OBJCOPY)" \
		PLATFORM=$(CONFIG_OPENSBI_PLATFORM) \
		O="$(OPENSBI_BUILD)" \
		FW_JUMP=n \
		FW_DYNAMIC=y \
		FW_PAYLOAD=n \
		FW_TEXT_START=$(CONFIG_OPENSBI_TEXT_START)

# build upstream OpenSBI source tree
opensbi-source-y = $(OPENSBI_TARGET)
# get OpenSBI from specified binary
opensbi-source-$(CONFIG_OPENSBI_BLOB) = $(call strip_quotes,$(CONFIG_OPENSBI_BLOB_PATH))

$(OPENSBI): $(opensbi-source-y)
	cp $< $@

OPENSBI_CBFS := $(CONFIG_CBFS_PREFIX)/opensbi
$(OPENSBI_CBFS)-file := $(OPENSBI)
$(OPENSBI_CBFS)-type := payload
$(OPENSBI_CBFS)-compression := $(CBFS_COMPRESS_FLAG)
cbfs-files-y += $(OPENSBI_CBFS)

check-ramstage-overlap-files += $(OPENSBI_CBFS)

endif # CONFIG_OPENSBI_PRELOADED

endif #CONFIG_RISCV_OPENSBI

endif #CONFIG_ARCH_RISCV
