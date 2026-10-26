/* Minimal ARM64 payload for coreboot on MSM8937 santoni */
.global _start
.section .text.entry, "ax"
_start:
	/* Entry: x0 receives coreboot tables / arg pointer */
	mov	x19, x0

	/* Paint a bright green rectangle at top-left of continuous splash FB */
	ldr	x1, =0x90001000		/* FB base */
	ldr	w2, =0x00FF00		/* Green in RGB888 / XRGB8888 */
	ldr	x3, =720 * 80		/* 80 lines */
1:
	str	w2, [x1], #4
	subs	x3, x3, #1
	b.ne	1b

	/* Hang safely */
2:
	wfe
	b	2b
