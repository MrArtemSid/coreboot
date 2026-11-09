# Grantley helpers

Tools for the Intel Grantley (LGA2011-3, Haswell-EP / Broadwell-EP) boards.

## extract_blobs.py

Pulls the four vendor inputs a Grantley port needs out of a stock image:

| blob             | where it comes from                                          |
|------------------|--------------------------------------------------------------|
| `descriptor.bin` | flash descriptor region                                      |
| `me.bin`         | ME region                                                    |
| `uncore.bin`     | PE32 image of the `UncoreInitPeim` module of the BIOS region  |
| `intelsetup.bin` | `IntelSetup` NVAR variable, the setup structure the PEIM reads |

The module is matched by its user interface section rather than by a file GUID,
because the GUID is not stable across firmware releases. The variable is
taken from the last copy in the store, which is the one the board last ran
with; the store keeps the payload in `IntelSetup`, terminated by a NUL that is
not part of the structure.

```
$ util/grantley/extract_blobs.py --print-regions stock.rom
descriptor 00000000 - 00001000  4096 bytes
bios       00800000 - 01000000  8388608 bytes
me         00001000 - 00800000  8384512 bytes

$ util/grantley/extract_blobs.py -o 3rdparty/blobs/mainboard/vendor/board stock.rom
descriptor.bin       4096 bytes  sha256 ...
me.bin            8384512 bytes  sha256 ...
uncore.bin         604832 bytes  sha256 ...
intelsetup.bin       6090 bytes  sha256 ...
```

The image must be a full flash dump, not a BIOS region alone: the descriptor is
what tells the tool where the regions are. A board that has no `IntelSetup`
variable of its own is served by the vendor image as well, but note that the
structure size is model specific — use `--setup-size` when it is not 6090 bytes
(the Haswell-EP size).

The mainboard then consumes the files from `3rdparty/blobs` through
`CONFIG_IFD_BIN_PATH`, `CONFIG_ME_BIN_PATH`, `CONFIG_INTEL_UNCORE_BIN_FILE` and
`CONFIG_GRANTLEY_INTELSETUP_FILE`; the module is placed in CBFS at its PE image
base and the setup variable is served to it as the `IntelSetup` variable.

## extract_iio_recipe.py

Generates `src/northbridge/intel/grantley/iio_phy_recipe.h`, the IIO PCIe PHY
receiver settings of Haswell-EP, from a stock image. The vendor firmware
applies them from its `IioInit` DXE module before the links train: a common
set, a set selected by the CPU stepping and, from B0 on, a CTLE update for
parts that are not fused for DCU mode.

The tables are located through the code that hands them to the register
writer, so no offset inside the module is assumed, and the settings the
writer would skip on the DMI port are left out the same way.

```
$ util/grantley/extract_iio_recipe.py --summary stock.rom
common at 0x3db0, 216 entries
C0     at 0x4e90,  16 entries
B0     at 0x4fd0,  17 entries
A0     at 0x5130,   4 entries
ctle   at 0x5180,  11 entries
DMI port registers: 0x12004480, 0x12004464 (and != 0xfff81fff), ...

$ util/grantley/extract_iio_recipe.py -o src/northbridge/intel/grantley/iio_phy_recipe.h stock.rom
```

The input can be a full flash image, a BIOS region or the `IioInit` PE32
image itself.
