## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += ../car/non-evict/cache_as_ram.S
bootblock-y += ../car/bootblock.c
bootblock-y += ../../x86/early_reset.S

postcar-y += ../car/non-evict/exit_car.S

# No client Haswell power management, SMM relocation or memory-init ABI.
subdirs-y += ../microcode
cpu_microcode_bins += $(wildcard 3rdparty/intel-microcode/intel-ucode/06-3f-02)
cpu_microcode_bins += $(wildcard 3rdparty/intel-microcode/intel-ucode/06-4f-01)
# This pinned Intel package keeps BDX updates here because OS late-loading
# requires special handling. This port uses coreboot's pre-RAM loader only.
cpu_microcode_bins += $(wildcard 3rdparty/intel-microcode/intel-ucode-with-caveats/06-4f-01)
