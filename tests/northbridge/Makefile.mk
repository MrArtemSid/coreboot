# SPDX-License-Identifier: GPL-2.0-only

tests-y += grantley-platform-test
grantley-platform-test-srcs += tests/northbridge/intel/grantley-platform-test.c
grantley-platform-test-srcs += src/northbridge/intel/grantley/platform.c
grantley-platform-test-srcs += src/cpu/intel/grantley/platform.c

tests-y += grantley-spd-test
grantley-spd-test-srcs += tests/northbridge/intel/grantley-spd-test.c
grantley-spd-test-srcs += src/northbridge/intel/grantley/native_raminit/spd.c
grantley-spd-test-srcs += src/device/dram/ddr4.c
grantley-spd-test-srcs += src/device/dram/ddr_common.c
grantley-spd-test-srcs += tests/stubs/console.c

tests-y += grantley-pei-test
grantley-pei-test-srcs += tests/northbridge/intel/grantley-pei-test.c
grantley-pei-test-srcs += src/northbridge/intel/grantley/uncore_runner/pei_services.c
grantley-pei-test-srcs += src/drivers/efi/efivars.c
grantley-pei-test-srcs += src/commonlib/region.c
grantley-pei-test-srcs += tests/stubs/console.c
grantley-pei-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/
grantley-pei-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32/
grantley-pei-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Pi/
grantley-pei-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include/
grantley-pei-test-cflags += -Wno-attributes

tests-y += grantley-romstage-test
grantley-romstage-test-srcs += tests/northbridge/intel/grantley-romstage-test.c
grantley-romstage-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/
grantley-romstage-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32/
grantley-romstage-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Pi/
grantley-romstage-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include/
grantley-romstage-test-cflags += -Wno-attributes
grantley-romstage-test-cflags += -Wno-unused-variable -Wno-unused-function -Wno-unused-value
