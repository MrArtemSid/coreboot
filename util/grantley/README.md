# Grantley helpers

Tools for the Intel Grantley (LGA2011-3, Haswell-EP / Broadwell-EP) boards.

## extract_blobs.py

Pulls the four vendor inputs a Grantley port needs out of a stock image:

| blob             | where it comes from                                          |
|------------------|--------------------------------------------------------------|
| `descriptor.bin` | flash descriptor region                                      |
| `me.bin`         | ME region                                                    |
| `uncore.bin`     | PE32 image of the `UncoreInitPeim` module of the BIOS region  |
| `intelsetup.bin` | `IntelSetup` NVAR variable, the RC `SYSTEM_CONFIGURATION`     |

The module is matched by its user interface section rather than by a file GUID,
because the GUID is not stable across reference code releases. The variable is
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
