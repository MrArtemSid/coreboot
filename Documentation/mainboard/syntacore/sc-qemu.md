# Syntacore SCR9 L3 on SCR QEMU

How to build and run the full `coreboot -> OpenSBI -> U-Boot -> Linux`
chain for the Syntacore SCR9 L3 cluster on the SCR QEMU machine that
ships with the Syntacore Development Toolkit (SC-DT).

Prerequisites: source SC-DT `env.sh`, have the `opensbi` and `u-boot`
Syntacore trees (sc/2026.04 branches).

## 0. Fetch the submodules and SC-DT

```bash
git submodule update --init 3rdparty/opensbi 3rdparty/u-boot
source <sc-dt>/env.sh   # SCR-HAL + toolchains (SC_HAL_PATH)
```

## 1. Build the payloads

OpenSBI `fw_dynamic` must be linked at the fixed address coreboot will
load it at (`CONFIG_OPENSBI_TEXT_START`, dram + 4 MiB on L3 platforms).
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

SCR machines enter the firmware at OCRAM `0x02000200` and do not set
up any firmware themselves, so the ROM image is loaded twice: the
bootblock (the first 128 KiB of `coreboot.rom`, linked at the reset
vector) into OCRAM, and the whole image into the CBFS window at
dram + 128 MiB (`0x408000000`):

```bash
qemu-system-riscv64 -M scr9_l3 -smp 1 -nographic \
    -device loader,file=build/coreboot.rom,addr=0x2000200 \
    -device loader,file=build/coreboot.rom,addr=0x408000000
```

Expected console: coreboot banner -> OpenSBI v1.7 (Platform Name:
IPDEV VCU118 SCR9 L3 prototype) -> U-Boot 2025.10 shell.

## 4. Boot Linux

Preload the kernel and the rootfs into DDR away from the boot flow
regions, interrupt the U-Boot autoboot, and boot. The kernel command
line (including `initrdmem=0x430000000,512M`) comes from the `chosen`
node of the board FDT that coreboot passes down the chain:

```bash
qemu-system-riscv64 -M scr9_l3 -smp 1 -nographic \
    -device loader,file=build/coreboot.rom,addr=0x2000200 \
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
