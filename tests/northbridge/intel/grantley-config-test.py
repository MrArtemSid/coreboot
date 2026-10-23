#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check the capabilities advertised by cold-boot-only Grantley boards."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[3]


class GrantleyConfigTest(unittest.TestCase):
    def test_cpu_selects_pre_ram_microcode_update(self):
        text = (ROOT / "src/cpu/intel/grantley/Kconfig").read_text()
        self.assertRegex(
            text,
            re.compile(r"^\s*select\s+MICROCODE_UPDATE_PRE_RAM\s*$", re.MULTILINE),
            "Grantley must apply the matching microcode before initializing CAR",
        )

    def test_cold_boot_boards_do_not_advertise_s3_resume(self):
        boards = (
            "angxun/x99-xd4",
            "envinda/x99d4m4",
            "huananzhi/x99-series",
        )
        for board in boards:
            with self.subTest(board=board):
                text = (ROOT / "src/mainboard" / board / "Kconfig").read_text()
                self.assertNotRegex(
                    text,
                    re.compile(r"^\s*select\s+HAVE_ACPI_RESUME\b", re.MULTILINE),
                    "Full memory training must not be advertised as S3 resume",
                )


if __name__ == "__main__":
    unittest.main()
