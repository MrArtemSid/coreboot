# Qualcomm MSM89x7 (MSM8917/MSM8937/MSM8940)

This board runs coreboot as a *second-stage* bootloader on msm89x7
devices: the stock Android bootloader (LK/aboot) loads coreboot from
the boot partition the same way it loads lk2nd — like an arm64 Linux
kernel — and hands over through TrustZone.

## Boot flow

1. The device's PBL/SBL initializes DRAM, loads the stock Android
   bootloader (LK) and TrustZone firmware.
2. LK reads the `boot` partition, detects the image as arm64 (the
   coreboot bootblock embeds an ARM64 kernel image header with the
   `ARM\x64` magic at offset 0x38), copies the device tree selected
   from the QCDT table to its tags address and prepares it.
3. LK switches to AArch64 via the TrustZone `elexec` call
   (`scm_elexec_call`) and enters coreboot at the first byte of the
   loaded image (0x80080000), with the DTB address in `x0`.
4. The coreboot bootblock saves the DTB pointer, sets up its MMU and
   console, and runs the usual bootblock → romstage → ramstage flow
   entirely from DRAM. Nothing that the primary bootloader already
   initialized (DRAM, clocks, pin muxing, PMIC) is touched again.
5. The payload (e.g. a FIT image with Linux and a mainline DTB) is
   loaded from CBFS and started. SMP bringup and power management
   come from the TrustZone firmware via PSCI.

## Building

```bash
make menuconfig   # mainboard: Qualcomm -> e.g. Xiaomi Redmi 4X (santoni)
make
```

When `MSM89X7_BOOTIMG` is enabled (the default), `make` also creates
`build/boot.img`, the image to flash to the boot partition. To embed a
device tree for the primary bootloader, set `MSM89X7_QCDT_FILE` (see
below); without it the boot image is created without a device tree.

The resulting `build/coreboot.rom` contains the bootblock at offset 0
followed by FMAP and CBFS; the whole file is what gets booted.

## Packaging as a boot image

The primary bootloader needs a device tree it can pass on, which on
this SoC generation is provided as a QCDT table inside the boot
image. Extract it from the device's stock boot image (e.g. dumped via
TWRP or adb from the `boot` partition) and point the build at it:

```bash
util/msm89x7-bootimg/msm89x7-bootimg.py extract-qcdt stock-boot.img -o stock-dt.img
# then select MSM89X7_QCDT_FILE=stock-dt.img in menuconfig and rebuild
```

For manual packaging (e.g. with a different payload):

```bash
util/msm89x7-bootimg/msm89x7-bootimg.py make \
    --kernel build/coreboot.rom --qcdt stock-dt.img -o boot.img
fastboot flash boot boot.img
```

The stock bootloader must be unlocked, since the image is unsigned.

## Debugging on a device

The console is drawn into the continuous splash framebuffer that the
stock bootloader leaves scanning on the panel
(`MSM89X7_DISPLAY_CONSOLE`). The framebuffer address, geometry and
pixel format are read from the live MDP5 pipe configuration, the
Kconfig values (`MSM89X7_FB_ADDR`, `MSM89X7_FB_WIDTH`,
`MSM89X7_FB_HEIGHT`) are only used if no active pipe is found.

There is no UART console driver yet. On santoni the BLSP1 UART2 clock
is off after the stock bootloader, and accessing the unclocked UART
results in a synchronous external abort and a reset.

`MSM89X7_RAMOOPS_CONSOLE` mirrors the log into the ramoops console
buffer of lk2nd, so it survives a warm reset and can be read with:

```bash
fastboot oem ramoops console
fastboot get_staged coreboot.log
```

`MSM89X7_DEBUG_WATCHDOG` additionally arms the APSS watchdog during
the boot and makes `die()` reset into fastboot mode, so a hang or a
fatal error returns the device to lk2nd automatically. Note that the
log only survives if the PMIC is configured for a warm reset on
PS_HOLD.

If the device does not boot, the stock bootloader's fastboot is still
available: restore the original system by flashing back the stock
boot image that was replaced.

## Memory map

The firmware regions marked `no-map` in `/reserved-memory` of the
device tree handed over by the primary bootloader are protected by
XPUs and are left out of the page tables. Mapping them cacheable lets
the CPU prefetch from them, which TrustZone answers with a reset.

## Payload

By default no payload is selected. A FIT payload with an arm64 Linux
kernel and a mainline DTB (e.g. `arch/arm64/boot/dts/qcom/`
`msm8937-*.dts` or `msm8940-*.dts`) can be added with:

```bash
build/cbfstool build/coreboot.rom add -f uImage -n fallback/payload -t fit
```

See `Documentation/lib/payloads/fit.md` for building the FIT image.
