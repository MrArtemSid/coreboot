/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_QUALCOMM_MSM89X7_PAYLOAD_FB_H__
#define __SOC_QUALCOMM_MSM89X7_PAYLOAD_FB_H__

/*
 * Convert the LK splash to XRGB8888 and enable it as simple-framebuffer
 * in a copy of @fdt. Returns the new device tree, or @fdt unchanged if
 * there is nothing to hand over.
 */
void *msm89x7_payload_fb_handoff(void *fdt);

#endif /* __SOC_QUALCOMM_MSM89X7_PAYLOAD_FB_H__ */
