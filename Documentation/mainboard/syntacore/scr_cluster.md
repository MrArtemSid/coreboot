# Syntacore SCR cluster

This board supports RV64 SCR9 and SCR7 L2/L3 clusters on the VCU118 FPGA
and the Syntacore Development Toolkit (SC-DT) QEMU machines. The initial
boot flow requires miniboot; direct reset entry is not supported.

## Boot flow

Miniboot runs from OCRAM and initializes PMP/PMA, caches, MRT, the machine
timer and per-hart feature CSRs. It transfers all harts to coreboot at the
DDR base. coreboot preserves that hardware configuration, uses ICCM for
IPIs, and obtains its device tree from CBFS when miniboot's FDT is invalid.
The Linux device-tree sources are in `3rdparty/syntacore/linux`.

The memory layout relative to DDR is:

| Offset | Use |
| --- | --- |
| `0` | Bootblock (128 KiB), including romstage |
| `0x20000` | Ramstage (2 MiB) |
| `0x220000` | Console and metadata caches |
| `0x300000` | Bootblock per-hart stacks |
| `0x400000` | Ramstage per-hart stacks |
| `0x2200000`–`0x2400000` | Unused miniboot FDT window |
| `0x4000000` | OpenSBI (1 MiB) |
| `0x8000000` | Read-only CBFS ROM window (16 MiB) |
| `0x10000000` | CrabEFI or U-Boot |

DDR starts at `0x400000000` for L3 and zero for L2. OpenSBI is built from
`3rdparty/syntacore/opensbi`, using its generic platform. coreboot copies
the next-stage FDT to CBMEM and transfers all harts to OpenSBI.

## Build

Install SC-DT and export the external SCR-HAL source directory. SCR-HAL is
not part of coreboot; only its ICCM accessors and cache maintenance code
are used, with isolated compiler flags rather than HAL runtime startup.
`SC_HAL_PATH` takes precedence over `CONFIG_SCR_HAL_DIR`.

```sh
source /path/to/sc-dt/env.sh
export SC_HAL_PATH=/path/to/sc-dt/workspace/scr-hal
git submodule update --init 3rdparty/syntacore/opensbi \
    3rdparty/syntacore/linux 3rdparty/syntacore/miniboot
cp configs/config.syntacore_scr_cluster_scr9_l3_qemu_crabefi .config
make menuconfig
make
```

Select a suitable RISC-V toolchain. Building OpenSBI from source requires
`CONFIG_COMPILER_LLVM_CLANG=y`; external toolchains also require
`CONFIG_ANY_TOOLCHAIN=y`. CrabEFI requires Rust with the
`riscv64gc-unknown-none-elf` target and its repository toolchain installed.
The defconfigs select Clang and source-built OpenSBI and pin CrabEFI to
`ba37d2755e74`. The graphical UI is disabled for serial-only QEMU boot.

When switching clusters, remove the shared CrabEFI build directory
`payloads/external/CrabEFI/CrabEFI` before rebuilding: its non-relocatable
payload link address changes. Use the corresponding `scr9_l2` defconfig
for L2. FPGA configurations must leave `CONFIG_SCR_CLUSTER_QEMU` disabled.

The `*_uboot` defconfigs build the Syntacore U-Boot fork
(`sc/2026.04`, `scr_rv64_l{2,3}_cluster_opensbi_defconfig`) as an S-mode
payload, relinked at the payload address above, with the device tree
coreboot passes in `a1`. They set `CONFIG_UBOOT_CROSS_COMPILE` to the
SC-DT `riscv64-unknown-linux-gnu-` toolchain, which must be in `PATH`.
The fork's configuration has no virtio, block device or EFI support; it
boots a kernel loaded into memory or over TFTP with `booti`.

Build miniboot from the same SC-DT environment (substitute `l2` for `l3`
when needed):

```sh
cmake -S 3rdparty/syntacore/miniboot -B /path/to/miniboot-l3 \
    -DPLATFORM=rv64_pmp_l3 \
    -DTOOLCHAIN_FILE="$SC_CMAKE_TOOLCHAINS/riscv64-elf-gcc.cmake" \
    -DINSTANT_BOOT=ON -DJUMP_NEXT_STAGE=ON
make -C /path/to/miniboot-l3
```

## SC-DT QEMU

Load bootblock separately at DDR and the complete ROM at the CBFS window.
Use a writable snapshot of a test disk, not a writable pristine image.
For L3:

```sh
/path/to/sc-dt/tools/bin/qemu-system-riscv64 \
    -M scr9_l3,plic-msi-ver=2,mrt=on -smp 8 -nographic \
    -global syntacore-scr9-riscv-cpu.pma=on \
    -global virtio-mmio.force-legacy=false \
    -bios none \
    -device loader,file=/path/to/miniboot-l3/miniboot.elf,cpu-num=0 \
    -device loader,file=build/cbfs/fallback/bootblock.bin,addr=0x400000000 \
    -device loader,file=build/coreboot.rom,addr=0x408000000 \
    -drive file=/path/to/test.img,format=raw,if=none,id=hd0,snapshot=on \
    -device virtio-blk-device,drive=hd0
```

For L2 use `-M scr9_l2,mrt=on`, omit the PMA global, use the L2 miniboot,
and set the two loader addresses to `0x0` and `0x8000000` respectively.
The QEMU device tree enables modern virtio transports and, on SCR9,
Zicbom (SC-DT QEMU does not implement it for SCR7). Mainboard
initialization sets PLIC sources to level-high. L3 requires
`plic-msi-ver=2` for the register layout expected by stock Linux.
L2 omits the first two virtio transports because they overlap cluster
registers. A GRUB Linux command line should include
`console=ttyS0,115200 efi=novamap` to retain the serial console.

## AIA IPI backend

`CONFIG_SCR_IPI_IMSIC=y` selects machine-level IMSIC IPIs instead of
ICCM on SCR9/SCR7 L2 and L3. Like ICCM, the controller is driven through
the SCR-HAL AIA API. The `arch/riscv` wake-up hook is needed because
IMSIC IPIs arrive as MEIP rather than MSIP. Each sleeping hart enables
identity 1; the next stage clears it before sleeping again, and OpenSBI
initializes the controller for the payload. SCR-HAL platform headers do
not describe AIA yet, so the SC-DT QEMU addresses are used; they have
not been verified on FPGA. The QEMU overlay connects peripheral
interrupts through S-mode APLIC to S-mode IMSIC, without guest pages
(`aia-guests=0`).
For example, in the L3 command above replace the machine argument with:

```sh
-M scr9_l3,aia=aplic-imsic,aia-guests=0,mrt=on,ocram-size=0x80000
```

Use `scr9_l2,aia=aplic-imsic,aia-guests=0,mrt=on` for L2; retain the
corresponding loader addresses and PMA settings described above.

