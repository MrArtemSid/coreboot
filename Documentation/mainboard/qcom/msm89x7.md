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
make menuconfig   # mainboard: Qualcomm -> Generic MSM8917/MSM8937/MSM8940
make
```

The resulting `build/coreboot.rom` contains the bootblock at offset 0
followed by FMAP and CBFS; the whole file is what gets booted.

## Packaging as a boot image

The primary bootloader needs a device tree it can pass on, which on
this SoC generation is provided as a QCDT table inside the boot
image. Extract it from the device's stock boot image:

```bash
util/msm89x7-bootimg/msm89x7-bootimg.py extract-qcdt stock-boot.img -o stock-dt.img
```

Then package coreboot:

```bash
util/msm89x7-bootimg/msm89x7-bootimg.py make \
    --kernel build/coreboot.rom --qcdt stock-dt.img -o boot.img
fastboot flash boot boot.img
```

The stock bootloader must be unlocked, since the image is unsigned.

## Payload

By default no payload is selected. A FIT payload with an arm64 Linux
kernel and a mainline DTB (e.g. `arch/arm64/boot/dts/qcom/`
`msm8937-*.dts` or `msm8940-*.dts`) can be added with:

```bash
build/cbfstool build/coreboot.rom add -f uImage -n fallback/payload -t fit
```

See `Documentation/lib/payloads/fit.md` for building the FIT image.
