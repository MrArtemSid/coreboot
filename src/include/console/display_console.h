/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _CONSOLE_DISPLAY_CONSOLE_H_
#define _CONSOLE_DISPLAY_CONSOLE_H_

#include <types.h>

/*
 * Display console: draws the log into a framebuffer provided by the
 * platform (e.g. the continuous splash framebuffer left running by an
 * earlier firmware stage). Implemented by the SoC.
 */
#if CONFIG(CONSOLE_DISPLAY)
void __display_console_init(void);
void __display_console_tx_byte(unsigned char data);
void __display_console_tx_flush(void);
#else
static inline void __display_console_init(void) {}
static inline void __display_console_tx_byte(unsigned char data) {}
static inline void __display_console_tx_flush(void) {}
#endif

#endif /* _CONSOLE_DISPLAY_CONSOLE_H_ */
