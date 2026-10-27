/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _CONSOLE_RAMOOPS_CONSOLE_H_
#define _CONSOLE_RAMOOPS_CONSOLE_H_

#include <types.h>

/*
 * Ramoops console: mirrors the log into a fixed RAM buffer in the pstore
 * persistent_ram console format, so it survives a reset and can be
 * fetched by a later boot stage or by the primary bootloader (e.g.
 * lk2nd's "fastboot oem ramoops console"). Implemented by the SoC.
 */
#if CONFIG(CONSOLE_RAMOOPS)
void __ramoops_init(void);
void __ramoops_tx_byte(unsigned char data);
#else
static inline void __ramoops_init(void) {}
static inline void __ramoops_tx_byte(unsigned char data) {}
#endif

#endif /* _CONSOLE_RAMOOPS_CONSOLE_H_ */
