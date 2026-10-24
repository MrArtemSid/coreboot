# Syntacore SCR boards on SCR QEMU

How to build and run the full `coreboot -> OpenSBI -> U-Boot -> Linux`
chain for the Syntacore SCR clusters on the SCR QEMU machines that ship
with the Syntacore Development Toolkit (SC-DT).

## Platform matrix

| Board config | QEMU machine | SoC family | Memory typing | Validated to |
|---|---|---|---|---|
| config.syntacore_scr9_l3 | scr9_l3 (riscv64) | L3, 2 MiB OCRAM | PMP + PMA | Linux userspace (root login) |
| config.syntacore_scr7_l3 | scr7_l3 (riscv64) | L3, 64 KiB OCRAM | PMP + PMA | Linux userspace (root login) |
| config.syntacore_scr9_l2 | scr9_l2 (riscv64) | L2, 64 KiB OCRAM | PMP + PMA | Linux userspace (root login) |
| config.syntacore_scr7_l2 | scr7_l2 (riscv64) | L2, 64 KiB OCRAM | PMP + PMA | U-Boot shell |
| config.syntacore_scr5_rv64 | scr5 (riscv64) | L2, 64 KiB OCRAM | MPU | U-Boot shell |
| config.syntacore_scr5_rv32 | scr5 (riscv32) | L2, 64 KiB OCRAM | MPU | U-Boot shell |

The matrix records earlier single-hart payload validation. The ICCM
change was rebuilt for all six boards; current smoke-test results and
limitations are recorded in [ICCM support](iccm.md).

Prerequisites: source SC-DT `env.sh`, have the `opensbi` and `u-boot`
Syntacore trees (sc/2026.04 branches).

## 0. Fetch the submodules and SC-DT

```bash
git submodule update --init --depth 1 3rdparty/opensbi 3rdparty/u-boot 3rdparty/linux
source <sc-dt>/env.sh   # SCR-HAL + toolchains (SC_HAL_PATH)
```

## 1. Build the payloads

OpenSBI `fw_dynamic` must be linked at the fixed address coreboot will
load it at (`CONFIG_OPENSBI_TEXT_START`, dram + 64 MiB).
Build it with clang + GNU bfd (the Syntacore sc-dt LLVM `ld.lld` needs
`libxml2.so.2`; clang + the linux-gnu bfd linker supports PIE):

```bash
cd 3rdparty/opensbi
make CC=clang CROSS_COMPILE=riscv64-unknown-linux-gnu- \
     AR=llvm-ar OBJCOPY=llvm-objcopy \
     PLATFORM=generic O=build \
     FW_JUMP=n FW_DYNAMIC=y FW_PAYLOAD=n \
     FW_TEXT_START=0x404000000 -j$(nproc)
cp build/platform/generic/firmware/fw_dynamic.elf \
   <coreboot>/3rdparty/scr-blobs/
```

U-Boot (S-mode payload, entry at dram + 2 MiB):

```bash
cd 3rdparty/u-boot
make O=build CROSS_COMPILE=riscv64-unknown-linux-gnu- \
     scr_rv64_l3_cluster_opensbi_defconfig
sed -i 's/CONFIG_TEXT_BASE=0x400200000/CONFIG_TEXT_BASE=0x402000000/' build/.config
make O=build CROSS_COMPILE=riscv64-unknown-linux-gnu- -j$(nproc)
cp build/u-boot <coreboot>/3rdparty/scr-blobs/u-boot
```

## 2. Build coreboot

```bash
cd coreboot
cp configs/config.syntacore_scr_l3_cluster .config
make CROSS_COMPILE=riscv64-unknown-elf- olddefconfig
make CROSS_COMPILE=riscv64-unknown-elf- BUILD_TIMELESS=1 -j$(nproc)
```

SCR-HAL is picked up from `SC_HAL_PATH` (exported by the SC-DT
`env.sh`); alternatively set `CONFIG_SCR_HAL_DIR` to the scr-hal
directory.

## 3. Run on SCR QEMU

SCR machines do not set up any firmware at reset, so the ROM image is
loaded by hand.

### L3 family (scr9_l3, scr7_l3)

The machine reset PC is `OCRAM + 0x200`. Load only the extracted
bootblock into OCRAM and load the whole ROM into the CBFS window:

```bash
qemu-system-riscv64 -M scr9_l3 -smp 1 -nographic \
    -device loader,file=build/cbfs/fallback/bootblock.bin,addr=0x2000200 \
    -device loader,file=build/coreboot.rom,addr=0x408000000
```

Expected console: coreboot banner -> OpenSBI v1.7 (Platform Name:
IPDEV VCU118 SCR9 L3 prototype) -> U-Boot 2025.10 shell.

