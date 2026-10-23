#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host regression tests: python3 tests/northbridge/intel/grantley-pci-cfg-decode-test.py.

Compile the production helpers, not a second implementation of the decoder.
The address layout comes from the in-tree UDK2017 PCI_CFG2 ABI definition.
Only a host C compiler and the Python standard library are required.
"""

import ctypes
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "src/northbridge/intel/grantley/uncore_runner/pei_services.c"
ABI = ROOT / "src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ppi/PciCfg2.h"


def extract_function(source, name):
    """Keep the exact production definition, including its signature."""
    match = re.search(r"^static inline [^\n]*\b" + name + r"\(", source, re.M)
    if match is None:
        raise RuntimeError(f"Production function {name} not found")
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


class Decoded(ctypes.Structure):
    _fields_ = [("pdev", ctypes.c_uint32), ("reg", ctypes.c_uint32),
                ("bus", ctypes.c_uint8), ("dev", ctypes.c_uint8),
                ("func", ctypes.c_uint8), ("mmio", ctypes.c_size_t)]


class PciCfgDecodeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="grantley-pci-cfg-test-")
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        source = SOURCE.read_text()
        abi = re.search(r"typedef struct \{[^{}]*\}\s*"
                        r"EFI_PEI_PCI_CFG_PPI_PCI_ADDRESS;", ABI.read_text())
        if abi is None:
            raise RuntimeError("UDK2017 PCI_CFG2 address structure not found")
        harness = '''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uint32_t u32;
'''
        harness += f'#include "{ROOT / "src/include/device/pci_type.h"}"\n'
        harness += abi.group() + "\n"
        harness += '''
_Static_assert(sizeof(EFI_PEI_PCI_CFG_PPI_PCI_ADDRESS) == 8, "address size");
_Static_assert(offsetof(EFI_PEI_PCI_CFG_PPI_PCI_ADDRESS, Register) == 0, "Register");
_Static_assert(offsetof(EFI_PEI_PCI_CFG_PPI_PCI_ADDRESS, Function) == 1, "Function");
_Static_assert(offsetof(EFI_PEI_PCI_CFG_PPI_PCI_ADDRESS, Device) == 2, "Device");
_Static_assert(offsetof(EFI_PEI_PCI_CFG_PPI_PCI_ADDRESS, Bus) == 3, "Bus");
_Static_assert(offsetof(EFI_PEI_PCI_CFG_PPI_PCI_ADDRESS, ExtendedRegister) == 4,
               "ExtendedRegister");
'''
        harness += extract_function(source, "pci_cfg_decode") + "\n"
        harness += extract_function(source, "pci_cfg_mmio") + "\n"
        harness += '''
struct decoded {
    pci_devfn_t pdev;
    UINT32 reg;
    UINT8 bus, dev, func;
    uintptr_t mmio;
};
void decode(UINT8 bus, UINT8 dev, UINT8 func, UINT8 reg, UINT32 extended,
            struct decoded *out)
{
    EFI_PEI_PCI_CFG_PPI_PCI_ADDRESS fields = {
        .Register = reg, .Function = func, .Device = dev, .Bus = bus,
        .ExtendedRegister = extended,
    };
    UINT64 address;
    memcpy(&address, &fields, sizeof(address));
    pci_cfg_decode(address, &out->pdev, &out->reg, &out->bus, &out->dev, &out->func);
    out->mmio = pci_cfg_mmio(0x80000000, out->bus, out->dev, out->func, out->reg);
}
'''
        (directory / "decode.c").write_text(harness)
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                       ["-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                        str(directory / "decode.c"), "-o", str(directory / "decode.so")],
                       check=True)
        cls.library = ctypes.CDLL(str(directory / "decode.so"))
        cls.library.decode.argtypes = [ctypes.c_uint8] * 4 + [ctypes.c_uint32,
                                                            ctypes.POINTER(Decoded)]
        cls.library.decode.restype = None

    def decode(self, bus, dev, func, reg, extended=0):
        result = Decoded()
        self.library.decode(bus, dev, func, reg, extended, ctypes.byref(result))
        return result

    def assert_decoded(self, result, bus, dev, func, expected_reg):
        self.assertEqual((result.bus, result.dev, result.func), (bus, dev, func))
        self.assertEqual(result.reg, expected_reg)
        expected_pdev = bus * 0x100000 + dev * 0x8000 + func * 0x1000
        self.assertEqual(result.pdev, expected_pdev)
        self.assertEqual(result.mmio, 0x80000000 + expected_pdev + expected_reg)

    def test_nonzero_function_does_not_leak_into_ordinary_register(self):
        result = self.decode(0, 0, 1, 0x40)
        self.assertEqual(result.reg, 0x40)

    def test_ordinary_registers_all_functions_and_bdf_boundaries(self):
        for bus in (0, 0xff):
            for dev in (0, 0x1f):
                for func in range(8):
                    for reg in (0, 0x40, 0xff):
                        with self.subTest(bus=bus, dev=dev, func=func, reg=reg):
                            self.assert_decoded(self.decode(bus, dev, func, reg),
                                                bus, dev, func, reg)

    def test_extended_register_overrides_low_byte_for_all_functions(self):
        for bus in (0, 0xff):
            for dev in (0, 0x1f):
                for func in range(8):
                    for low in (0, 0x40, 0xff):
                        for extended in (0x40, 0x100, 0xfff):
                            with self.subTest(bus=bus, dev=dev, func=func,
                                              low=low, extended=extended):
                                result = self.decode(bus, dev, func, low, extended)
                                self.assert_decoded(result, bus, dev, func, extended)

    def test_function_ecam_addresses_are_distinct_and_one_page_apart(self):
        for extended in (0, 0x340):
            with self.subTest(extended=extended):
                addresses = [self.decode(0xff, 0x1f, func, 0x40, extended).mmio
                             for func in range(8)]
                self.assertEqual(len(set(addresses)), 8)
                self.assertEqual([b - a for a, b in zip(addresses, addresses[1:])],
                                 [0x1000] * 7)


if __name__ == "__main__":
    unittest.main(verbosity=2)
