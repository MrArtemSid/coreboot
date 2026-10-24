## SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_NORTHBRIDGE_INTEL_GRANTLEY),y)
ifeq ($(CONFIG_HAVE_INTEL_UNCORE_BIN),)
# Reject image builds if uncore reference blob is not configured.
ifeq ($(strip $(MAKECMDGOALS)),)
$(error Grantley DRAM initialization is not implemented; build diagnostic ELF targets only)
endif
ifneq ($(filter all coreboot coreboot.rom %/coreboot.rom %/coreboot.pre,$(MAKECMDGOALS)),)
$(error Grantley DRAM initialization is not implemented; build diagnostic ELF targets only)
endif
endif

bootblock-y += platform.c
bootblock-y += bootblock.c
romstage-y += platform.c
ramstage-y += platform.c
romstage-y += romstage.c
romstage-y += memmap.c
romstage-y += native_raminit/spd.c
romstage-y += uncore_runner/pei_services.c
romstage-y += uncore_runner/pei_hobs.c

postcar-y += memmap.c
ramstage-y += memmap.c
ramstage-y += northbridge.c

cbfs-files-$(CONFIG_HAVE_INTEL_UNCORE_BIN) += uncore.bin
uncore_bin_file := $(call strip_quotes,$(CONFIG_INTEL_UNCORE_BIN_FILE))
uncore.bin-file := $(uncore_bin_file)
uncore.bin-type := raw
uncore.bin-position := $(if $(wildcard $(uncore_bin_file)),\
	$(shell $(PYTHON3) util/grantley/pe_image_base.py \
		$(uncore_bin_file)))

# There is deliberately no ramstage/resource map or memory-init substitute.
ifeq ($(CONFIG_HAVE_INTEL_UNCORE_BIN),)
.PHONY: grantley-no-raminit
grantley-no-raminit:
	$(error Grantley DRAM initialization is not implemented; build bootblock/romstage diagnostic ELF targets only)

$(obj)/coreboot.rom: grantley-no-raminit
endif
endif
