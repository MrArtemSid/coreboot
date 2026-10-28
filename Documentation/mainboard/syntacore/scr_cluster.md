# Syntacore SCR cluster

This board supports RV64 SCR9 and SCR7 L2/L3 clusters on the VCU118 FPGA
and the Syntacore Development Toolkit (SC-DT) QEMU machines. Select either
miniboot handoff (the default) or direct reset entry with SCR-HAL initialization.

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

## Reset-vector flow

`CONFIG_SCR_BOOT_HAL=y` initializes the platform in bootblock on every
hart, before generic SMP synchronization. The working hart initializes
shared caches, final memory types, MRT, features, the machine timer and
ICCM before releasing the other harts. An enabled L2 is never invalidated
again. Later stages and the miniboot flow do not perform this setup.
Reset does not supply a valid FDT pointer; this flow always uses CBFS `DTB`.

`CONFIG_SCR_OCRAM_SIZE_SELECT` picks the pre-RAM layout. The 512 KiB
layout, available on SCR9 L3 only, starts bootblock, including romstage,
at OCRAM offset `0x200` with a budget ending at 128 KiB; console, FMAP
and CBFS metadata occupy offsets 128–148 KiB, followed by 4 KiB per hart
of stacks. All pre-RAM data stays in OCRAM: DDR is outside medany reach.

The 64 KiB layout is the default on SCR7 L3 and on L2, and is also
available on SCR9 L3. All pre-RAM code, data, eight hart stacks and
console live in OCRAM (`0x2000000` on L3, `0xffffffffffff0000` on L2):

| OCRAM offset | Size | Use |
| --- | --- | --- |
| `0` | 512 B | Reserved for the QEMU clock blob |
| `0x200` | 27.5 KiB | Bootblock, including romstage, data and BSS |
| `0x7000` | 32 KiB | Eight 4 KiB per-hart stacks, including HLS |
| `0xf000` | 3840 B | Pre-RAM console |
| `0xff00` | 256 B | L2 reset vector: a jump at reset PC `0xffffffffffffff00` |

It uses LZ4 ramstage compression, avoiding the LZMA scratch buffer, and
disables FMAP and CBFS metadata caches. Clang builds bootblock with `-Oz`.
RAMSTAGE, ramstage stacks, OpenSBI, CBFS and payload offsets remain as
in the miniboot layout. DDR is first accessed in romstage, after SCR-HAL
initialization and the optional key wait: for the CBFS DTB, then for
CBMEM and ramstage. Link-time assertions bound every pre-RAM region and
the reset vector; the HAL build also rejects a size larger than the
platform supports.

On L3, `CONFIG_SCR_WAIT_KEY_ON_FIRST_BOOT` defaults to enabled. If the
active MRT catch-all entry was missing, coreboot initializes MRT, prints
"Press any key", and waits on the coreboot console at the start of
romstage, before the first DDR access. Load the ROM into DDR before
continuing on FPGA. QEMU's reset also resets MRT, so a QMP system reset
may prompt again. L2 has no MRT and no such option: load CBFS into DDR
before releasing reset.

Use `config.syntacore_scr_cluster_scr9_l3_qemu_hal_crabefi` (or `l2`)
for this flow; select SCR7 or the 64 KiB layout in menuconfig. No miniboot loader is needed. The L3 QEMU command is:

```sh
/path/to/sc-dt/tools/bin/qemu-system-riscv64 \
    -M scr9_l3,plic-msi-ver=2,mrt=on,ocram-size=0x80000 -smp 8 -nographic \
    -global syntacore-scr9-riscv-cpu.pma=on \
    -global virtio-mmio.force-legacy=false -bios none \
    -device loader,file=build/cbfs/fallback/bootblock.bin,addr=0x2000200 \
    -device loader,file=build/coreboot.rom,addr=0x408000000 \
    -drive file=/path/to/test.img,format=raw,if=none,id=hd0,snapshot=on \
    -device virtio-blk-device,drive=hd0
```

For L2 use `-M scr9_l2,mrt=on`, omit the PMA global and set the loader
addresses to `0xffffffffffff0200` and `0x8000000`. With the 64 KiB
layout on L3 use `ocram-size=0x10000`; SCR7 uses `-M scr7_l3` without the
PMA global. L3 also supports the QEMU default OCRAM size, a superset of
both layouts. Do not enable
PMA enforcement on L2: SC-DT QEMU faults on the reset vector even with
stock miniboot.

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

## Source layout

`src/soc/syntacore/scr/` contains the code shared by both boot flows.
`miniboot/` holds the DDR layout of the miniboot flow, `hal/` the reset
entry, SCR-HAL initialization and OCRAM layouts, and `iccm/` both IPI
backends with their isolated SCR-HAL accessors. The top-level
`memlayout.ld` selects the layout for the configured flow.

## Devicetree

`CONFIG_RISCV_DTS_FILE` ("Devicetree source file" in menuconfig) defaults
to `dts/scr_cluster.dts`. It includes the Linux `scr9.dtsi` or `scr7.dtsi`
and `vcu118_l3.dtsi` or `vcu118_l2.dtsi`. With `CONFIG_SCR_CLUSTER_QEMU`
it adds `qemu.dtsi` and either `qemu_iccm.dtsi` or `qemu_aia.dtsi`, with
values in `qemu-macros.dtsi` and `aia-macros.dtsi`.

A different file may be set as an absolute path or relative to the
coreboot tree. It is preprocessed with the board `dts/` and Linux
Syntacore directories on the include path and with the `SCR_*` macros
defined, so a board variant can extend the default:

```
#include "scr_cluster.dts"

/ {
	model = "My SCR board";
};
```

## Build

Install SC-DT and export the external SCR-HAL source directory. SCR-HAL is
not part of coreboot. ICCM accessors and cache maintenance are used in
both flows; reset entry additionally builds isolated platform initialization
objects, without linking HAL console, allocation or startup runtime.
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
