/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * blatantly copied from linux/kernel/printk.c
 */

#include <console/cbmem_console.h>
#include <console/console.h>
#include <console/streams.h>
#include <console/vtxprintf.h>
#include <smp/spinlock.h>
#include <smp/node.h>
#include <timer.h>
#include <types.h>

DECLARE_SPIN_LOCK(console_lock)

#define TRACK_CONSOLE_TIME (!ENV_SMM && CONFIG(HAVE_MONOTONIC_TIMER))

static struct mono_time mt_start, mt_stop;
static long console_usecs;

static void console_time_run(void)
{
	if (TRACK_CONSOLE_TIME && boot_cpu())
		timer_monotonic_get(&mt_start);
}

static void console_time_stop(void)
{
	if (TRACK_CONSOLE_TIME && boot_cpu()) {
		timer_monotonic_get(&mt_stop);
		console_usecs += mono_time_diff_microseconds(&mt_start, &mt_stop);
	}
}

void console_time_report(void)
{
	if (!TRACK_CONSOLE_TIME)
		return;

	printk(BIOS_DEBUG, "BS: " ENV_STRING " times (exec / console): total (unknown) / %ld ms\n",
		DIV_ROUND_CLOSEST(console_usecs, USECS_PER_MSEC));
}

long console_time_get_and_reset(void)
{
	if (!TRACK_CONSOLE_TIME)
		return 0;

	long elapsed = console_usecs;
	console_usecs = 0;
	return elapsed;
}

void do_putchar(unsigned char byte)
{
	console_time_run();
	console_tx_byte(byte);
	console_time_stop();
}

union log_state {
	void *as_ptr;
	struct {
		uint8_t level;
		uint8_t speed;
	};
};

#define LOG_FAST(state) (HAS_ONLY_FAST_CONSOLES || ((state).speed == CONSOLE_LOG_FAST))

static void wrap_interactive_printf(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vtxprintf(console_interactive_tx_byte, fmt, args, NULL);
	va_end(args);
}

static void line_start(union log_state state)
{
	if (state.level > BIOS_LOG_PREFIX_MAX_LEVEL)
		return;

	/* Stored consoles just get a single control char marker to save space. If we are in
	   LOG_FAST mode, just write the marker to CBMC and exit -- the rest of this function
	   implements the LOG_ALL case. */
	unsigned char marker = BIOS_LOG_LEVEL_TO_MARKER(state.level);
	if (LOG_FAST(state)) {
		__cbmemc_tx_byte(marker);
		return;
	}
	if (CONFIG(SOC_QUALCOMM_MSM89X7))
		*(volatile uint64_t *)(uintptr_t)0xbffffff8 = 0xcb340040;
	console_stored_tx_byte(marker, NULL);
	if (CONFIG(SOC_QUALCOMM_MSM89X7))
		*(volatile uint64_t *)(uintptr_t)0xbffffff8 = 0xcb340050;

	/* Interactive consoles get a `[DEBUG]  ` style readable prefix,
	   and potentially an escape sequence for highlighting. */
	if (CONFIG(SOC_QUALCOMM_MSM89X7)) {
		console_interactive_tx_byte('[', NULL);
		if (state.level <= BIOS_LOG_PREFIX_MAX_LEVEL) {
			for (int j = 0; j < 5; j++)
				console_interactive_tx_byte(bios_log_prefix[state.level][j], NULL);
		} else {
			const char *p = "LOG  ";
			while (*p)
				console_interactive_tx_byte(*p++, NULL);
		}
		console_interactive_tx_byte(']', NULL);
		console_interactive_tx_byte(' ', NULL);
		console_interactive_tx_byte(' ', NULL);
		return;
	}

	if (CONFIG(CONSOLE_USE_ANSI_ESCAPES))
		wrap_interactive_printf(BIOS_LOG_ESCAPE_PATTERN, bios_log_escape[state.level]);
	if (CONFIG(CONSOLE_USE_LOGLEVEL_PREFIX))
		wrap_interactive_printf(BIOS_LOG_PREFIX_PATTERN, bios_log_prefix[state.level]);
}

static void line_end(union log_state state)
{
	if (CONFIG(CONSOLE_USE_ANSI_ESCAPES) && !LOG_FAST(state))
		wrap_interactive_printf(BIOS_LOG_ESCAPE_RESET);
}

static void wrap_putchar(unsigned char byte, void *data)
{
	union log_state state = { .as_ptr = data };
	static bool line_started = false;

	if (byte == '\n') {
		line_end(state);
		line_started = false;
	} else if (!line_started) {
		line_start(state);
		line_started = true;
	}

	if (LOG_FAST(state))
		__cbmemc_tx_byte(byte);
	else
		console_tx_byte(byte);
}

int vprintk(int msg_level, const char *fmt, va_list args)
{
	union log_state state = { .level = msg_level };
	int i;

	if (CONFIG(SQUELCH_EARLY_SMP) && ENV_ROMSTAGE_OR_BEFORE && !boot_cpu())
		return 0;

	if (CONFIG(SOC_QUALCOMM_MSM89X7)) {
		uint64_t ticks, sctlr;
		__asm__ volatile("mrs %0, cntpct_el0" : "=r"(ticks));
		__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
		*(volatile uint64_t *)(uintptr_t)0xbfffffb0 = ticks;
		*(volatile uint64_t *)(uintptr_t)0xbfffffb8 = sctlr;
		*(volatile uint64_t *)(uintptr_t)0xbffffff0 = 0xcb330010;
	}
	state.speed = console_log_level(msg_level);
	if (state.speed < CONSOLE_LOG_FAST)
		return 0;
	if (CONFIG(SOC_QUALCOMM_MSM89X7))
		*(volatile uint64_t *)(uintptr_t)0xbffffff0 = 0xcb330020;

	spin_lock(&console_lock);

	console_time_run();
	if (CONFIG(SOC_QUALCOMM_MSM89X7))
		*(volatile uint64_t *)(uintptr_t)0xbffffff0 = 0xcb330030;

	if (CONFIG(SOC_QUALCOMM_MSM89X7))
		*(volatile uint64_t *)(uintptr_t)0xbffffff8 = 0xcb340010;
	i = vtxprintf(wrap_putchar, fmt, args, state.as_ptr);
	if (CONFIG(SOC_QUALCOMM_MSM89X7))
		*(volatile uint64_t *)(uintptr_t)0xbffffff0 = 0xcb330040;
	if (!LOG_FAST(state))
		console_tx_flush();

	console_time_stop();

	spin_unlock(&console_lock);

	return i;
}

int printk(int msg_level, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vprintk(msg_level, fmt, args);
	va_end(args);

	return i;
}
