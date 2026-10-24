# ICCM support

The port uses coreboot's native RISC-V entry code, hart-local storage,
`smp_pause()`, `smp_resume()` and OpenSBI handoff. There is no private
SMP backend or Syntacore entry trampoline. SCR9 L3 reserves stacks for
eight harts and reads the active count from ICCM L3. L2 boards reserve
eight harts for SCR9/SCR7 and four for SCR5, matching the Linux DTS.
SCR7 L3 retains `MAX_CPUS=1`.

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

## SCR9 L3 stage transitions

The three stages reserve independent stack/HLS arrays in the 2 MiB
OCRAM. Each hart has one 4 KiB page, matching native RISC-V startup's
stride. At the default `MAX_CPUS=8` the stack regions are:

| Stage | Start | End (exclusive) |
|---|---|---|
| bootblock | `0x02046000` | `0x0204e000` |
| romstage | `0x0204e000` | `0x02056000` |
| ramstage | `0x02056000` | `0x0205e000` |

This prevents a hart entering the next stage from clearing HLS that
another hart still uses during native `smp_resume()`. Each stage also
publishes completion of hart-local initialization in its own initialized
data array. The platform hart-count hook waits for that publication
and native `smp_pause()`'s sleeping marker before returning. Without
this wait, native `smp_resume()` can skip a hart that has not yet parked.
The actual wakeup and callback protocol remains native coreboot code.

The existing `fw_dynamic` handoff enters OpenSBI with the physical hart
ID in `a0`, the shared FDT in `a1`, and dynamic info in `a2`, selecting
the working hart as boot hart. OpenSBI initializes secondary harts;
Linux detects SBI HSM and starts them through ordered booting.

## L2 stage transitions

L2 uses the same native initialization and parking-readiness wait.
Bootblock includes romstage, so only two independent stack/HLS banks
are needed. They remain in DDR, as in the original single-hart port:

| Boards | Bootblock bank | Ramstage bank |
|---|---|---|
| SCR9/SCR7 L2 | `0x0000..0x7fff` | `0x8000..0xffff` |
| SCR5 RV64/RV32 | `0x0000..0x3fff` | `0x4000..0x7fff` |

Each hart uses one 4 KiB page. Console, FMAP cache and CBFS cache follow
the two banks. Native startup continues to index pages by physical
hart ID; the Linux DTS describes IDs 0 through 7 or 0 through 3.

CSR ICCM exposes mailbox slot capacity, not an active-hart count. The
L2 hart-count hook therefore uses the CPU nodes in the Linux board FDT.
All described harts must execute at reset. Run SCR9/SCR7 with `-smp 8`
and SCR5 with `-smp 4`; a smaller emulated hart count requires a matching
board DTS. Disabled or sparse CPU configurations are not supported.

DDR must already be usable and coherent at firmware entry. This port
has no DDR training implementation; the stack expansion does not make
it suitable for reset into uninitialized DDR. SCR QEMU supplies usable
DDR. The equivalent loader/platform contract needs hardware validation.

## Validation and limitations

All six reference configurations compile with GCC 14.2.0. SCR QEMU
9.2.3 SCR9 L3 boots to U-Boot with 1, 2, 4 and 8 harts. Five repeated
runs each with 4 and 8 harts complete all three native resume operations
without missing harts. Linux 6.12 with eight harts reports SBI HSM and
`smp: Brought up 1 node, 8 CPUs`, then reaches the login prompt.
The stage stack addresses and acquire/release fences were checked in
the generated ELF files and disassembly.

L2 smoke tests cover SCR9/SCR7 with eight harts and SCR5 RV64/RV32 with
four. Three repeated boots per L2 configuration reach OpenSBI/U-Boot
without missed harts. Linux 6.12 reaches login with eight CPUs on SCR9
and SCR7 L2 and four CPUs on SCR5 RV64, detecting SBI HSM in each case.
No Linux RV32 image was available; SCR5 RV32 validation stops at U-Boot.
The early and ramstage stack banks were checked in all four L2 ELFs.

Earlier checks also cover SCR7 L3's single-hart flow, generic QEMU RV64
with four harts, and standalone two-hart ICCM send/wake/clear probes.

SCR9 L3 support here targets the physical IDs 0 through 7 described by
the Linux board DTS; native coreboot indexes stacks and HLS by physical
hart ID. Sparse IDs and reset configurations with a hart ID outside the
reserved array are unsupported. All harts reported by ICCM must start
and reach the stage's wait; a missing or faulting hart blocks boot.
The ICCM count does not replace this reset contract.

SCR7 L3 remains single-hart. No physical hardware testing has been
performed: reset behavior, SRAM/DDR availability and cache coherency
still require validation on real boards.
