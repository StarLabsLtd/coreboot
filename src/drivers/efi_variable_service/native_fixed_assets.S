/* SPDX-License-Identifier: GPL-2.0-only */

	.section .rodata.ass_fixed_assets,"a",@progbits
.macro asset name, file
	.global ass_dbx_\name
	.global ass_dbx_\name\()_end
ass_dbx_\name:
	.incbin "\file"
ass_dbx_\name\()_end:
.endm
	asset baseline, ASS_DBX_BASELINE
	asset append, ASS_DBX_APPEND
	asset update, ASS_DBX_UPDATE

	.section .note.GNU-stack,"",@progbits
