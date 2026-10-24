# ICCM support

The port uses coreboot's native RISC-V entry code, hart-local storage,
`smp_pause()`, `smp_resume()` and OpenSBI handoff. There is no private
SMP backend or Syntacore entry trampoline. Reference configurations
retain `MAX_CPUS=1`; this change adds the interrupt transport, not a
complete multi-hart boot configuration.

## Transport

`src/soc/syntacore/iccm.c` implements the existing `set_msip()` hook:

- L2 platforms use the SCR-HAL CSR ICCM driver. Physical hart IDs are
  translated to mailbox slots using `mbox-mapping` and CPU phandles in
  the Linux board FDT; a hart ID is not assumed to equal a CSR slot.
- L3 platforms use SCR-HAL's MMIO ICCM L3 driver. As in Syntacore
  OpenSBI's `scr_iccm_mmio` driver, destinations are physical hart IDs.
- Both devices deliver machine software interrupts (`mip.MSIP`). Sending
  publishes memory with a RISC-V fence before notification. Clearing
  drains the local mailbox with a bounded loop and fences before return.
  Initialization does not require enabling an interrupt controller.

`mtime_init()`, reached from native `hls_init()` before `smp_pause()`,
performs hart-local PMP/PMA setup. The working hart configures the MRT
in bootblock and enables ICCM L3 control. A stage-local initialized-data
flag publishes completion with release/acquire semantics. The driver
uses ordinary aligned loads/stores and fences for this flag; it adds
no AMO requirement. Native coreboot SMP retains its own atomic and
coherency requirements. Secondary mailbox clearing remains in native
`smp_pause()`.

The code calls SCR-HAL helpers rather than defining new CSR numbers or
MMIO addresses. References inspected locally:

- Miniboot `4cda778`: `src/cores_sync.c`, `src/miniboot.c` and its SCR-HAL
  dependency establish the distinction between platform initialization
  and firmware synchronization. Its separate barrier framework is not
  copied here.
- SC-DT `2025.12`: SCR-HAL `include/drivers/iccm.h`, `iccm_l3.h` and the
  `iccm_test` / `iccm_l3_test` sample applications. Both samples enable
  machine software interrupts.
- Syntacore OpenSBI `0bb5b64`: `lib/utils/ipi/scr_iccm.c` and
  `scr_iccm_mmio.c` establish the destination mapping and L3 control.
- Linux submodule `644787d66`: board DTS/DTSI and mailbox mappings.
  SCR9 L2 and SCR7 L3 use thin wrappers around those Linux includes;
  the other four boards select the Linux DTS directly. SCR6 is removed.

## Validation and limitations

All six reference configurations compile with GCC 14.2.0 and reach
OpenSBI and the U-Boot shell in single-hart SCR QEMU smoke tests.
The generic QEMU RISC-V RV64 configuration also builds and reaches
the expected no-payload halt with four harts. SCR QEMU
9.2.3 standalone two-hart probes on SCR9 L2, SCR9 L3 and SCR5 RV32
verify sending, `wfi` wakeup with global interrupts disabled, pending
MSIP and mailbox clearing. These probes exercise the SCR-HAL functions
linked from the coreboot build, not the complete coreboot SMP flow.

Multi-hart coreboot boot is deliberately not claimed. The restored
native startup indexes stacks and HLS by physical hart ID, the board
layouts reserve only one early stack, and stage transitions still need
separate multi-hart validation. L2 early stacks retain the existing DDR
placement, which presumes DDR is available at entry. Sparse hart IDs,
cache coherency on real hardware and cold/warm OpenSBI handoff with
multiple harts remain unverified. Merely increasing `MAX_CPUS` or
QEMU's `-smp` does not resolve these constraints.

No physical hardware testing has been performed.
