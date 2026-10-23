#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""IA32 instruction-level runner tests (no privileged host instructions).

Run: uv run --with unicorn python tests/northbridge/intel/grantley-idtr-test.py
Requires a C compiler with -m32 support, nm, and Unicorn. Compile the exact
production runner and its adjacent private helpers freestanding; emulate the
real SIDT/LIDT and CAR stack switch. Only PEIM execution is replaced by a hook.
"""
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_IDTR

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'src/northbridge/intel/grantley/uncore_runner/pei_services.c'

PREAMBLE = r'''
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned int uintptr_t;
typedef int intptr_t;
typedef int int32_t;
typedef unsigned int size_t;
typedef unsigned int EFI_STATUS;
#define EFI_INVALID_PARAMETER 0x80000002U
#define EFI_UNSUPPORTED 0x80000003U
#define ENV_TEST 0
#define CONFIG_DCACHE_RAM_BASE 0x400000
#define CONFIG_DCACHE_RAM_SIZE 0x100000
#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define _Static_assert _Static_assert
struct _EFI_PEI_SERVICES { uint32_t signature; };
struct grantley_uncore_state {
    const struct _EFI_PEI_SERVICES *services_ptr;
    uintptr_t saved_esp;
};
void *memcpy(void *d, const void *s, size_t n) {
    for (size_t i = 0; i < n; i++) ((uint8_t *)d)[i] = ((const uint8_t *)s)[i];
    return d;
}
void *memset(void *d, int c, size_t n) {
    for (size_t i = 0; i < n; i++) ((uint8_t *)d)[i] = c;
    return d;
}
#define offsetof(t, m) __builtin_offsetof(t, m)
'''


class IdtrTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='grantley-idtr-')
        cls.addClassCleanup(cls.tmp.cleanup)
        directory = Path(cls.tmp.name)
        source = SOURCE.read_text()
        # The private IDTR definitions belong immediately before this typedef.
        start = source.index('typedef EFI_STATUS (*grantley_peim_entry_t)')
        marker = source.find('/* IA-32 implicit PEI services', 0, start)
        if marker >= 0:
            start = marker
        end = source.index('static EFI_STATUS EFIAPI grantley_ffs_stub', start)
        c = directory / 'runner.c'
        c.write_text(PREAMBLE + source[start:end])
        elf = directory / 'runner.elf'
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
            '-m32', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector',
            '-nostdlib', '-no-pie', '-Wl,-Ttext-segment=0x100000',
            '-Wl,-e,grantley_run_uncore_peim', str(c), '-o', str(elf)], check=True)
        cls.elf = elf.read_bytes()
        symbols = subprocess.check_output(['nm', '-n', str(elf)], text=True)
        cls.entry = next(int(line.split()[0], 16) for line in symbols.splitlines()
                         if line.endswith(' grantley_run_uncore_peim'))

    def run_peim(self, result=0, old_base=0, old_limit=0):
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(0x80000, 0x580000)
        e = self.elf
        phoff = struct.unpack_from('<I', e, 28)[0]
        phsize, phnum = struct.unpack_from('<HH', e, 42)
        for i in range(phnum):
            kind, off, addr, _, size, _, _, _ = struct.unpack_from('<8I', e, phoff+i*phsize)
            if kind == 1:
                uc.mem_write(addr, e[off:off+size])
        pe, state, services, stack, stop = 0x300000, 0x310000, 0x311000, 0x3f0000, 0x320000
        header = bytearray(0x1000)
        header[:2] = b'MZ'
        struct.pack_into('<I', header, 0x3c, 0x80)
        header[0x80:0x84] = b'PE\0\0'
        struct.pack_into('<I', header, 0xa8, 0x200)
        struct.pack_into('<I', header, 0xb4, pe)
        header[0x200] = 0xc3  # Synthetic PEIM returns EAX set by the hook.
        uc.mem_write(pe, bytes(header))
        uc.mem_write(state, struct.pack('<II', services, 0))
        uc.mem_write(services, struct.pack('<I', 0x56524553))
        uc.mem_write(stack, struct.pack('<III', stop, pe, state))
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_IDTR, (0, old_base, old_limit, 0))
        old_gates = bytes((i * 17 + 0x83) & 0xff for i in range(old_limit + 1))
        if old_base:
            uc.mem_write(old_base, old_gates)
        visits = []

        def hook(cpu, address, size, _):
            if address == pe + 0x200:
                _, base, limit, _ = cpu.reg_read(UC_X86_REG_IDTR)
                self.assertNotEqual(base, 0, 'PEIM must receive a private IDT, not null IDTR')
                implicit = struct.unpack('<I', cpu.mem_read(base-4, 4))[0]
                esp = cpu.reg_read(UC_X86_REG_ESP)
                explicit = struct.unpack('<I', cpu.mem_read(esp+8, 4))[0]
                self.assertEqual(implicit, state, '[IDTR.base-4] must be &services_ptr')
                self.assertEqual(implicit, explicit, 'implicit and explicit double pointers differ')
                table = struct.unpack('<I', cpu.mem_read(implicit, 4))[0]
                self.assertEqual(table, services)
                self.assertEqual(bytes(cpu.mem_read(table, 4)), b'SERV')
                self.assertGreaterEqual(esp, 0x400000, 'PEIM must use dedicated CAR stack')
                self.assertFalse(0x400000 <= base < 0x500000,
                                 'IDT must not live on the PEIM stack')
                if old_base:
                    self.assertNotEqual(base, old_base, 'must not overwrite coreboot IDT')
                    self.assertEqual(limit, min(old_limit, 2047))
                    self.assertEqual(bytes(cpu.mem_read(base, limit+1)),
                                     old_gates[:limit+1], 'preserve existing exception gates')
                    self.assertEqual(bytes(cpu.mem_read(old_base, old_limit+1)), old_gates)
                visits.append((base, limit))
                cpu.reg_write(UC_X86_REG_EAX, result)
            elif address == stop:
                cpu.emu_stop()

        uc.hook_add(UC_HOOK_CODE, hook)
        uc.emu_start(self.entry, stop+1, count=100000)
        self.assertEqual(len(visits), 1)
        self.assertEqual(uc.reg_read(UC_X86_REG_EAX), result)
        self.assertEqual(uc.reg_read(UC_X86_REG_ESP), stack+4)
        self.assertEqual(uc.reg_read(UC_X86_REG_IDTR)[1:3], (old_base, old_limit),
                         'original IDTR must be restored before returning')
        return uc, visits[0]

    def test_implicit_double_pointer_and_restore(self):
        self.run_peim()

    def test_existing_exception_gates_survive_private_idt(self):
        self.run_peim(old_base=0x330000, old_limit=32*8-1)

    def test_error_return_restores_idtr_and_stack(self):
        self.run_peim(result=0x80000007, old_base=0x330000, old_limit=32*8-1)

    def test_oversized_original_idt_is_copied_within_private_storage(self):
        self.run_peim(old_base=0x330000, old_limit=4095)


if __name__ == '__main__':
    unittest.main(verbosity=2)
