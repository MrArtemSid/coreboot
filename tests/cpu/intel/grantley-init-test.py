#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Standalone Grantley CPU tests; no firmware MSRs execute on the host."""
import ctypes
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
CPU = ROOT / "src/cpu/intel/grantley"


class GrantleyInitTest(unittest.TestCase):
    def test_mp_and_driver_are_connected(self):
        source = (CPU / "cpu_init.c").read_text() if (CPU / "cpu_init.c").exists() else ""
        self.assertIn("mp_init_with_smm(cpu_bus, &grantley_mp_ops)", source,
                      "CPU bus must start the common MP/SMM flight plan")
        self.assertIn("__cpu_driver", source)
        self.assertIn("0x306f0, CPUID_ALL_STEPPINGS_MASK", source)
        self.assertIn("0x406f0, CPUID_ALL_STEPPINGS_MASK", source)
        self.assertIn("x86_setup_mtrrs_with_detect()", source)
        self.assertIn("grantley_smm_relocation_handler", source)
        self.assertIn("get_microcode_info", source)
        self.assertIn("soc_skip_ucode_update(get_current_microcode_rev()", source)
        self.assertIn("intel_microcode_load_unlocked(intel_microcode_find())", source)
        northbridge = (ROOT / "src/northbridge/intel/grantley/northbridge.c").read_text()
        self.assertNotIn("void mp_init_cpus(", northbridge)

    def test_ssdt_enumerates_without_invented_power_states(self):
        source = (CPU / "acpi.c").read_text() if (CPU / "acpi.c").exists() else ""
        self.assertIn("dev_count_cpu()", source)
        self.assertIn("acpigen_write_processor(i, 0, 0)", source)
        self.assertIn("acpigen_pop_len()", source)
        self.assertNotIn("acpigen_write_PSS", source)
        self.assertNotIn("acpigen_write_CST", source)
        cpu_init = (CPU / "cpu_init.c").read_text()
        self.assertIn(".acpi_fill_ssdt   = grantley_cpu_fill_ssdt", cpu_init)

    def test_real_cpu_callbacks_with_mock_hardware(self):
        headers = ["console/console.h", "cpu/cpu.h", "cpu/intel/common/common.h",
                   "cpu/intel/microcode.h", "cpu/x86/mp.h", "cpu/x86/msr.h",
                   "cpu/x86/mtrr.h", "device/device.h", "commonlib/helpers.h",
                   "drivers/intel/oc_mailbox/oc_mailbox.h"]
        with tempfile.TemporaryDirectory(prefix="grantley-cpu-", dir=os.environ.get("TMPDIR")) as tmp:
            tmp = Path(tmp)
            for header in headers:
                target = tmp / header
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text('#include "grantley-init-shim.h"\n')
            for smi in (0, 1):
                binary = tmp / f"callbacks-{smi}"
                subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                "-Wno-unused-parameter", f"-DCONFIG_HAVE_SMI_HANDLER={smi}",
                                "-I", str(tmp), "-I", str(Path(__file__).parent),
                                "-I", str(ROOT / "src"),
                                str(Path(__file__).with_name("grantley-init-harness.c")),
                                str(CPU / "platform.c"), "-o", str(binary)], check=True)
                subprocess.run([str(binary)], check=True)

    def test_real_smm_callbacks_with_mock_hardware(self):
        headers = ["console/console.h", "cpu/x86/msr.h", "cpu/x86/mtrr.h",
                   "cpu/x86/smm.h", "device/pci_def.h", "device/pci_ops.h",
                   "cpu/intel/smm_reloc.h", "types.h", "commonlib/helpers.h"]
        with tempfile.TemporaryDirectory(prefix="grantley-smm-", dir=os.environ.get("TMPDIR")) as tmp:
            tmp = Path(tmp)
            for header in headers:
                target = tmp / header
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text('#include "grantley-smm-shim.h"\n')
            state_header = tmp / "cpu/intel/em64t101_save_state.h"
            state_header.parent.mkdir(parents=True, exist_ok=True)
            state_header.write_text('#include "' + str(ROOT / "src/include/cpu/intel/em64t101_save_state.h") + '"\n')
            binary = tmp / "smm-callbacks"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-parameter", "-I", str(tmp),
                            "-I", str(Path(__file__).parent), "-I", str(ROOT / "src"),
                            str(Path(__file__).with_name("grantley-smm-harness.c")),
                            str(CPU / "platform.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_smrr_encoding_rejects_unsafe_regions(self):
        with tempfile.TemporaryDirectory(prefix="grantley-cpu-", dir=os.environ.get("TMPDIR")) as tmp:
            tmp = Path(tmp)
            (tmp / "commonlib").mkdir()
            (tmp / "commonlib/helpers.h").write_text("#define __weak __attribute__((weak))\n")
            library = tmp / "platform.so"
            subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                            "-I", str(tmp), str(CPU / "platform.c"), "-o", str(library)],
                           check=True)
            lib = ctypes.CDLL(str(library))
            self.assertTrue(hasattr(lib, "grantley_smrr_values"),
                            "SMRR must validate the actual hardware range before encoding")
            encode = lib.grantley_smrr_values
            encode.argtypes = [ctypes.c_uint64, ctypes.c_uint64,
                               ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32)]
            encode.restype = ctypes.c_bool
            base, mask = ctypes.c_uint32(), ctypes.c_uint32()
            self.assertTrue(encode(0x7f800000, 0x800000, ctypes.byref(base), ctypes.byref(mask)))
            self.assertEqual(base.value, 0x7f800006)
            self.assertEqual(mask.value, 0xff800800)
            for start, size in [(0, 0), (0x7f800000, 0), (0x7f800000, 0x600000),
                                (0x7f900000, 0x800000), (0x100000000, 0x800000),
                                (0xfffff000, 0x2000), (0, 0x100000000),
                                (0x7f800000, 0x800), (0, 0x800000)]:
                self.assertFalse(encode(start, size, ctypes.byref(base), ctypes.byref(mask)),
                                 (start, size))

    def test_server_smm_does_not_use_client_registers(self):
        smm = (CPU / "smm.c").read_text() if (CPU / "smm.c").exists() else ""
        self.assertIn("SMRR_PHYSBASE_MSR", smm)
        self.assertIn("SMRR_PHYSMASK_MSR", smm)
        self.assertIn("smm_region(", smm)
        self.assertIn("grantley_smrr_values(", smm)
        self.assertIn("pci_s_read_config32(IIO_DEV, 0xa8)", smm)
        self.assertIn("pci_s_read_config32(IIO_DEV, 0xac)", smm)
        self.assertIn("rdmsr(SMRR_PHYSMASK_MSR).raw", smm)
        self.assertIn("grantley_cpu_finalize", smm)
        self.assertIn("GRANTLEY_MSR_LT_CONTROL", smm)
        self.assertIn("SMM_DEFAULT_SIZE", smm)
        self.assertNotIn("haswell.h", smm)
        self.assertNotIn("PRMRR", smm)
        self.assertNotIn("0x4e0", smm.lower())
        makefile = (CPU / "Makefile.mk").read_text()
        self.assertNotIn("../haswell/smm_finalize.c", makefile)


if __name__ == "__main__":
    unittest.main()
