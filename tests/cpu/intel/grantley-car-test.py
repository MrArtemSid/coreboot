#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Execute the real IA-32 CAR-to-C handoff without privileged CAR setup."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
CAR = ROOT / "src/cpu/intel/grantley/cache_as_ram.S"


class GrantleyCarTest(unittest.TestCase):
    def run_assembly(self, fixture):
        with tempfile.TemporaryDirectory(prefix="grantley-car-prereq-",
                                         dir=os.environ.get("TMPDIR")) as tmp:
            assembly, binary = Path(tmp) / "test.S", Path(tmp) / "test"
            assembly.write_text(fixture)
            subprocess.run(["cc", "-m32", "-nostdlib", "-static", "-no-pie",
                            "-Wl,--build-id=none", "-I", str(ROOT / "src"),
                            str(assembly), "-o", str(binary)], check=True)
            return subprocess.run([str(binary)], timeout=5).returncode

    def test_car_mmio_decode_follows_target_lists(self):
        source = CAR.read_text()
        routing = source[source.index("\tmovl\t$0x80018550"):
                         source.index("\t/* Entry16/32 POSTs")]
        fixture = r"""
.section .text
.code32
.macro outl value, port
    cmpw $0xcf8, \port
    jne .Ldata\@
    movl \value, selector
    jmp .Ldone\@
.Ldata\@:
    pushl %edi
    pushl %esi
    movl count, %edi
    movl selector, %esi
    movl %esi, writes(,%edi,8)
    movl \value, writes+4(,%edi,8)
    incl count
    popl %esi
    popl %edi
.Ldone\@:
.endm
.macro inl port, value
    movl $0x28, \value
.endm
.global _start
_start:
""" + routing + r"""
    movl $1, %ebx
    cmpl $5, count
    jne fail
    cmpl $0x80017de0, writes+8
    jne fail
    cmpl $(5 * 0x249249), writes+12
    jne fail
    cmpl $0x80017de8, writes+16
    jne fail
    cmpl $(5 * 0x249249), writes+20
    jne fail
    cmpl $0x80017d5c, writes+24
    jne fail
    cmpl $0, writes+28
    jne fail
    cmpl $0x80017d58, writes+32
    jne fail
    cmpl $0xff00007f, writes+36
    jne fail
    xorl %ebx, %ebx
fail:
    movl $1, %eax
    int $0x80
.section .data
selector: .long 0
count: .long 0
writes: .zero 128
.section .note.GNU-stack,"",@progbits
"""
        self.assertEqual(self.run_assembly(fixture), 0,
                         "Target lists must precede CAR MMIO high=0, low=0xff00007f")

    def test_each_nem_fill_line_is_fenced_before_run(self):
        source = CAR.read_text()
        fill = source[source.index("\t/* Address to touch cache line."):
                      source.index("\tpost_code(POSTCODE_SOC_ENABLE_CACHE)")]
        fixture = r"""
#define NoEvictMod_MSR 0x2e0
.set _car_mtrr_size, 0x100000
.section .text
.code32
.macro sfence
    .byte 0x0f, 0xae, 0xf8 /* Execute the actual SFENCE, too. */
    cmpl cursor, %edi
    jne fail
    cmpl %eax, (%edi)
    jne fail
    addl $64, cursor
    incl fences
.endm
.macro rdmsr
    movl $1, %eax
    xorl %edx, %edx
.endm
.macro wrmsr
    cmpl $0x2e0, %ecx
    jne fail
    cmpl $3, %eax
    jne fail
    cmpl $(_car_mtrr_size / 64), fences
    jne fail
.endm
.global _start
_start:
    movl $1, %eax
""" + fill + r"""
    xorl %ebx, %ebx
    jmp exit
fail:
    movl $1, %ebx
exit:
    movl $1, %eax
    int $0x80
.section .data
cursor: .long _car_mtrr_start
fences: .long 0
.section .bss
.balign 64
_car_mtrr_start: .skip _car_mtrr_size
.section .note.GNU-stack,"",@progbits
"""
        self.assertEqual(self.run_assembly(fixture), 0,
                         "Every 64-byte fill store needs SFENCE before NEM Run")

    def test_car_readback_blocks_bad_memory_before_stack(self):
        source = CAR.read_text()
        check = source[source.index("\tpost_code(POSTCODE_SOC_ENABLE_CACHE)"):
                       source.index("\t/* Set up the C stack")]
        for fault in (-1, 0, 0x80000, 0xffffc):
            with self.subTest(fault=fault):
                fixture = r"""
#define post_code(value) movl $value, last_post
#define POSTCODE_SOC_ENABLE_CACHE 0x2c
#define POSTCODE_RAM_FAILURE 0xe3
.set _car_mtrr_size, 0x100000
.section .text
.code32
.macro checked_cmp lhs, rhs
.ifc \rhs,(%edi)
    cmp $0x5a5a5a5a, %eax
    jne fail
    cmp %eax, (%edi)
    jne fail
    incl reads
    cmp $(_car_mtrr_start + FAULT), %edi
    jne .Lgood\@
    xorl $1, (%edi)
.Lgood\@:
.endif
    cmpl \lhs, \rhs
.endm
.macro hlt
    cmpl $0xe3, last_post
    jne fail
    cmpl $EXPECTED_READS, reads
    jne fail
    cmpl initial_sp, %esp
    jne fail
    jmp success
.endm
.global _start
_start:
    movl %esp, initial_sp
    std /* Production must establish its own forward direction. */
""" + check.replace("\tcmpl\t", "\tchecked_cmp\t") + r"""
    cmpl $-1, $0 /* replaced below: fault must not reach C/stack setup */
    cmpl $(_car_mtrr_size / 4), reads
    jne fail
    cmpl initial_sp, %esp
    jne fail
    movl $_car_mtrr_start, %edi
    movl $(_car_mtrr_size / 4), %ecx
    xorl %eax, %eax
    repe scasl
    jne fail
    cmpl $0x12345678, guard_before
    jne fail
    cmpl $0x87654321, guard_after
    jne fail
success:
    xorl %ebx, %ebx
    jmp exit
fail:
    movl $1, %ebx
exit:
    movl $1, %eax
    int $0x80
.section .data
last_post: .long 0
reads: .long 0
initial_sp: .long 0
guard_before: .long 0x12345678
_car_mtrr_start: .zero _car_mtrr_size
guard_after: .long 0x87654321
.section .note.GNU-stack,"",@progbits
"""
                fixture = fixture.replace(
                    '    cmpl $-1, $0 /* replaced below: fault must not reach C/stack setup */',
                    '    jmp fail' if fault >= 0 else '')
                fixture = (f"#define FAULT {fault}\n"
                           f"#define EXPECTED_READS {fault // 4 + 1}\n" + fixture)
                self.assertEqual(self.run_assembly(fixture), 0,
                                 "CAR must verify every dword, halt on mismatch, then clear")

    def test_microcode_returns_before_cache_enable_preserving_entry_state(self):
        source = CAR.read_text()
        start = source.index("#if CONFIG(MICROCODE_UPDATE_PRE_RAM)")
        end = source.index("#endif", start) + len("#endif")
        self.assertLess(start, source.index("/* Enable cache (CR0.CD"),
                        "Microcode must run while CR0.CD is still set")
        fixture = r"""
#define CONFIG(x) 1
.section .text
.code32
.global _start
_start:
    movl $0xdeadbeef, %eax
    movd %eax, %mm0
    movl $0x12345678, %eax
    movd %eax, %mm1
    movl $0x9abcdef0, %eax
    movd %eax, %mm2
""" + source[start:end] + r"""
    movl $1, %ebx
    cmpl $1, visits
    jne exit
    movd %mm0, %eax
    cmpl $0xdeadbeef, %eax
    jne exit
    movd %mm1, %eax
    cmpl $0x12345678, %eax
    jne exit
    movd %mm2, %eax
    cmpl $0x9abcdef0, %eax
    jne exit
    xorl %ebx, %ebx
exit:
    movl $1, %eax
    int $0x80
update_bsp_microcode:
    incl visits
    /* Match the documented loader clobbers and its stackless return. */
    movl $0x88, %eax
    movl %eax, %ebx
    movl %eax, %ecx
    movl %eax, %edx
    movl %eax, %esi
    movl %eax, %edi
    movl %eax, %ebp
    jmp *%esp
.section .data
visits: .long 0
.section .note.GNU-stack,"",@progbits
"""
        self.assertEqual(self.run_assembly(fixture), 0,
                         "Stackless microcode loader must return without losing BIST/TSC")

    def test_early_posts_suppressed_but_car_failure_posts_restored(self):
        # Exercise the same header guard used by entry16/32, then the CAR
        # translation unit that restores POST only after legacy routing.
        fixture = ('#include <cpu/x86/post_code.h>\n'
                   'early_post_marker:\npost_code(0xe1)\n'
                   f'#include "{CAR}"\n')
        result = subprocess.check_output([
            'cc', '-m32', '-E', '-P', '-x', 'assembler-with-cpp',
            '-DCONFIG(x)=CONFIG_##x', '-DCONFIG_POST_IO=1',
            '-DCONFIG_POST_IO_PORT=0x80', '-DCONFIG_NO_EARLY_BOOTBLOCK_POSTCODES=1',
            '-DCONFIG_ROM_SIZE=0x1000000', '-DCONFIG_DCACHE_RAM_BASE=0xfe100000',
            '-DCONFIG_DCACHE_RAM_SIZE=0x100000',
            '-DCONFIG_MICROCODE_UPDATE_PRE_RAM=1', '-DENV_BOOTBLOCK=1',
            '-DENV_X86_64=0', '-DENV_SEPARATE_DATA_AND_BSS=1',
            '-I', str(ROOT / 'src/include'),
            '-I', str(ROOT / 'src/commonlib/include'), '-I', str(ROOT / 'src'),
            '-'], input=fixture, text=True)
        self.assertNotIn('outb', result[:result.index('cache_as_ram:')])
        failure = result[result.index('.Lcar_failure:'):result.index('.Lcar_verified:')]
        self.assertIn('outb %al, $0x80', failure)
        self.assertNotIn('outb', result[result.index('cache_as_ram:'):result.index('$0xff00007f')])

    def test_optional_preram_microcode_hook(self):
        with tempfile.TemporaryDirectory(prefix="grantley-car-hook-",
                                         dir=os.environ.get("TMPDIR")) as tmp:
            for enabled in (0, 1):
                with self.subTest(enabled=enabled):
                    obj = Path(tmp) / f"car-{enabled}.o"
                    subprocess.run([
                        "cc", "-m32", "-c", "-x", "assembler-with-cpp",
                        "-DCONFIG(x)=CONFIG_##x", "-DCONFIG_POST_IO=0",
                        "-DCONFIG_ROM_SIZE=0x1000000",
                        "-DCONFIG_DCACHE_RAM_BASE=0xfe100000",
                        "-DCONFIG_DCACHE_RAM_SIZE=0x100000",
                        f"-DCONFIG_MICROCODE_UPDATE_PRE_RAM={enabled}",
                        "-DENV_X86_64=0", "-DENV_SEPARATE_DATA_AND_BSS=1",
                        "-I", str(ROOT / "src/include"),
                        "-I", str(ROOT / "src/commonlib/include"),
                        "-I", str(ROOT / "src"), str(CAR), "-o", str(obj),
                    ], check=True)
                    undefined = subprocess.check_output(["nm", "-u", str(obj)],
                                                        text=True)
                    self.assertEqual("update_bsp_microcode" in undefined,
                                     bool(enabled),
                                     "Configured early microcode hook must be retained")

    def test_nem_setup_clears_run_state(self):
        source = CAR.read_text()
        begin = source.index("/* Enable No-Eviction Mode - Setup State")
        end = source.index("/* Address to touch cache line.", begin)
        setup = source[begin:end]
        fixture = r"""
#define NoEvictMod_MSR 0x2e0
.section .text
.code32
.macro rdmsr
    movl mock_msr, %eax
    movl mock_msr+4, %edx
.endm
.macro wrmsr
    movl %eax, mock_msr
    movl %edx, mock_msr+4
.endm
.global _start
_start:
""" + setup + r"""
    movl $1, %ebx
    cmpl $1, mock_msr
    jne 1f
    cmpl $0x12345678, mock_msr+4
    jne 1f
    xorl %ebx, %ebx
1:
    movl $1, %eax
    int $0x80
.section .data
mock_msr:
    .long 3, 0x12345678
.section .note.GNU-stack,"",@progbits
"""
        with tempfile.TemporaryDirectory(prefix="grantley-nem-",
                                         dir=os.environ.get("TMPDIR")) as tmp:
            assembly, binary = Path(tmp) / "setup.S", Path(tmp) / "setup"
            assembly.write_text(fixture)
            subprocess.run(["cc", "-m32", "-nostdlib", "-static", "-no-pie",
                            "-Wl,--build-id=none", str(assembly), "-o", str(binary)],
                           check=True)
            result = subprocess.run([str(binary)], timeout=5)
            self.assertEqual(result.returncode, 0,
                             "NEM Setup must set bit 0 and clear Run bit 1")

    def test_c_entry_contract(self):
        source = CAR.read_text()
        start = re.search(r"\bmovl?\s+\$_ecar_stack,\s*%esp", source)
        if start is None:
            self.fail("CAR must establish the C stack")
        # Run the production handoff, not a reimplementation of its instructions.
        # The preceding privileged cache/routing setup is deliberately not run.
        handoff = source[start.start():source.index(".Lhlt:", start.start())]
        fixture = r"""
#define post_code(value)
#define ENV_SEPARATE_DATA_AND_BSS 1
#define ENV_X86_64 0
.section .text
.code32
.global _start
_start:
    cld
    movl $0xdeadbeef, %eax
    movd %eax, %mm0
    movl $0x12345678, %eax
    movd %eax, %mm1
    movl $0x9abcdef0, %eax
    movd %eax, %mm2
    jmp handoff
handoff:
""" + handoff + r"""
    ud2
bootblock_c_entry_bist:
    xorl %ebx, %ebx
    leal 4(%esp), %eax
    testl $15, %eax
    jz 1f
    orl $1, %ebx
1:
    cmpl $0xdeadbeef, 12(%esp)
    je 2f
    orl $2, %ebx
2:
    cmpl $0x12345678, 4(%esp)
    jne 3f
    cmpl $0x9abcdef0, 8(%esp)
    je 4f
3:
    orl $4, %ebx
4:
    cmpl $0x55aa1234, _data
    jne 5f
    cmpl $0xaabbccdd, _data+12
    je 6f
5:
    orl $8, %ebx
6:
    movl $1, %eax
    int $0x80
.section .data
.balign 4
_data_load:
    .long 0x55aa1234, 0x87654321, 0x10203040, 0xaabbccdd
_data:
    .zero 16
_edata:
.section .bss
.balign 16
    .skip 65536
_ecar_stack:
.section .note.GNU-stack,"",@progbits
"""
        with tempfile.TemporaryDirectory(prefix="grantley-car-",
                                         dir=os.environ.get("TMPDIR")) as tmp:
            tmp = Path(tmp)
            assembly, binary = tmp / "handoff.S", tmp / "handoff"
            assembly.write_text(fixture)
            subprocess.run(["cc", "-m32", "-nostdlib", "-static", "-no-pie",
                            "-Wl,--build-id=none", "-I", str(ROOT / "src"),
                            str(assembly), "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], timeout=5)
            errors = {1: "misaligned C stack", 2: "lost BIST",
                      4: "lost initial TSC", 8: "uncopied .data"}
            failures = [text for bit, text in errors.items()
                        if result.returncode >= 0 and result.returncode & bit]
            self.assertEqual(result.returncode, 0,
                             ", ".join(failures) or f"exit {result.returncode}")


if __name__ == "__main__":
    unittest.main()
