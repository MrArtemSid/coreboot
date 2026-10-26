#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compile the real console header and printk LOG_FAST predicate on the host."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FastConsoleTest(unittest.TestCase):
    def check_console(self, defines, fast_only):
        # Use the production macro, not a second implementation of its semantics.
        printk = (ROOT / 'src/console/printk.c').read_text()
        match = re.search(r'^#define LOG_FAST\(state\).+$', printk, re.M)
        self.assertIsNotNone(match)
        assert match is not None
        log_fast = match.group()
        harness = r'''
#include <stdint.h>
#include <kconfig.h>
#define ENV_TEST 1
#define __noreturn __attribute__((noreturn))
#define __printf(a, b) __attribute__((format(printf, a, b)))
typedef uint8_t u8;
#include <console/console.h>
'''+log_fast+f'''
_Static_assert((HAS_ONLY_FAST_CONSOLES) == {int(fast_only)},
               "wrong fast-only console classification");
int main(void)
{{
    struct {{ int speed; }} state;
    state.speed = CONSOLE_LOG_FAST;
    if (!LOG_FAST(state)) return 1;
    state.speed = CONSOLE_LOG_ALL;
    if ((!!LOG_FAST(state)) != {int(fast_only)}) return 2;
    return 0;
}}
'''
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            tmp = Path(tmp)
            (tmp / 'config.h').write_text(
                '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 7\n'+
                ''.join(f'#define CONFIG_{key} {value}\n' for key, value in defines.items()))
            (tmp / 'test.c').write_text(harness)
            command = ['cc', '-std=gnu11', '-Wall', '-Werror', '-I'+str(tmp),
                       '-I'+str(ROOT / 'src/include'),
                       '-I'+str(ROOT / 'src/commonlib/include'),
                       '-I'+str(ROOT / 'src/commonlib/bsd/include'), str(tmp / 'test.c'),
                       '-o', str(tmp / 'test')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(tmp / 'test')], check=True)

    def test_ramoops_only_is_not_fast_only(self):
        self.check_console({'CONSOLE_RAMOOPS': 1}, False)

    def test_cbmem_only_is_fast_only(self):
        self.check_console({'CONSOLE_CBMEM': 1}, True)

    def test_display_and_ramoops_are_not_fast_only(self):
        self.check_console({'CONSOLE_DISPLAY': 1, 'CONSOLE_RAMOOPS': 1}, False)

    def test_serial_log_fast_semantics_are_unchanged(self):
        self.check_console({'CONSOLE_SERIAL': 1, 'CONSOLE_CBMEM': 1}, False)


if __name__ == '__main__':
    unittest.main()