### L2 family (scr9_l2, scr7_l2, scr5)

The 64 KiB OCRAM sits at the very top of the address space and the
machine reset PC is its last page. Like miniboot (`.reset_vector` in
its ocram.ld), the bootblock carries a small `.reset_vector` section
that the board memlayout places at the reset PC
(`CONFIG_SCR_RESET_STUB_ADDR`) with a linker-resolved `j _start` into
the bootblock entry - so two pieces are loaded: the CBFS window at
dram + 128 MiB (`0x08000000`) and the bootblock (the first 128 KiB of
the ROM) at `OCRAM + 0x200`. The whole span (~64 KiB incl. the reset
vector) stays below the 128 KiB FMAP BOOTBLOCK region and is loaded as
one file; on rv32 the load wraps past the end of the 32-bit space,
which the SCR QEMU loader handles. A standalone 4-byte extraction of
the reset vector (`build/scrhal/reset_stub.bin`) is also produced for
flows that need the stub separately:

```bash
qemu-system-riscv64 -M scr9_l2 -smp 1 -nographic \
    -device loader,file=build/coreboot.rom,addr=0x08000000 \
    -device loader,file=build/cbfs/fallback/bootblock.bin,addr=0xffffffffffff0200
```

(`bootblock.bin` = `cbfstool build/coreboot.rom read -r BOOTBLOCK`;
for the rv32 board run `qemu-system-riscv32 -M scr5` with the same
loaders at `0xffff0200`.)

Expected console: coreboot banner (bootblock + inline romstage from
the 64 KiB OCRAM, stack/preram data at the bottom of DDR) -> OpenSBI
v1.7 -> U-Boot 2025.10 shell.

## 4. Boot Linux (L3 addresses shown; L2 uses the addresses below)

Preload the kernel and the rootfs into DDR away from the boot flow
regions, interrupt the U-Boot autoboot, and boot. The kernel command
line (including `initrdmem=0x430000000,512M`) comes from the `chosen`
node of the board FDT that coreboot passes down the chain:

```bash
qemu-system-riscv64 -M scr9_l3 -smp 1 -nographic \
    -device loader,file=build/cbfs/fallback/bootblock.bin,addr=0x2000200 \
    -device loader,file=build/coreboot.rom,addr=0x408000000 \
    -device loader,file=$OS_PACK/linux/Image,addr=0x410000000 \
    -device loader,file=$OS_PACK/rootfs.cpio,addr=0x430000000
```

In the U-Boot prompt (press a key during the autoboot countdown):

```
booti 0x410000000 - 0x400019ff0
```

`0x400019ff0` is the FDT coreboot installed (printed as
`## Flattened Device Tree blob at ...`); `-` means "no initrd
argument" - the kernel picks the rootfs up through the
`initrdmem=0x430000000,512M` command line baked into the board FDT.

Login: `root` / `root`.

## ICCM and SMP scope

Both CSR ICCM and MMIO ICCM L3 implement the native coreboot
`set_msip()` hook. They deliver machine software interrupts and can wake
the native `smp_pause()` wait. See [ICCM support](iccm.md).

SCR9 L3 reserves eight per-hart stack pages in each stage and reads the
active count from ICCM L3. Use `-smp 8` to run all eight harts described
by its Linux DTS. This configuration reaches Linux userspace with eight
CPUs through the native coreboot/OpenSBI handoff and SBI HSM.

Other reference configurations retain `MAX_CPUS=1`; use `-smp 1` for
those images. Their memory layouts have not been adapted for multiple
harts. See [ICCM support](iccm.md) for the validated stack addresses,
startup requirements and hardware limitations.

## Memory map (SCR9 L3, dram base 0x400000000)

| Offset from DDR | Region |
|---|---|
| +0x0000000 | ramstage (2 MiB) |
| +0x0020000 | preram console leftovers / cbtables (cbmem top: +512 MiB) |
| +0x4000000 | OpenSBI `fw_dynamic` (1 MiB, `CONFIG_OPENSBI_TEXT_START`) |
| +0x8000000 | CBFS ROM window (`coreboot.rom`, read-only) |
| OCRAM 0x02000000 + 0x200 | bootblock (128 KiB) |
| OCRAM +0x21000 | romstage (128 KiB) |
| OCRAM +0x41000.. | preram console, FMAP cache, CBFS mcache, stack |

Keep the region above dram + 2 MiB (where U-Boot relocates the Linux
Image) clear of locked firmware regions.

L2-family addresses: kernel preload `0x0A000000`, initrd
`0x30000000` (the `initrdmem` baked into the L2 device trees), FDT
address as printed by U-Boot, `booti 0x0A000000 - <fdt>`.
