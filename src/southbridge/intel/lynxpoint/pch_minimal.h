/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOUTHBRIDGE_INTEL_LYNXPOINT_PCH_MINIMAL_H
#define SOUTHBRIDGE_INTEL_LYNXPOINT_PCH_MINIMAL_H

/*
 * Temporary file to assist in unifying Lynx Point and Wildcat Point
 */

#if CONFIG(INTEL_LYNXPOINT_LP)
#define DEFAULT_PMBASE		0x1000
#define DEFAULT_GPIOBASE	0x1400
#define DEFAULT_GPIOSIZE	0x400
#define DEFAULT_PMBASE_SIZE	0x100
#else
/*
 * Grantley platforms use the server reference I/O map, where the 128-byte
 * ACPI/PM block starts at 0x400.  The SMBus BAR of such a platform sits at
 * 0x580 (see FIXED_SMBUS_IO_BASE) so the two LPC windows do not overlap:
 * PM1_TMR is at PMBASE + 8 and the TCO block at PMBASE + 0x60.
 *
 * DEFAULT_PMBASE_SIZE is the number of bytes the block decodes (PMSIZE in
 * southbridge/intel/common/pmbase.c) and the size of the ramstage PMBASE
 * resource.  At 0x400 it must not be the historical 256-byte span: that would
 * cover the GPIO block at 0x480 and the PIC ELCR at 0x4d0.
 */
#if CONFIG(NORTHBRIDGE_INTEL_GRANTLEY)
#define DEFAULT_PMBASE		0x400
#define DEFAULT_PMBASE_SIZE	0x80
#else
#define DEFAULT_PMBASE		0x500
#define DEFAULT_PMBASE_SIZE	0x100
#endif
#define DEFAULT_GPIOBASE	0x480
#define DEFAULT_GPIOSIZE	0x80
#endif

/*
 * The length the ACPI _CRS entry publishes for the PM block.  Grantley
 * declares the 0x80 bytes it decodes.  Client and LP builds keep the 0xff
 * byte this file declared before the length was parameterised, so their DSDT
 * stays byte-identical; only the ramstage resource follows
 * DEFAULT_PMBASE_SIZE there, and that resource is inert on those maps because
 * pch_lpc_add_io_resource() drops a range that ends below 0x1000.
 */
#if CONFIG(NORTHBRIDGE_INTEL_GRANTLEY)
#define DEFAULT_PMBASE_ASL_SIZE	DEFAULT_PMBASE_SIZE
#else
#define DEFAULT_PMBASE_ASL_SIZE	0xff
#endif

#ifndef __ACPI__

#include <types.h>

#if CONFIG(INTEL_LYNXPOINT_LP)
#define MAX_USB2_PORTS	10
#define MAX_USB3_PORTS	4
#else
#define MAX_USB2_PORTS	14
#define MAX_USB3_PORTS	6
#endif

/* There are 8 OC pins */
#define USB_OC_PIN_SKIP	8

enum usb2_port_location {
	USB_PORT_SKIP = 0,
	USB_PORT_BACK_PANEL,
	USB_PORT_FRONT_PANEL,
	USB_PORT_DOCK,
	USB_PORT_MINI_PCIE,
	USB_PORT_FLEX,
	USB_PORT_INTERNAL,
};

/*
 * USB port length is in MRC format: binary-coded decimal length in tenths of an inch.
 *   4.2 inches -> 0x0042
 *  12.7 inches -> 0x0127
 */
struct usb2_port_config {
	uint16_t length;
	bool enable;
	unsigned short oc_pin;
	enum usb2_port_location location;
};

struct usb3_port_config {
	bool enable;
	unsigned int oc_pin;
	/*
	 * Set to 0 if trace length is > 5 inches
	 * Set to 1 if trace length is <= 5 inches
	 */
	uint8_t fixed_eq;
};

/* Mainboard-specific USB configuration */
extern const struct usb2_port_config mainboard_usb2_ports[MAX_USB2_PORTS];
extern const struct usb3_port_config mainboard_usb3_ports[MAX_USB3_PORTS];

static inline bool pch_is_lp(void)
{
	return CONFIG(INTEL_LYNXPOINT_LP);
}

/* PCH platform types, safe for MRC consumption */
enum pch_platform_type {
	PCH_TYPE_MOBILE	 = 0,
	PCH_TYPE_DESKTOP = 1, /* or server */
	PCH_TYPE_ULT	 = 5,
};

#define MAINBOARD_POWER_OFF	0
#define MAINBOARD_POWER_ON	1
#define MAINBOARD_POWER_KEEP	2

#endif /* __ACPI__ */
#endif /* SOUTHBRIDGE_INTEL_LYNXPOINT_PCH_MINIMAL_H */
