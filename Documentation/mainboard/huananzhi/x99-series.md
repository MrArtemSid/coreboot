# Huananzhi X99-QD4 / X99-BD4: Grantley diagnostic bring-up

## Status

This is an **experimental, build-tested early diagnostic path**, not a bootable
mainboard port. No hardware execution, working serial output, DRAM training,
Broadwell-EP boot or OS boot has been demonstrated. Full `coreboot.rom` builds
are deliberately rejected. Do not flash these artifacts.

The implementation is separate from the client Haswell CPU/northbridge code:

- `src/cpu/intel/grantley`: generic non-eviction CAR entry and EP microcode
  selection, without client power-management or SMM initialization.
- `src/northbridge/intel/grantley`: CPU-model/PCH identification and a romstage
  flow running Intel Uncore PEIM via native PEI runner.
- `src/mainboard/huananzhi/x99-series`: board selection and early LPC UART setup.

The existing nested `x99-series` reference material is not used as executable
mainboard code.

## Hardware evidence and restrictions

| Configuration | LPC device observed in captures | Super I/O ID at port 0x2e |
|---------------|--------------------------------|---------------------------|
| X99-QD4       | Intel Q87, `8086:8c4e`          | `d121`                    |
| X99-BD4       | Intel B85, `8086:8c50`          | `c562`                    |

These are Lynx Point boards despite the X99 branding, not Wellsburg boards.
The checks intentionally reject a different PCH or Super I/O ID instead of
assuming that every revision with the same product name has the same wiring.
The Super I/O IDs are taken from the respective `logs/superiotool.log` captures;
model-name aliases in tools or stock PEIM names are not used as identification.

CPUID models `06_3f` and `06_4f` are recognized. Model classification ignores the
stepping field; it is **not** a socket, stepping, engineering-sample or board
compatibility guarantee. Available hardware captures show Haswell-EP `306f2`.
There is no Broadwell-EP hardware validation.

The default microcode inputs are production `306f2` and `406f1` updates from the
pinned Intel microcode submodule. Engineering-sample updates are not copied from
the stock image. In this pinned package `06-4f-01` is under
`intel-ucode-with-caveats`: Intel documents additional requirements for OS
late-loading. This path uses the existing pre-RAM loader, not OS late-loading.
The microcode update itself has not been exercised on these boards.

## Early flow

1. Use coreboot's generic x86 reset entry and non-eviction CAR implementation.
   The CAR candidate is 1 MiB at `0xfe100000`, matching the region recovered
   from the QD4 SEC. The generic CAR sequence is not the stock sequence and
   still requires hardware validation. Only the BSP is used.
2. Before PCH MMIO/decode setup, check the Intel CPU vendor/model and the selected
   board's LPC ID via PCI configuration mechanism 1. An identity mismatch emits
   POST `e0` and halts. ECAM is disabled: no client `PCIEXBAR` write is performed.
3. Reuse Lynx Point bootblock setup and verify the Super I/O ID before enabling
   UART logical device 2 at the configured COM1 I/O base. A mismatch emits POST
   `e1` and halts. Unknown GPIO/pin-multiplexing tables are not copied in.
4. Load the diagnostic romstage, print CPUID, host-bridge and LPC identity, emit
   POST `e3`, and halt with an explicit missing-DRAM-init message.

The diagnostic stack is 64 KiB. It is not intended to accommodate the stock
Uncore PEIM. No PEIM is packaged or called, no client ME memory-init helper is
compiled into the EP romstage, and no CBMEM/resource map or post-CAR transition
is fabricated.

## Build and verification

From the coreboot repository root, initialize the required pinned submodules:

```sh
git submodule update --init --depth=1 3rdparty/vboot
git -c submodule.intel-microcode.update=checkout submodule update \
    --init --depth=1 3rdparty/intel-microcode
```

With the coreboot toolchain installed:

```sh
python3 util/testing/test_grantley_diagnostic.py
```

For a compile/link check with the installed host compiler instead:

```sh
python3 util/testing/test_grantley_diagnostic.py --any-toolchain
```

The latter explicitly enables `ANY_TOOLCHAIN` only in the isolated test
configurations. It is not a supported toolchain or a hardware-validation claim.
The tests build both board configurations, verify IA32 ELF headers, required
entry symbols and absence of unresolved symbols, validate selected microcode
signatures/checksums, assert that client Haswell/ECAM paths are disabled, and
check that default, `coreboot`, and direct ROM builds fail with the intended
message. They also run the six Cmocka platform tests and propagate failures.

Outputs:

- `build/grantley-qd4/cbfs/fallback/{bootblock,romstage}.debug`
- `build/grantley-bd4/cbfs/fallback/{bootblock,romstage}.debug`
- `build/grantley-diagnostic-results.json`

The `.debug` files are linked ELF stages, not flash images. The test script does
not change the top-level `.config`, execute firmware or write any flash device.

## Remaining work

Boot support still requires EP-specific memory initialization, a verified PEI
service/policy ABI if the vendor Uncore path is used, a measured temporary-memory
layout, EP address-map/resource handling, CAR teardown, CPU/AP and SMM support,
ACPI/SMBIOS and board-specific device setup. The seven Uncore DEPEX providers
have been identified, but that alone does not establish a standalone MRC ABI.
QD4 stock firmware must not be treated as a validated BD4 initialization binary.
