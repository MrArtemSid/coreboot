#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Assemble and emulate the production stackless startup (uv --with unicorn)."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_INSN
from unicorn.x86_const import *

ROOT = Path(__file__).resolve().parents[3]
HELPER = ROOT / 'src/cpu/intel/grantley/early_startup.inc'
CAR = ROOT / 'src/cpu/intel/grantley/cache_as_ram.S'
ECAM = 0x80000000
UBOX = 0x8ff87000
NODE = 0x8ff85040
PCU = 0x8fff2000
VCU = 0x8fff8000


class Machine:
    def __init__(self, mode='PRE', signature=0x306f2, oc=False, busy=-1,
                 emulation=0, bad_route=False, smx=False, txt=0, full=False,
                 reject_write=None, missing_vcu=False, max_leaf=0xb, topology=True,
                 bad_reset=False, capid=None):
        self.u = u = Uc(UC_ARCH_X86, UC_MODE_32)
        self.signature, self.oc, self.busy = signature, oc, busy
        self.smx = smx
        self.reject_write, self.missing_vcu = reject_write, missing_vcu
        self.max_leaf, self.topology = max_leaf, topology
        self.full = full
        self.events = []
        self.cfg = {0x80017df4: 0, 0x80018700: 0x8086,
                    0x80ff8700: 0x8086, 0x80ff7d00: 0x8086,
                    0x80002800: 0x8086, 0x80ff87dc: 0xf8}
        if bad_reset:
            self.cfg[0x80018700] = 0xffffffff
        if bad_route:
            self.cfg[0x80ff8700] = 0xffffffff
        self.selector = 0
        self.remapped = False
        self.writes, self.msrs, self.msr_reads, self.reads = [], {0x1a0: (1 << 22) | 0x123, 0x1b: 0x1fee00900, 0x61e: 3}, [], []
        self.commands, self.polls, self.halted = [], 0, False
        u.mem_map(0x100000, 0x10000)
        u.mem_map(ECAM, 0x10000000)
        u.mem_map(0xfed30000, 0x20000)
        if full:
            u.mem_map(0xfe100000, 0x10000)
            self.msrs[0xfe] = 8
            u.reg_write(UC_X86_REG_CR0, u.reg_read(UC_X86_REG_CR0) | 0x60000000)
        self.put(0xfed30010, txt)
        self.put(0x8fff3000 + 0x94, capid if capid is not None else ((1 << 28) if oc else 0))
        self.put(0x8ff850b0, emulation)
        self.put(PCU, 0x8086)
        self.put(NODE, 0xa5a50007)
        self.put(PCU + 0x6c, 0xa5a50703)
        self.put(UBOX + 0x68, 0x87ffffff)
        body = HELPER.read_text() if HELPER.exists() else ''
        entry = '\n'.join(f'movl ${value}, %eax\nmovd %eax, %mm{i}' for i, value in enumerate((0x12345678, 0x87654321, 0xdeadbeef)))
        save = '\n'.join(f'movd %mm{i}, %eax\nmovl %eax, {0x108000 + 4*i}' for i in range(3))
        definitions = f'#define GRANTLEY_{mode}_NEM 1\n'
        if mode == 'PRE':
            definitions += '#define GRANTLEY_CACHE_PREP 1\n'
        fixture = definitions + '#define post_code(x)\n.code32\n.global _start\n_start:\n' + entry + '\n' + body + '\n' + save + '\n.global finish\nfinish: nop\n'
        if full:
            fixture = '''
#define CONFIG(x) CONFIG_##x
#define CONFIG_POST_IO 1
#define CONFIG_POST_IO_PORT 0x80
#define CONFIG_NO_EARLY_BOOTBLOCK_POSTCODES 1
#define CONFIG_MICROCODE_UPDATE_PRE_RAM 1
#define CONFIG_ROM_SIZE 0x1000000
#define CONFIG_DCACHE_RAM_BASE 0xfe100000
#define CONFIG_DCACHE_RAM_SIZE 0x1000
#define ENV_X86_64 0
#define ENV_BOOTBLOCK 1
#define ENV_SEPARATE_DATA_AND_BSS 1
.section .text
.code32
.global _start
_start:
''' + entry + '\njmp bootblock_pre_c_entry\n' + f'#include "{CAR}"\n' + '''
.section .text
check_mtrr: jmp *%esp
update_bsp_microcode: jmp *%esp
bootblock_c_entry_bist:
''' + save + '''
.global finish
finish: nop
.set _car_mtrr_start, 0xfe100000
.set _car_mtrr_size, 0x1000
.set _car_mtrr_mask, 0xfffff000
.set _rom_mtrr_base, 0xff000000
.set _rom_mtrr_mask, 0xff000000
.set _xip_mtrr_mask, 0xff000000
.set _ecar_stack, 0xfe101000
.set _data, 0x108400
.set _edata, 0x108400
.set _data_load, 0x108400
'''
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            p = Path(tmp)
            (p / 'test.S').write_text(fixture)
            (p / 'link.ld').write_text('SECTIONS { . = 0x100000; .text : { *(.text) *(.init) } /DISCARD/ : { *(.note*) } }')
            subprocess.run(['cc', '-m32', '-nostdlib', '-static', '-no-pie', '-Wl,-T,' + str(p / 'link.ld'), '-Wl,--build-id=none', '-I', str(ROOT / 'src/include'), '-I', str(ROOT / 'src/commonlib/include'), '-I', str(ROOT / 'src'), str(p / 'test.S'), '-o', str(p / 'test')], check=True)
            subprocess.run(['objcopy', '-O', 'binary', '-j', '.text', str(p / 'test'), str(p / 'test.bin')], check=True)
            self.symbols = {line.split()[2]: int(line.split()[0], 16) for line in subprocess.check_output(['nm', str(p / 'test')], text=True).splitlines() if len(line.split()) == 3}
            code = (p / 'test.bin').read_bytes()
        self.finish = self.symbols['finish']
        u.mem_write(0x100000, code)
        u.reg_write(UC_X86_REG_ESP, 0xabcdef00)  # deliberately unmapped: no stack allowed
        u.hook_add(UC_HOOK_CODE, self.instruction)
        u.hook_add(UC_HOOK_INSN, self.out, None, 1, 0, UC_X86_INS_OUT)
        u.hook_add(UC_HOOK_INSN, self.inp, None, 1, 0, UC_X86_INS_IN)
        u.hook_add(UC_HOOK_MEM_READ, self.read)
        u.hook_add(UC_HOOK_MEM_WRITE, self.write)
        u.emu_start(0x100000, self.finish, count=200000)
        self.complete = u.reg_read(UC_X86_REG_EIP) == self.finish

    def put(self, addr, value):
        self.u.mem_write(addr, struct.pack('<I', value))

    def get(self, addr):
        return struct.unpack('<I', self.u.mem_read(addr, 4))[0]

    def out(self, u, port, size, value, _):
        if port == 0xcf8:
            self.selector = value
        elif port == 0xcfc:
            if self.selector != self.reject_write:
                self.cfg[self.selector] = value
                if self.selector == 0x800187d0 and value & 0x80000000:
                    self.remapped = True
                    self.cfg[0x80ff87d0] = value
            self.writes.append(('pci', self.selector, value))
        elif port == 0x80:
            self.events.append(('post', value, self.cfg.get(0x80017de0)))

    def inp(self, u, port, size, _):
        if self.remapped and ((self.selector >> 16) & 0xff) == 1:
            return 0xffffffff  # reset alias no longer responds after CPUBUSNO
        return self.cfg.get(self.selector, 0) if port == 0xcfc else 0

    def instruction(self, u, addr, size, _):
        op = bytes(u.mem_read(addr, size))
        if self.full and addr == self.symbols.get('update_bsp_microcode'):
            self.events.append(('microcode', self.cfg.get(0x80ff7dc0)))
        if self.full and addr == self.symbols.get('bootblock_c_entry_bist'):
            self.events.append(('c_entry', self.get(UBOX + 0x68)))
        if len(op) == 3 and op[:2] == b'\x0f\x22' and (op[2] & 0x38) == 0:
            self.events.append(('cr0', len(self.commands)))
        if op == b'\x0f\xa2':
            leaf = u.reg_read(UC_X86_REG_EAX)
            vals = {0: (self.max_leaf, 0, 0, 0), 1: (self.signature, 0x42000000, (1 << 6) if self.smx else 0, 0), 0xb: (1, 2 if self.topology else 0, 0x100, 0x1234), 0x80000008: (46, 0, 0, 0)}.get(leaf, (0, 0, 0, 0))
            for reg, value in zip((UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX), vals):
                u.reg_write(reg, value)
            u.reg_write(UC_X86_REG_EIP, addr + size)
        elif op in (b'\x0f\x32', b'\x0f\x30'):
            msr = u.reg_read(UC_X86_REG_ECX)
            if op[-1] == 0x32:
                self.msr_reads.append(msr)
                value = self.msrs.get(msr, 0)
                u.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
                u.reg_write(UC_X86_REG_EDX, value >> 32)
            else:
                value = u.reg_read(UC_X86_REG_EAX) | (u.reg_read(UC_X86_REG_EDX) << 32)
                self.msrs[msr] = value
                self.writes.append(('msr', msr, value))
            u.reg_write(UC_X86_REG_EIP, addr + size)
        elif op == b'\xf4':
            self.halted = True
            u.emu_stop()

    def read(self, u, access, addr, size, value, _):
        self.reads.append(addr)
        if addr == VCU + 0x40:
            stage = len(self.commands)
            self.polls += 1
            self.put(addr, 0xffffffff if self.missing_vcu else (0x80000000 if stage == self.busy else 0))

    def write(self, u, access, addr, size, value, _):
        self.writes.append(('mmio', addr, value))
        if addr == VCU + 0x40:
            self.commands.append(value)


