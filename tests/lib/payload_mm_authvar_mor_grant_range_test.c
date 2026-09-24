/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>

#include "../../src/lib/payload_mm_authvar_mor_grant.c"

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

static bool protected_storage(void *context, const void *storage, size_t size)
{
	return context || (storage && size);
}

int main(void)
{
	const size_t size = sizeof(struct payload_mm_authvar_mor_grant);
	const void *last_exclusive_safe = (const void *)(UINTPTR_MAX - size);
	const void *last_byte_safe = (const void *)(UINTPTR_MAX - (size - 1U));
	const void *overflowing = (const void *)(UINTPTR_MAX - (size - 2U));

	CHECK(object_valid(last_exclusive_safe, size, 1));
	CHECK(object_valid(last_byte_safe, size, 1));
	CHECK(!object_valid(overflowing, size, 1));
	CHECK(ranges_overlap(last_exclusive_safe, size,
		last_exclusive_safe, size));
	CHECK(ranges_overlap(last_byte_safe, size, last_byte_safe, size));
	CHECK(!ranges_overlap(last_byte_safe, size, (const void *)0x1000, size));

	/* Close scrubs both private receipt buffers and seals the install slot. */
	memset(&authority.grant, 0xa5, sizeof(authority.grant));
	memset(&authority.candidate, 0x5a, sizeof(authority.candidate));
	CHECK(payload_mm_authvar_mor_grant_close() == CB_SUCCESS);
	CHECK(authority.install_attempted && authority.poisoned);
	CHECK(bytes_zero(&authority.grant, sizeof(authority.grant)));
	CHECK(bytes_zero(&authority.candidate, sizeof(authority.candidate)));
	CHECK(payload_mm_authvar_mor_grant_close() == CB_ERR);
	memset(&authority, 0, sizeof(authority));

	/* An input aliasing any byte of protected authority is rejected terminally. */
	CHECK(payload_mm_authvar_mor_grant_install(
		(const struct payload_mm_authvar_mor_grant *)&authority,
		protected_storage, NULL) == CB_ERR);
	CHECK(authority.install_attempted);
	CHECK(authority.poisoned);
	CHECK(!payload_mm_authvar_mor_grant_ready());
	return 0;
}
