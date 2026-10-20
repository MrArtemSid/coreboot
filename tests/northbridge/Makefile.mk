# SPDX-License-Identifier: GPL-2.0-only

tests-y += haswell_ep-platform-test
haswell_ep-platform-test-srcs += tests/northbridge/intel/haswell_ep-platform-test.c
haswell_ep-platform-test-srcs += src/northbridge/intel/haswell_ep/platform.c

tests-y += haswell_ep-spd-test
haswell_ep-spd-test-srcs += tests/northbridge/intel/haswell_ep-spd-test.c
haswell_ep-spd-test-srcs += src/northbridge/intel/haswell_ep/native_raminit/spd.c
haswell_ep-spd-test-srcs += src/device/dram/ddr4.c
haswell_ep-spd-test-srcs += src/device/dram/ddr_common.c
haswell_ep-spd-test-srcs += tests/stubs/console.c
