## SPDX-License-Identifier: GPL-2.0-only

unexport KCONFIG_AUTOHEADER
unexport KCONFIG_AUTOCONFIG
unexport KCONFIG_DEPENDENCIES
unexport KCONFIG_SPLITCONFIG
unexport KCONFIG_TRISTATE
unexport KCONFIG_NEGATIVES
unexport $(COREBOOT_EXPORTS)

CRABEFI_DIR := $(dir $(lastword $(MAKEFILE_LIST)))CrabEFI
CRABEFI_REPO ?= https://github.com/ArthurHeymans/CrabEFI
CRABEFI_TAG_OR_REV ?= origin/master
CRABEFI_CARGO_TARGET := x86_64-unknown-none
CRABEFI_CARGO_PACKAGE := crabefi-coreboot
CRABEFI_EXTRA_ENV :=

ifeq ($(CONFIG_ARCH_RISCV_RV64),y)
CRABEFI_CARGO_TARGET := riscv64gc-unknown-none-elf
CRABEFI_EXTRA_ENV := CC_riscv64gc_unknown_none_elf=$(CROSS_COMPILE)gcc PAYLOAD_BASE=$(CONFIG_CRABEFI_RISCV_PAYLOAD_BASE)
endif

ifeq ($(CONFIG_CRABEFI_DEBUG),y)
CRABEFI_BUILD_TYPE := debug
CRABEFI_CARGO_FLAGS :=
else
CRABEFI_BUILD_TYPE := release
CRABEFI_CARGO_FLAGS := --release
endif

ifeq ($(CONFIG_CRABEFI_UI),y)
CRABEFI_CARGO_FLAGS += --features ui
endif

CRABEFI_ELF := $(CRABEFI_DIR)/target/$(CRABEFI_CARGO_TARGET)/$(CRABEFI_BUILD_TYPE)/crabefi

build: crabefi_sync $(CRABEFI_ELF)

$(CRABEFI_DIR):
	git clone "$(CRABEFI_REPO)" "$(CRABEFI_DIR)"

# Re-fetch and check out the configured revision on every build so a pin
# change in the board config reaches an already-cloned tree. A local
# checkout is attempted first so offline rebuilds of an already-fetched
# revision keep working.
.PHONY: crabefi_sync
crabefi_sync: $(CRABEFI_DIR)
ifneq ($(CRABEFI_TAG_OR_REV),)
	git -C "$(CRABEFI_DIR)" checkout -q $(CRABEFI_TAG_OR_REV) 2>/dev/null || { \
		git -C "$(CRABEFI_DIR)" fetch origin "$(CRABEFI_TAG_OR_REV)" || \
		git -C "$(CRABEFI_DIR)" fetch origin; \
		git -C "$(CRABEFI_DIR)" checkout -q $(CRABEFI_TAG_OR_REV); }
endif

$(CRABEFI_ELF): crabefi_sync
	cd "$(CRABEFI_DIR)" && $(CRABEFI_EXTRA_ENV) cargo build --locked -p $(CRABEFI_CARGO_PACKAGE) --target $(CRABEFI_CARGO_TARGET) $(CRABEFI_CARGO_FLAGS)

clean:
	test -d "$(CRABEFI_DIR)" && cd "$(CRABEFI_DIR)" && cargo clean || true

distclean:
	rm -rf "$(CRABEFI_DIR)"

.PHONY: build clean distclean
