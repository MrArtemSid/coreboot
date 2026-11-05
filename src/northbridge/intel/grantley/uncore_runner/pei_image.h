/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_PEI_IMAGE_H
#define NORTHBRIDGE_INTEL_GRANTLEY_PEI_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <PiPei.h>

/*
 * The vendor UncoreInitPeim is a PE32 (i386) image whose ImageBase is the
 * address at which the stock firmware executes it from memory mapped SPI
 * flash.  CBFS placement already puts the payload there (coreboot.rom
 * 0xDCF9B8 == ImageBase), so this port verifies the geometry and executes it
 * in place.  There is deliberately no loader or relocator: copying 605 KiB
 * into a 1 MiB CAR is impossible and a fixed-up copy was never exercised.
 */
struct grantley_peim_image {
	uint32_t image_base;
	uint32_t image_size;
	uint32_t entry_rva;
	uint32_t headers_size;
	uint16_t sections;
	uint32_t reloc_rva;
	uint32_t reloc_size;
	/* Every section has VirtualAddress == PointerToRawData and its file
	 * bytes cover the whole virtual extent (SizeOfRawData >= VirtualSize).
	 * SizeOfRawData is normally aligned up, so equality is not required;
	 * a virtual hole (VirtualSize > SizeOfRawData) is what makes in-place
	 * execution read bytes that are not in the file.
	 */
	bool xip_compatible;
};

/* Validate the PE32 headers, sections and entry point.  xip_compatible is
 * true only when every section is flat (VirtualAddress == PointerToRawData and
 * SizeOfRawData >= VirtualSize) and the entry point is backed by file bytes.
 */
EFI_STATUS grantley_peim_verify(const void *image, size_t size,
				struct grantley_peim_image *out);

#endif /* NORTHBRIDGE_INTEL_GRANTLEY_PEI_IMAGE_H */
