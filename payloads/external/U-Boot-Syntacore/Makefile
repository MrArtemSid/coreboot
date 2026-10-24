## SPDX-License-Identifier: GPL-2.0-only

# Builds the Syntacore U-Boot S-mode payload from the 3rdparty/u-boot
# submodule (github.com/syntacore/u-boot, branch sc/2026.04). coreboot
# selects the platform defconfig (CONFIG_UBOOT_SCR_DEFCONFIG) and moves
# the link address to CONFIG_UBOOT_SCR_TEXT_BASE, the entry it passes
# to OpenSBI through the fw_dynamic info. The toolchain must be in
# PATH: source the SC-DT env.sh (SC_GCC_PATH, linux-gnu GCC prefix).

project_name=U-Boot (Syntacore)
project_build_dir=build
project_config_file=$(project_build_dir)/.config

project_src := $(CURDIR)/../../../3rdparty/u-boot

make_args=-C $(project_src) O=$(CURDIR)/$(project_build_dir) \
	CROSS_COMPILE=$(CONFIG_UBOOT_SCR_CROSS_COMPILE)

unexport KCONFIG_AUTOHEADER
unexport KCONFIG_AUTOCONFIG
unexport KCONFIG_DEPENDENCIES
unexport KCONFIG_SPLITCONFIG
unexport KCONFIG_TRISTATE
unexport KCONFIG_NEGATIVES
unexport $(COREBOOT_EXPORTS)

ifeq ($(wildcard $(project_src)/Makefile),)
$(error U-Boot submodule not found - run: git submodule update --init --depth 1 3rdparty/u-boot)
endif

all: build

config:
	mkdir -p $(project_build_dir)
	rm -f $(project_config_file)
	$(MAKE) $(make_args) $(CONFIG_UBOOT_SCR_DEFCONFIG)
	echo "CONFIG_TEXT_BASE=$(CONFIG_UBOOT_SCR_TEXT_BASE)" >> $(project_config_file)
	$(MAKE) $(make_args) olddefconfig

build: config
	echo "    MAKE       $(project_name)"
	$(MAKE) $(make_args)

clean:
	test -d $(project_build_dir) && $(MAKE) $(make_args) clean || exit 0

distclean:
	rm -rf $(project_build_dir)

print-repo-info:
	echo "https://github.com/syntacore/u-boot.git (3rdparty/u-boot, sc/2026.04) $(project_src)"

.PHONY: all config build clean distclean print-repo-info
