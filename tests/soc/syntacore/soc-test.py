import pathlib, subprocess, tempfile, unittest
ROOT = pathlib.Path(__file__).resolve().parents[3] / 'src/soc/syntacore'
_tmp = tempfile.TemporaryDirectory(prefix='scr9-soc-test-')
TMP = pathlib.Path(_tmp.name)

def function(file, name):
    text = (ROOT / file).read_text()
    import re
    start = re.search(r'^\w[^\n]*\b' + name + r'\([^;]*?\)\n\{', text, re.M).start()
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

def run_c(name, code):
    src = TMP / (name + '.c')
    exe = TMP / name
    src.write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <assert.h>\n#include <stdlib.h>\n#include <setjmp.h>\n' + code)
    subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=5)

class SocTests(unittest.TestCase):
    def test_mrt_active_catchall(self):
        run_c('mrt', '''
#define PLF_MRT_SUPPORT 1
#define MRT_ENTRY_START 1
#define MRT_ENTRY_VALID 1
static size_t regs[3];
#define MRT_A_ENTRY_N_CTRL(n) (&regs[0])
#define MRT_A_ENTRY_N_ADDR(n) (&regs[1])
#define MRT_A_ENTRY_N_MASK(n) (&regs[2])
''' + function('scr_hal_glue.c', 'scr_hal_mrt_is_initialized') + '''
int main(void) {
 regs[0]=1; regs[1]=0; regs[2]=0; assert(scr_hal_mrt_is_initialized());
 regs[2]=(size_t)-1; assert(!scr_hal_mrt_is_initialized());
 regs[2]=0xfffc000000ULL; assert(!scr_hal_mrt_is_initialized());
 regs[2]=0; regs[1]=1; assert(!scr_hal_mrt_is_initialized());
 regs[1]=0; regs[0]=0; assert(!scr_hal_mrt_is_initialized());
}
''')

    def test_pma_layout(self):
        run_c('pma', '''
#define PLF_PMP_SUPPORT 1
#define PLF_OCRAM_BASE 0x2000000
#define PLF_OCRAM_SIZE 0x10000
#define PLF_MMCFG_BASE 0xe8000000
#define PLF_MMCFG_SIZE 0x2000
#define PLF_MEM_BASE 0x400000000ULL
#define PLF_MEM_SIZE 0x80000000
#define PMA_MEM_REGION_C_WO 24
#define PMA_MEM_REGION_IO 7
#define PLF_OCRAM_ATTR 1
#define PLF_MMCFG_ATTR 2
#define PLF_MEM_ATTR 3
static unsigned count=8, writes;
static jmp_buf failed;
static bool pma_is_available(void) { return true; }
static size_t get_pma_regions(void) { return count; }
static void set_pma_region(size_t n,size_t base,size_t size,size_t attr) {
 assert(n==count-3+writes);
 if(writes==0) {assert(base==PLF_OCRAM_BASE); assert(size==PLF_OCRAM_SIZE); assert(attr==24);}
 if(writes==1) {assert(base==PLF_MEM_BASE); assert(size==PLF_MEM_SIZE); assert(attr==24);}
 if(writes==2) {assert(base==0); assert(size==0); assert(attr==7);}
 writes++;
}
#define con_puts(msg) ((void)0)
#define create_pmp_region(a,b,c,d) ((void)0)
#define ifence() ((void)0)
#define scr_hal_fatal(msg) longjmp(failed,1)
''' + function('scr_hal_glue.c', 'scr_hal_pmp_platform_init') + '''
int main(void) {
 scr_hal_pmp_platform_init(); assert(writes==3);
 count=2; writes=0;
 if (!setjmp(failed)) { scr_hal_pmp_platform_init(); assert(0); }
 assert(writes==0);
}
''')

    def test_cache_policy(self):
        for dedicated in (0, 1):
            run_c('cache' + str(dedicated), '''
#define PLF_CACHE_L2_DEDICATED %d
static bool l2_on;
static unsigned l1_calls,l2_calls,l3_calls;
#define fence() ((void)0)
#define ifence() ((void)0)
static bool plf_l2cache_is_enabled(void) {return l2_on;}
static void plf_l1cache_init(void) {l1_calls++;}
static void plf_l2cache_init(void) {l2_calls++; l2_on=true;}
static void plf_l3cache_init(void) {l3_calls++;}
''' % dedicated + function('scr_hal_glue.c','scr_hal_cache_shared_init') + '\n' + function('scr_hal_glue.c','scr_hal_cache_local_init') + '''
int main(void) {
 scr_hal_cache_shared_init();
 assert(l3_calls==1); assert(l2_calls==!PLF_CACHE_L2_DEDICATED);
 scr_hal_cache_local_init(); assert(l1_calls==1); assert(l2_calls==1);
 scr_hal_cache_local_init(); assert(l1_calls==2); assert(l2_calls==1);
}
''')

    def test_hart_bounds(self):
        run_c('hart_bounds', '''
#define CONFIG(x) 1
#define ENV_BOOTBLOCK 1
#define CONFIG_MAX_CPUS 4
#define CONFIG_RISCV_WORKING_HARTID 0
static unsigned long mock_hartid=4;
#define read_csr(csr) mock_hartid
static int iccm_ready;
static int hart_ready[4];
static bool scr_is_cold_reset;
static jmp_buf failed;
#define die(msg) longjmp(failed,1)
#define scr_hal_pmp_platform_init() ((void)0)
#define scr_hal_mrt_is_initialized() 1
#define scr_hal_mrt_init() ((void)0)
#define scr_hal_iccm_init() ((void)0)
#define scr_hal_cache_shared_init() ((void)0)
#define scr_hal_cache_local_init() ((void)0)
#define scr_hal_cache_sync_code(a,b) ((void)0)
#define SCR_HART_WAIT_SPINS 100
''' + function('iccm.c','scr_iccm_init') + '''
int main(void) {
 if(!setjmp(failed)) {scr_iccm_init(); assert(0);}
 assert(iccm_ready==0); assert(!scr_is_cold_reset);
 mock_hartid=0; scr_iccm_init(); assert(hart_ready[0]==1);
}
''')

    def test_missing_hart_fails_closed(self):
        run_c('missing', '''
#define CONFIG(x) 1
#define CONFIG_MAX_CPUS 4
#define CONFIG_RISCV_WORKING_HARTID 0
#define SCR_HART_WAIT_SPINS 10
#define OTHER_HLS(i) (&hls[i])
static struct { struct { struct {int counter;} sync_a;} entry;} hls[4];
static int hart_ready[4];
static unsigned hardware_count=2;
static unsigned scr_hal_iccm_hart_count(void) {return hardware_count;}
static jmp_buf failed;
#define die(msg) longjmp(failed,1)
#define HLS() ((void*)0)
typedef unsigned u32;
static bool fdt_is_valid(const void *f) {(void)f; return true;}
#define fdt_find_node_by_path(a,b,c,d) 0
#define fdt_find_subnodes_by_prefix(a,b,c,d,e,f,g) ((void)(b),(void)(f),0)
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
''' + function('iccm.c','smp_get_hart_count').replace('const void *fdt = HLS()->fdt;', 'const void *fdt = HLS();') + '''
int main(void) {
 if(!setjmp(failed)) {smp_get_hart_count(); assert(0);}
 hart_ready[1]=1;
 if(!setjmp(failed)) {smp_get_hart_count(); assert(0);}
 hls[1].entry.sync_a.counter=1;
 assert(smp_get_hart_count()==2);
 hardware_count=5;
 if(!setjmp(failed)) {smp_get_hart_count(); assert(0);}
}
''')

    def test_loaded_code_publication(self):
        run_c('publish', '''
static unsigned sequence;
static void cache_l1_flush(void *p,long size) {assert((uintptr_t)p==0x1234); assert(size==123); assert(sequence++==0);}
#define ifence() assert(sequence++==1)
''' + function('scr_hal_glue.c','scr_hal_cache_sync_code') + '''
int main(void) {scr_hal_cache_sync_code(0x1234,123); assert(sequence==2);}
''')

    def test_timer_conversion(self):
        run_c('timer', '''
struct mono_time { uint64_t microseconds; };
static uint64_t ticks;
static unsigned long hz;
uint64_t scr_hal_mtimer_now(void) {return ticks;}
unsigned long scr_hal_timebase_hz(void) {return hz;}
#define USECS_PER_SEC 1000000
#define die(msg) abort()
''' + function('mtime.c', 'timer_monotonic_get') + '''
int main(void) {
 struct mono_time mt;
 hz=10000000; ticks=50000000000ULL; timer_monotonic_get(&mt); assert(mt.microseconds==5000000000ULL);
 hz=32768; ticks=32769; timer_monotonic_get(&mt); assert(mt.microseconds==1000030);
 hz=1500000; ticks=1500001; timer_monotonic_get(&mt); assert(mt.microseconds==1000000);
 hz=10000000; ticks=UINT64_MAX; timer_monotonic_get(&mt); assert(mt.microseconds==1844674407370955161ULL);
}
''')

if __name__ == '__main__': unittest.main()
