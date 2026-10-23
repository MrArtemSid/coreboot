#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise installed D4M4 PE32 timer code, not a reconstructed delay routine.

Run with: uv run --with unicorn python tests/northbridge/intel/grantley-blob-timer-test.py
The proprietary input is optional and is never modified or copied into the test.
"""
import hashlib
from pathlib import Path
import struct
import unittest

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_INSN, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_REG_EAX, UC_X86_REG_EDX,
                              UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_ESI)

ROOT = Path(__file__).resolve().parents[3]
BLOB = ROOT / "3rdparty/blobs/mainboard/envinda/x99d4m4/uncore.bin"
BASE = 0xffdce8e0
STATE, STACK, STOP, ECAM = 0x200000, 0x310000, 0x100000, 0x80000000


@unittest.skipUnless(BLOB.exists(), "optional D4M4 uncore.bin is absent")
class BlobTimerTest(unittest.TestCase):
    def setUp(self):
        self.blob = BLOB.read_bytes()
        self.assertEqual(hashlib.sha256(self.blob).hexdigest(),
                         "35b74537356d86ebcc6c778d646be12fcf86f8f5dcce5dae11a48f78bbe382d4",
                         "Re-audit timer callsites for a changed PEIM")
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        for address, size in [(0xffdc0000, 0x100000), (STATE, 0x80000),
                              (0x300000, 0x20000), (STOP, 0x1000), (ECAM, 0x100000)]:
            self.uc.mem_map(address, size)
        self.uc.mem_write(BASE, self.blob)
        self.uc.mem_write(STATE + 0x22b1c, struct.pack("<I", ECAM))
        self.ports = []
        self.reads = 0

    def call(self, address, *args, esi=0):
        self.uc.mem_write(STACK, struct.pack("<" + "I" * (len(args) + 1), STOP, *args))
        self.uc.reg_write(UC_X86_REG_ESP, STACK)
        self.uc.reg_write(UC_X86_REG_ESI, esi)
        self.uc.emu_start(address, STOP, timeout=1000000, count=100000)
        self.assertEqual(self.uc.reg_read(UC_X86_REG_EIP), STOP,
                         "timer routine exceeded bounded instruction/time budget")
        return self.uc.reg_read(UC_X86_REG_EAX)

    def io_clock(self, uc, port, size, _):
        self.assertEqual(size, 4)
        self.ports.append(port)
        self.reads += 1
        return (0xfffff0 + self.reads * 8) & 0xffffff

    def test_enabled_pmbase_is_preserved_and_timer_uses_it(self):
        for callsite, target in [(0xffe02023, 0xffe0fe4a),
                                 (0xffe024c9, 0xffe0ff64),
                                 (0xffdfaa69, 0xffdffacb)]:
            offset = callsite - BASE
            self.assertEqual(self.blob[offset], 0xe8)
            self.assertEqual((callsite + 5 + struct.unpack_from("<i", self.blob,
                                                              offset + 1)[0]) & 0xffffffff,
                             target)
        # Execute actual PCI MMIO read helpers as well as the initializer.
        for pmbase in (0x500, 0x600):
            self.uc.mem_write(ECAM + 0xf8040, struct.pack("<I", pmbase | 1))
            self.uc.mem_write(ECAM + 0xf8044, b"\x80")
            self.call(0xffe0fe4a, STATE)
            self.assertEqual(bytes(self.uc.mem_read(STATE + 0x22b08, 2)),
                             struct.pack("<H", pmbase))
            self.assertEqual(bytes(self.uc.mem_read(ECAM + 0xf8040, 4)),
                             struct.pack("<I", pmbase | 1))
            hook = self.uc.hook_add(UC_HOOK_INSN, self.io_clock, None, 1, 0, UC_X86_INS_IN)
            self.call(0xffe0ff64, STATE)
            self.uc.hook_del(hook)
            self.assertEqual(self.ports[-1], pmbase + 8)

    def test_disabled_acpi_decode_is_the_only_fixed_400_fallback(self):
        self.uc.mem_write(ECAM + 0xf8040, struct.pack("<I", 0x501))
        self.uc.mem_write(ECAM + 0xf8044, b"\x00")
        self.call(0xffe0fe4a, STATE)
        self.assertEqual(bytes(self.uc.mem_read(STATE + 0x22b08, 2)), b"\x00\x04")
        self.assertEqual(bytes(self.uc.mem_read(ECAM + 0xf8040, 4)), b"\x01\x04\x00\x00")
        self.assertEqual(bytes(self.uc.mem_read(ECAM + 0xf8044, 1)), b"\x80")

    def test_mrc_delay_uses_context_base_and_survives_24bit_wrap(self):
        # Producer at ffdffcbc reads PCI 00:1f.0:40 and stores base at context+ee0.
        offset = 0xffdffcbc - BASE
        self.assertEqual(self.blob[offset:offset + 6], bytes.fromhex("536840800f00"))
        self.assertEqual(self.blob[0xffdffcd3 - BASE:0xffdffcdf - BASE],
                         bytes.fromhex("0fb7c083e0fe8987e00e0000"))
        self.uc.mem_write(STATE + 0xee0, struct.pack("<I", 0x500))
        hook = self.uc.hook_add(UC_HOOK_INSN, self.io_clock, None, 1, 0, UC_X86_INS_IN)
        self.call(0xffdffacb, 10, esi=STATE)
        self.uc.hook_del(hook)
        self.assertGreater(self.reads, 2)
        self.assertLess(self.reads, 32)
        self.assertEqual(set(self.ports), {0x508})

    def test_tsc_delay_does_not_access_pm_io(self):
        self.uc.mem_write(STATE + 0x22b38, struct.pack("<Q", 1000000))
        ticks = 0

        def tsc(uc, address, size, _):
            nonlocal ticks
            if address == 0xffe10958:  # rdtsc; substitute a deterministic advancing clock.
                ticks += 10
                uc.reg_write(UC_X86_REG_EAX, ticks)
                uc.reg_write(UC_X86_REG_EDX, 0)
                uc.reg_write(UC_X86_REG_EIP, address + 2)

        self.uc.hook_add(UC_HOOK_CODE, tsc)
        self.uc.hook_add(UC_HOOK_INSN, self.io_clock, None, 1, 0, UC_X86_INS_IN)
        self.call(0xffe0fec3, STATE, 20)
        self.assertGreater(ticks, 20)
        self.assertEqual(self.ports, [])


if __name__ == "__main__":
    unittest.main()
