# Syntacore SCR SoCs

This page describes coreboot support for Syntacore SCR RISC-V clusters
(SCR5/SCR7/SCR9 cores with an MMU).

## Overview

Syntacore clusters boot through the following chain:

```
coreboot -> OpenSBI -> U-Boot -> Linux
```

The platform services coreboot consumes (UART console, machine timer,
cluster MRT unlock, memory typing, hwinfo clocks) are provided by
SCR-HAL, which is part of the Syntacore Development Toolkit (SC-DT,
`workspace/scr-hal`):

- the memory map (`plf.h` per platform) is the *single source of truth*
  shared with miniboot, OpenSBI firmware and Syntacore sample apps;
- the UART driver (`src/drivers/uart/scr_hal.c`) is a thin shim over the
  SCR-HAL console driver, so UART base, register layout and the live
  clock come from SCR-HAL `hwinfo`, never from coreboot;
- `src/soc/syntacore/scr_hal_glue.c` is the only translation unit where
  SCR-HAL and coreboot headers meet (their RISC-V macro spellings
  conflict); it exposes plain functions.

SCR-HAL is consumed directly from an SC-DT checkout (source `env.sh` to
export `SC_HAL_PATH`, or point the `CONFIG_SCR_HAL_DIR` Kconfig string
at the `workspace/scr-hal` directory). Updating SC-DT therefore updates
the coreboot port with no code changes: the compile-time cross-check
`src/soc/syntacore/include/soc/plf_check.h` fails the build if the
Kconfig memory anchors (`SCR_DDR_BASE`, `SCR_OCRAM_*`) ever drift from
the SCR-HAL platform headers.

## Boot flow on SCR machines

- The CPU enters the firmware at `OCRAM base + 0x200` (bootblock and
  romstage execute from on-chip SRAM exactly like Syntacore miniboot;
  everything those stages reach through PC-relative addressing must
  stay in OCRAM - OCRAM and DDR are 16 GiB apart, beyond the ±2 GiB
  reach of the medany code model).
- Ramstage runs from DDR. The CBFS image is a read-only window in DDR
  (`SCR_CBFS_WINDOW_BASE`, SPI NOR XIP on FPGA boards, a preloaded
  `coreboot.rom` in SCR QEMU).
- The board device tree (`RISCV_DTS_FILE`, derived from the Linux
  `arch/riscv/boot/dts/syntacore/` board files) is stored in CBFS and
  installed as the hart FDT by the bootblock - SCR machines do not
  hand over an FDT at reset.
- During hart-local storage setup, the SoC configures the hart's memory
  attributes, initializes the cluster MRT in bootblock and prepares ICCM.
  The native coreboot SMP backend uses `set_msip()` to access ICCM.
  See [ICCM support](iccm.md) for scope and limitations.
- OpenSBI receives the existing `fw_dynamic` handoff and starts the ELF
  payload in S-mode. There is no Syntacore-specific OpenSBI trampoline.

## Supported boards

- `syntacore/scr_l3_cluster` - SCR9 and SCR7 L3 clusters (QEMU
  machines `scr9_l3`, `scr7_l3`), named after the Syntacore U-Boot
  board `scr_l3_cluster`;
- `syntacore/scr_l2_cluster` - SCR9/SCR7/SCR5 RV64 L2 clusters
  (QEMU machines `scr9_l2`, `scr7_l2`, `scr5`); the 64 KiB
  OCRAM at the top of the address space carries a single early stage
  (bootblock + inline romstage), with a four-byte reset stub at the
  machine reset PC;
- `syntacore/scr_l2_cluster_32` - SCR5 RV32 L2 cluster (32-bit).

All six platforms build through the same SoC layer; the per-board
differences are the memory anchors (Kconfig), the board device tree
(derived from the Linux `arch/riscv/boot/dts/syntacore/` board files)
and the payload addresses.

## Building

Build the payloads first (see sc-qemu.md), then:

The OpenSBI and U-Boot payload sources are pinned as git submodules
(`3rdparty/opensbi`, `3rdparty/u-boot`, Syntacore `sc/2026.04` branch).

```bash
make CROSS_COMPILE=riscv64-unknown-elf- distclean
cp configs/config.syntacore_scr_l3_cluster .config
source <sc-dt>/env.sh   # exports SC_HAL_PATH
make CROSS_COMPILE=riscv64-unknown-elf- olddefconfig
make CROSS_COMPILE=riscv64-unknown-elf-
```
