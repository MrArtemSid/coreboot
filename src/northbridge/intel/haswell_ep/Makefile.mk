## SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_NORTHBRIDGE_INTEL_HASWELL_EP),y)
# Reject image builds before make can pull in unsupported client ramstage code.
ifeq ($(strip $(MAKECMDGOALS)),)
$(error Grantley DRAM initialization is not implemented; build diagnostic ELF targets only)
endif
ifneq ($(filter all coreboot coreboot.rom %/coreboot.rom %/coreboot.pre,$(MAKECMDGOALS)),)
$(error Grantley DRAM initialization is not implemented; build diagnostic ELF targets only)
endif

bootblock-y += platform.c
romstage-y += platform.c
romstage-y += romstage.c
romstage-y += native_raminit/spd.c

# There is deliberately no ramstage/resource map or memory-init substitute.
.PHONY: grantley-no-raminit
grantley-no-raminit:
	$(error Grantley DRAM initialization is not implemented; build bootblock/romstage diagnostic ELF targets only)

$(obj)/coreboot.rom: grantley-no-raminit
endif
