/* SPDX-License-Identifier: GPL-2.0-only */

	.section .rodata.ass_capsule_trust,"a",@progbits
	.global ass_capsule_trust_start
	.global ass_capsule_trust_end
ass_capsule_trust_start:
	.incbin ASS_CAPSULE_TRUST_DER
ass_capsule_trust_end:

	.section .note.GNU-stack,"",@progbits