class StartupTest(unittest.TestCase):
    def test_cold_boot_does_not_replay_warm_policy_or_inert_qpi_msrs(self):
        m = Machine(full=True)
        accessed = set(m.msr_reads) | {a for k, a, _ in m.writes if k == 'msr'}
        self.assertTrue({0x31, 0x55, 0x56, 0xc80}.isdisjoint(accessed))
        self.assertFalse(any(k == 'mmio' and a == 0x8fff1090 for k, a, _ in m.writes),
                         'SET_CORE_RING_RATIO is gated by completed cold-reset sticky state')

    def test_missing_capability_csr_never_authorizes_oc_msr(self):
        m = Machine(mode='POST', capid=0xffffffff)
        self.assertTrue(m.halted)
        self.assertNotIn(0x61e, m.msr_reads)

    def test_reset_alias_must_exist_before_remapping(self):
        m = Machine(bad_reset=True)
        self.assertTrue(m.halted)
        self.assertNotIn(0x800187d0, m.cfg)

    def test_complete_assembled_car_startup(self):
        for signature in (0x306f0, 0x306f2, 0x406f1):
            with self.subTest(signature=hex(signature)):
                m = Machine(full=True, signature=signature)
                self.assertTrue(m.complete)
                self.assertIn(('microcode', 0x80000001), m.events)
                self.assertEqual(m.msrs[0x2e0] & 3, 3)
                self.assertEqual([m.get(0x108000 + 4*i) for i in range(3)], [0x12345678, 0x87654321, 0xdeadbeef])
                sp = m.u.reg_read(UC_X86_REG_ESP)
                self.assertEqual((sp + 4) & 15, 0)
                self.assertEqual([m.get(sp + 4*i) for i in range(1, 4)], [0x87654321, 0xdeadbeef, 0x12345678])
                self.assertTrue(all(e[1] == 4 for e in m.events if e[0] == 'cr0'))
                self.assertTrue(all(e[2] == 0 for e in m.events if e[0] == 'post'))
                self.assertTrue(any(e[0] == 'c_entry' and e[1] & 1 for e in m.events))
        m = Machine(full=True, bad_route=True)
        self.assertTrue(m.halted)
        self.assertNotIn(0x2e0, m.msrs)
        self.assertFalse(any(e[0] == 'cr0' for e in m.events))
        self.assertIn(('post', 0xcc, 0), m.events)
        for kwargs in ({'busy': 2}, {'missing_vcu': True}):
            with self.subTest(**kwargs):
                m = Machine(full=True, **kwargs)
                self.assertTrue(m.complete, 'Stock reports VCU failure and continues')
                self.assertFalse(m.halted)
                self.assertEqual(m.msrs[0x2e0] & 3, 3)
                self.assertIn(('post', 0xcb, 0), m.events)

    def test_stepping_specific_iio_registers(self):
        for signature in (0x306f0, 0x306f1, 0x306f2, 0x406f0, 0x406f1):
            with self.subTest(signature=hex(signature)):
                m = Machine(signature=signature)
                self.assertTrue(m.complete)
                if signature == 0x306f0:
                    self.assertEqual(m.cfg.get(0x80002884), ECAM)
                    self.assertEqual(m.cfg.get(0x80002888), 0x8c000000)
                    self.assertTrue(all(a not in m.cfg for a in (0x80002890, 0x80002894, 0x80002898, 0x8000289c)))
                else:
                    self.assertEqual(m.cfg.get(0x80002894), 0)
                    self.assertEqual(m.cfg.get(0x8000289c), 0)
                    self.assertNotIn(0x80002884, m.cfg)

    def test_failed_route_cannot_continue(self):
        for kwargs in ({'bad_route': True}, {'reject_write': 0x800187d0},
                       {'reject_write': 0x80ff7dc0},
                       {'signature': 0x306c3}):
            with self.subTest(**kwargs):
                m = Machine(**kwargs)
                self.assertTrue(m.halted)
                self.assertFalse(m.complete)
                self.assertEqual(m.commands, [])

    def test_invalid_or_unavailable_topology_leaf_uses_initial_apic_id(self):
        for kwargs in ({'max_leaf': 2}, {'topology': False}):
            m = Machine(mode='POST', **kwargs)
            self.assertEqual((m.get(UBOX + 0x68) >> 8) & 0xffff, 0x42)

    def test_startup_is_wired_before_cache_and_c(self):
        source = CAR.read_text()
        pre = source.find('#define GRANTLEY_PRE_NEM')
        post = source.find('#define GRANTLEY_POST_NEM')
        self.assertLess(pre, source.index('end_microcode_update:'))
        self.assertGreater(source.index('#define GRANTLEY_CACHE_PREP'), source.index('end_microcode_update:'))
        self.assertLess(pre, source.index('/* Enable cache (CR0.CD'))
        self.assertGreater(post, source.index('.Lcar_verified:'))
        self.assertLess(post, source.index('\tmovl\t$_ecar_stack'))
        self.assertIn('select NO_EARLY_BOOTBLOCK_POSTCODES', (CAR.parent / 'Kconfig').read_text())
        self.assertGreater(source.index('post_code(POSTCODE_BOOTBLOCK_CAR)'), source.index('$0xff00007f'))

    def test_txt_unlock_and_a0_workaround_guards(self):
        for signature in (0x306f0, 0x306f1, 0x306f2, 0x406f0, 0x406f1):
            m = Machine(mode='POST', signature=signature)
            self.assertEqual(0x1fc in m.msrs, signature == 0x306f0)
            if signature == 0x306f0:
                self.assertEqual(m.msrs[0x1fc], (1 << 21) | 1)
        for smx, status, unlock in ((False, 1, False), (True, 0, False),
                                    (True, 1, True), (True, 2, False),
                                    (True, 0xffffffff, False)):
            with self.subTest(smx=smx, status=status):
                m = Machine(mode='POST', smx=smx, txt=status)
                self.assertEqual(0x2e6 in m.msrs, unlock)
                self.assertEqual(0xfed40000 in m.reads, unlock)
                if unlock:
                    self.assertEqual(m.msrs[0x2e6], 0)

    def test_package_checkin_and_guarded_oc_pll_ratio(self):
        for signature in (0x306f0, 0x306f2, 0x406f1):
            for oc in (False, True):
                with self.subTest(signature=hex(signature), oc=oc):
                    m = Machine(mode='POST', signature=signature, oc=oc)
                    expected = (0x87ffffff & 0x870000f3) | ((signature & 15) << 27) | (0x1234 << 8) | 1 | (12 if oc else 0)
                    self.assertEqual(m.get(UBOX + 0x68), expected)
                    self.assertEqual(0x61e in m.msr_reads, oc)
                    self.assertEqual([m.get(0x108000 + 4*i) for i in range(3)], [0x12345678, 0x87654321, 0xdeadbeef])
                    self.assertEqual(m.u.reg_read(UC_X86_REG_ESP), 0xabcdef00)
                    self.assertTrue(m.complete)

    def test_cpuid_limit_and_apic_base(self):
        m = Machine()
        self.assertEqual(m.msrs[0x1a0], 0x123)
        self.assertEqual(m.msrs[0x1b], 0xfee00900)
        self.assertNotIn(0x31, m.msr_reads, 'Cold boot has no DCU ECC policy request')

    def test_vcu_sequence_and_bounded_timeout(self):
        m = Machine()
        self.assertEqual(m.commands, [0x80000002, 0x80000006, 0x8001100f, 0x80000003])
        data = [v for kind, addr, v in m.writes if kind == 'mmio' and addr == VCU + 0x44]
        self.assertEqual(data, [0x1000a, 0, 0x1000a])
        self.assertEqual([m.get(0x108000 + 4*i) for i in range(3)], [0x12345678, 0x87654321, 0xdeadbeef])
        for stage in range(5):
            with self.subTest(timeout=stage):
                m = Machine(busy=stage)
                self.assertFalse(m.halted, 'Stock VCU timeout is diagnostic, not fatal')
                self.assertTrue(m.complete)
                self.assertEqual(len(m.commands), stage)
                self.assertEqual(m.polls, 0x1000 + stage)
                self.assertEqual(m.u.reg_read(UC_X86_REG_ESP), 0xabcdef00)
        m = Machine(emulation=1)
        self.assertEqual(m.commands, [])
        self.assertTrue(m.complete)

    def test_pre_nem_routing(self):
        m = Machine()
        self.assertEqual(m.cfg.get(0x800187d0), 0x8000ff00,
                         'UBOX must assign uncore bus ff before CSR use')
        self.assertEqual(m.cfg.get(0x80ff7de4), 0)
        self.assertEqual(m.cfg.get(0x80ff7dc0), 0x80000001)
        self.assertEqual(m.cfg.get(0x80ff87dc), 0xf9)
        self.assertEqual(m.get(NODE), 0xa5a50000)
        self.assertEqual(m.get(PCU + 0x6c), 0xa5a50007)
        self.assertEqual(m.get(PCU + 0x40) & 0x8000ffff, 0x8000ff00)
        self.assertEqual(m.get(0x80028108) & 0x1ffff, 0x1ff00)
        self.assertEqual(m.cfg.get(0x80002890), ECAM)
        self.assertEqual(m.cfg.get(0x80002894), 0)
        self.assertEqual(m.cfg.get(0x80002898), 0x8c000000)
        self.assertEqual(m.cfg.get(0x8000289c), 0)
        self.assertTrue(m.complete)
        self.assertEqual(m.u.reg_read(UC_X86_REG_ESP), 0xabcdef00)


if __name__ == '__main__':
    unittest.main()
