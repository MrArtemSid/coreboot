#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Execute early_pch_init and its server helper with register/IO fixtures."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]


class EarlyPchTest(unittest.TestCase):
    def test_rtc_power_loss_before_smbus(self):
        headers = ["console/console.h", "device/pci_ops.h", "device/device.h",
                   "device/pci_def.h", "device/pci_ids.h", "device/smbus_host.h",
                   "southbridge/intel/common/lpc_def.h",
                   "southbridge/intel/common/pmbase.h", "southbridge/intel/common/gpio.h",
                   "arch/io.h", "device/mmio.h", "pc80/mc146818rtc.h"]
        with tempfile.TemporaryDirectory(prefix="grantley-pch-", dir=os.environ.get("TMPDIR")) as tmp:
            tmp = Path(tmp)
            for header in headers:
                target = tmp / header
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text('#include "grantley-early-pch-shim.h"\n')
            for grantley, romstage in [(1, 1), (0, 1), (1, 0)]:
                binary = tmp / f"run-{grantley}-{romstage}"
                subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-parameter", "-I", str(tmp),
                            f"-DCONFIG_NORTHBRIDGE_INTEL_GRANTLEY={grantley}",
                            f"-DENV_ROMSTAGE={romstage}",
                            "-I", str(Path(__file__).parent),
                            "-I", str(ROOT / "src"),
                            str(Path(__file__).with_name("grantley-early-pch-harness.c")),
                            "-o", str(binary)], check=True)
                subprocess.run([str(binary)], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
