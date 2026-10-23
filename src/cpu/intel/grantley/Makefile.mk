## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += cache_as_ram.S
bootblock-y += ../car/bootblock.c
bootblock-y += ../../x86/early_reset.S

postcar-y += ../car/non-evict/exit_car.S

bootblock-y += platform.c
romstage-y += platform.c
ramstage-y += platform.c

ramstage-y += cpu_init.c
ramstage-$(CONFIG_HAVE_ACPI_TABLES) += acpi.c
ramstage-$(CONFIG_HAVE_SMI_HANDLER) += smm.c
smm-$(CONFIG_HAVE_SMI_HANDLER) += smm_finalize.c

# No client Haswell power management, SMM relocation or memory-init ABI.
subdirs-y += ../microcode
cpu_microcode_bins += $(wildcard 3rdparty/intel-microcode/intel-ucode/06-3f-02)
cpu_microcode_bins += $(wildcard 3rdparty/intel-microcode/intel-ucode/06-4f-01)
# This pinned Intel package keeps BDX updates here because OS late-loading
# requires special handling. Firmware applies the matching update before OS boot.
cpu_microcode_bins += $(wildcard 3rdparty/intel-microcode/intel-ucode-with-caveats/06-4f-01)

# Engineering-sample steppings never shipped in the pinned Intel package.
# Boards carrying ES CPUs supply the vendor updates (extracted from their
# stock BIOS) through the board directory in the blobs submodule.
cpu_microcode_bins += $(sort $(wildcard 3rdparty/blobs/mainboard/$(MAINBOARDDIR)/microcode/*))
