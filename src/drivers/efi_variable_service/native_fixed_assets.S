/* SPDX-License-Identifier: GPL-2.0-only */

	.section .rodata.ass_fixed_assets,"a",@progbits
.macro asset name, file
	.global ass_\name
	.global ass_\name\()_end
ass_\name:
	.incbin "\file"
ass_\name\()_end:
.endm
	asset pk, "src/drivers/efi_variable_service/keys/pk.esl"
	asset kek, "src/drivers/efi_variable_service/keys/kek.esl"
	asset db, "src/drivers/efi_variable_service/keys/db.esl"
	asset dbx_baseline, "src/drivers/efi_variable_service/keys/dbx_baseline.auth"
	asset dbx_append, "src/drivers/efi_variable_service/keys/dbx_append.esl"
	asset dbx_update, "src/drivers/efi_variable_service/keys/dbx_update.auth"

	.section .note.GNU-stack,"",@progbits
