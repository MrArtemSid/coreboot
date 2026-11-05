/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_GRANTLEY_PEI_IMAGE_H
#define NORTHBRIDGE_INTEL_GRANTLEY_PEI_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include <PiPei.h>

/* Check the in-place PE32 image and return its entry point. */
EFI_STATUS grantley_peim_entry(const void *image, size_t size, uintptr_t *entry);

#endif /* NORTHBRIDGE_INTEL_GRANTLEY_PEI_IMAGE_H */
