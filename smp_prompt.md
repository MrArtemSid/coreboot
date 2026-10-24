You are an experienced firmware engineer specializing in RISC-V, coreboot, SMP initialization, and low-level boot firmware.

## Task

Implement SMP initialization and synchronization support for Syntacore RISC-V platforms in coreboot, using Syntacore Miniboot as a reference.

Repositories:
- coreboot: https://github.com/coreboot/coreboot
- Syntacore Miniboot: https://github.com/syntacore/miniboot
- Syntacore SCR-HAL: inspect the dependency used by Miniboot.

The implementation must integrate with coreboot's existing RISC-V SMP infrastructure rather than introducing a separate SMP framework.

## 1. Analyze existing implementations

First inspect the actual source code.

In coreboot, investigate:
- `src/arch/riscv/smp.c`
- `src/arch/riscv/ramstage.S`
- `src/arch/riscv/boot.c`
- RISC-V early entry and startup assembly
- `CONFIG_RISCV_WORKING_HARTID`
- `smp_pause()` and `smp_resume()`
- Per-hart stack and hart-local storage initialization
- The current OpenSBI payload handoff

In Miniboot, investigate:
- `src/cores_sync.c`
- `src/miniboot.c`
- `smp_slave_entry()`
- `plf_smp_hart_finit()`
- Atomic synchronization primitives
- Secondary hart waiting and release
- Cache and memory initialization dependencies

Verify that these files, symbols, and mechanisms exist in the checked-out revisions. Do not assume the referenced implementation is unchanged.

Identify what is reusable from coreboot and what requires Syntacore-specific implementation.

## 2. Architecture requirements

Use the following approach unless the actual hardware or coreboot architecture requires otherwise:

- All available harts may start executing after reset.
- One configured hart acts as the working/boot hart.
- Remaining harts enter a secondary waiting state.
- Synchronization uses shared memory, atomic operations, and spin-wait.
- Do not use IPIs for initial SMP synchronization unless strictly required.
- Reuse coreboot's existing SMP mechanisms.
- Do not duplicate Miniboot's synchronization framework.

Implement correct initialization and synchronization for all participating harts.

## 3. Early hardware initialization

Investigate the Syntacore-specific initialization requirements.

Pay particular attention to:
- Hart ID discovery using `mhartid`
- Per-hart stacks
- Cluster SRAM availability
- Early shared memory
- Cache state and coherency
- Memory ordering and RISC-V fences
- PMP / MPU / MRT setup where applicable
- Required per-hart initialization
- Required global initialization
- Hart-local storage

Ensure that all required hart-local initialization finishes before secondary harts enter their waiting state.

Do not assume that hart IDs are contiguous or start at zero.

Avoid using DRAM before it is initialized.

Do not assume that a `volatile` variable provides sufficient cross-hart synchronization.

## 4. SMP synchronization

Prefer the existing coreboot SMP implementation.

Verify:
- Shared synchronization memory is accessible to every participating hart.
- Synchronization works before DRAM initialization, where required.
- Appropriate atomic acquire/release semantics are used.
- Cache coherency requirements are satisfied.
- Secondary harts cannot observe partially initialized shared state.
- Hart-local stacks and storage cannot overlap.
- Secondary harts cannot execute uninitialized memory.
- No deadlocks or missed wakeups occur.

Use RISC-V atomic instructions only when the targeted hardware supports them.

If the early platform lacks suitable atomic support or coherent shared memory, determine and implement an appropriate platform-specific synchronization mechanism.

Avoid introducing a second SMP implementation unless necessary.

## 5. Boot flow

Implement and verify the following conceptual sequence:

```text
Reset
  |
  +-- Identify hart
  |
  +-- Establish per-hart stack
  |
  +-- Required hart-local initialization
  |
  +-- Required early memory/cache configuration
  |
  +-- Secondary harts enter coreboot SMP wait
  |
  +-- Working hart initializes platform
  |
  +-- Working hart loads OpenSBI
  |
  +-- coreboot releases secondary harts
  |
  +-- Handoff to OpenSBI
```

Adapt this flow to the actual coreboot stage architecture.

Do not assume that code executing in one boot stage can directly access synchronization state or stacks established in another stage.

Check whether secondary harts must participate in individual stages or can remain parked until the final handoff.

## 6. OpenSBI integration

Verify the actual OpenSBI boot protocol and the selected firmware configuration.

Check:
- Whether OpenSBI expects all harts or only one hart at entry.
- Coldboot and warmboot behavior.
- Correct `a0` and `a1` arguments.
- Device tree handling.
- Hart state initialization.
- Whether the eventual Linux boot uses ordered booting and SBI HSM.

Do not assume that all harts should jump directly to the same OpenSBI entry point without verifying its contract.

## 7. Implementation constraints

- Follow existing coreboot coding style.
- Prefer minimal, maintainable changes.
- Keep generic RISC-V functionality separate from Syntacore-specific code.
- Do not modify generic SMP code unless necessary.
- Avoid unnecessary IPI or interrupt-controller initialization.
- Do not introduce external dependencies without justification.
- Preserve existing supported RISC-V platforms.
- Add Kconfig and build integration only where necessary.
- Do not copy Miniboot source code blindly.

## 8. Validation

After implementation:

1. Run formatting and style checks.
2. Compile the affected coreboot configuration.
3. Inspect generated assembly for early startup paths.
4. Check that all secondary hart waiting paths are reachable and safe.
5. Verify memory ordering and synchronization.
6. Verify that hart IDs and stack addresses are correct.
7. Verify OpenSBI entry arguments and hart handoff.
8. Check for regressions on other RISC-V platforms.

If hardware testing is unavailable, clearly distinguish compile-time validation from hardware verification.

## 9. Deliverables

Provide:

1. A concise analysis of how SMP works in Miniboot and coreboot.
2. An explanation of the chosen synchronization strategy.
3. Actual source code changes in the working tree.
4. A list of modified files and why each change is required.
5. Build and test results.
6. Remaining hardware-dependent assumptions and risks.

## Important

Do not stop after producing a design document.

Implement the changes in the repository.

Do not invent hardware registers, CSR definitions, memory addresses, or initialization sequences.

If platform-specific information is missing, inspect the existing platform code, SCR-HAL, available headers, or hardware documentation first.

If something cannot be verified, explicitly identify the assumption and avoid presenting it as confirmed.

Keep the implementation as close as practical to existing coreboot mechanisms, using Miniboot primarily to understand Syntacore-specific hardware requirements.
