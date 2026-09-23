/* SPDX-License-Identifier: GPL-2.0-only */

/* Restore production media symbol names after renaming the fixture backends. */
#undef payload_mm_authvar_media_begin
#undef payload_mm_authvar_media_read
#undef payload_mm_authvar_media_end
#undef payload_mm_authvar_media_buffer_disjoint
enum payload_mm_authvar_media_result payload_mm_authvar_media_begin(
	uint64_t *generation, uint64_t *token);
enum payload_mm_authvar_media_result payload_mm_authvar_media_read(
	uint64_t generation, uint64_t token, uint32_t offset, void *buffer,
	size_t size);
enum payload_mm_authvar_media_result payload_mm_authvar_media_end(
	uint64_t generation, uint64_t token);
bool payload_mm_authvar_media_buffer_disjoint(const void *buffer, size_t size);
#include "../../src/lib/payload_mm_authvar_executor.c"
