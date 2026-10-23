#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Selected cold-boot PEIM input evidence; requires local proprietary artifacts.

These checks intentionally describe this exact image, not every Grantley PEIM.
They do not execute silicon initialization or prove maximum nested stack depth.
"""
from pathlib import Path
import re
import struct
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[3]
BLOB = ROOT / '3rdparty/blobs/mainboard/envinda/x99d4m4/uncore.bin'
ROM = ROOT / 'bios/D4M4-orig.bin'
PI_PCD = uuid.UUID('01f34d25-4de2-23ad-3ff3-36353ff323f1')


@unittest.skipUnless(BLOB.is_file() and ROM.is_file(), 'local selected blob and stock ROM required')
class SelectedEnvironmentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blob = BLOB.read_bytes()
        cls.rom = ROM.read_bytes()
        cls.disassembly = subprocess.check_output(
            ['objdump', '-d', '-Mintel', str(BLOB)], text=True, stderr=subprocess.DEVNULL)
        pe = struct.unpack_from('<I', cls.blob, 0x3c)[0]
        cls.image_base = struct.unpack_from('<I', cls.blob, pe + 0x34)[0]

    def test_selected_image_is_the_stock_pe32_payload(self):
        offset = self.image_base - (0x100000000 - len(self.rom))
        self.assertEqual(self.blob, self.rom[offset:offset + len(self.blob)])

    def test_pi_pcd_is_a_dispatch_gate_not_a_runtime_interface(self):
        # PEI dependency section immediately preceding this fixed-address image.
        # Parse the section length/type, rather than assuming GUID alignment.
        header = self.rom[0xdce858:0xdce85c]
        length = int.from_bytes(header[:3], 'little')
        self.assertEqual(header[3], 0x1b)
        depex = self.rom[0xdce85c:0xdce858 + length]
        guids, operators = [], []
        index = 0
        while index < len(depex):
            opcode = depex[index]
            index += 1
            if opcode == 2:
                guids.append(uuid.UUID(bytes_le=depex[index:index + 16]))
                index += 16
            else:
                operators.append(opcode)
        self.assertEqual(len(guids), 7)
        self.assertEqual(guids[-1], PI_PCD)
        self.assertEqual(operators, [3] * 6 + [8])
        self.assertNotIn(PI_PCD.bytes_le, self.blob)
        # Direct invocation does not evaluate the stock dispatch expression.
        # Do not advertise an empty PCD table merely to satisfy that expression.

    def test_no_indirect_notify_service_calls_in_selected_text(self):
        self.assertIsNone(re.search(r'\b(?:call|jmp)\s+DWORD PTR \[[^\]]*\+0x24\]',
                                    self.disassembly))
        # Also reject loading the slot into a register for an indirect call.
        self.assertIsNone(re.search(r'\bmov\s+\w+,DWORD PTR \[[^\]]*\+0x24\].*'
                                    r'\n[^\n]*\b(?:call|jmp)\s+\w+\s*$',
                                    self.disassembly, re.MULTILINE))

    def test_platform_reservations_are_real_payload_inputs(self):
        # Selected code copies TSEG and IED from the writable platform-HOB data.
        offset = 0xffe84938 - self.image_base
        self.assertEqual(self.blob[offset:offset + 6], bytes.fromhex('8b8685000000'))
        offset = 0xffe84944 - self.image_base
        self.assertEqual(self.blob[offset:offset + 6], bytes.fromhex('8b8689000000'))
        # Board ID is consumed unconditionally; a zero-filled HOB is not evidence
        # of the board's actual wiring or a proven safe unknown-board policy.
        offset = 0xffe81f29 - self.image_base
        self.assertEqual(self.blob[offset:offset + 3], bytes.fromhex('8a4046'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
