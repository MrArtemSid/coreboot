/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/smp/smp.h>
#include <cbmem.h>
#include <commonlib/device_tree.h>
#include <console/console.h>
#include <endian.h>
#include <mcall.h>
#include <soc/scr_hal.h>
#include <string.h>

/* SoC-private CBMEM ID: "SFDT". The full allocation is reserved in bootmem. */
#define SCR_CBMEM_ID_FDT 0x53464454
#define FDT_GROWTH_SLACK (16 * 1024)

/* Each stage owns this context; no returning stack frame may back the trampoline. */
static struct {
	void (*fn)(void *);
	void *arg;
} handoff;

#if ENV_RAMSTAGE
static void reserve_handoff_fdt(int is_recovery)
{
	(void)is_recovery;
	void *fdt = HLS()->fdt;
	if (!fdt || !fdt_is_valid(fdt))
		die("SCR: invalid input FDT");

	struct device_tree *tree = fdt_unflatten(fdt);
	if (!tree)
		die("SCR: cannot unpack input FDT");

	/* Budget the generic boot.c addition, including a missing /chosen node.
	 * Repacking can duplicate shared property names, so input totalsize is
	 * not a bound on the generic serializer's output.
	 */
	struct device_tree_node *chosen = dt_find_node_by_path(tree, "/chosen", NULL, NULL, 1);
	if (!chosen)
		die("SCR: cannot create /chosen");
	dt_add_u64_prop(chosen, "coreboot-table", 0);

	size_t packed_size = dt_flat_size(tree);
	if (packed_size < sizeof(struct fdt_header) ||
	    packed_size > UINT32_MAX - FDT_GROWTH_SLACK)
		die("SCR: invalid repacked FDT size");

	size_t capacity = packed_size + FDT_GROWTH_SLACK;
	const struct cbmem_entry *entry = cbmem_entry_add(SCR_CBMEM_ID_FDT, capacity);
	/* cbmem_entry_add() reuses an existing ID without resizing it. */
	if (!entry || cbmem_entry_size(entry) < capacity)
		die("SCR: cannot reserve next-stage FDT");
}
CBMEM_READY_HOOK(reserve_handoff_fdt);

static void relocate_handoff_fdt(void)
{
	/* boot.c has now published its final, coreboot-table-bearing heap FDT. */
	void *fdt = HLS()->fdt;
	if (!fdt || !fdt_is_valid(fdt))
		die("SCR: invalid final FDT");

	size_t packed_size = be32_to_cpu(((const struct fdt_header *)fdt)->totalsize);
	const struct cbmem_entry *entry = cbmem_entry_find(SCR_CBMEM_ID_FDT);
	if (packed_size < sizeof(struct fdt_header) ||
	    packed_size > UINT32_MAX - FDT_GROWTH_SLACK || !entry ||
	    cbmem_entry_size(entry) < packed_size + FDT_GROWTH_SLACK)
		die("SCR: next-stage FDT exceeds reserved capacity");

	void *next_fdt = cbmem_entry_start(entry);
	memcpy(next_fdt, fdt, packed_size);
	/* Keep packed totalsize: OpenSBI expands from totalsize, not capacity. */
	scr_hal_cache_sync_code((uintptr_t)next_fdt, packed_size);

	unsigned int hart_count = CONFIG_MAX_CPUS;
	if (CONFIG(RISCV_GET_HART_COUNT_AT_RUNTIME))
		hart_count = smp_get_hart_count();
	if (!hart_count || hart_count > CONFIG_MAX_CPUS)
		die("SCR: invalid handoff hart count");

	for (unsigned int i = 0; i < hart_count; i++) {
		OTHER_HLS(i)->fdt = next_fdt;
		scr_hal_cache_sync_code((uintptr_t)OTHER_HLS(i), sizeof(*OTHER_HLS(i)));
	}
}
#endif

static void handoff_trampoline(void *arg)
{
	(void)arg;
	/* Runs on every receiving hart, before the first next-stage instruction. */
	__asm__ volatile ("fence.i" ::: "memory");
	handoff.fn(handoff.arg);
	die("SCR: stage handoff returned");
}

void __real_smp_resume(void (*fn)(void *), void *arg);
void __wrap_smp_resume(void (*fn)(void *), void *arg) __noreturn;

void __wrap_smp_resume(void (*fn)(void *), void *arg)
{
	if (!fn)
		die("SCR: missing stage handoff callback");
#if ENV_RAMSTAGE
	relocate_handoff_fdt();
#endif
	handoff.fn = fn;
	handoff.arg = arg;
	scr_hal_cache_sync_code((uintptr_t)&handoff, sizeof(handoff));
	/* Never return: the original callback's arguments may live on our caller's stack. */
	__real_smp_resume(handoff_trampoline, &handoff);
	die("SCR: smp_resume returned");
}
