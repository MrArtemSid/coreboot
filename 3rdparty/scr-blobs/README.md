Payload staging directory (not tracked in git).

Build the payloads and copy them here, then select them via
CONFIG_OPENSBI_BLOB_PATH and CONFIG_PAYLOAD_FILE (see
configs/config.syntacore_scr_l3_cluster and
Documentation/mainboard/syntacore/sc-qemu.md):

- fw_dynamic.elf  OpenSBI fw_dynamic linked at CONFIG_OPENSBI_TEXT_START
                  (the submodule in 3rdparty/opensbi, branch sc/2026.04)

The U-Boot payload is no longer staged here: with
CONFIG_PAYLOAD_UBOOT_SYNTACORE=y coreboot builds it from the
3rdparty/u-boot submodule (see payloads/external/U-Boot-Syntacore).
