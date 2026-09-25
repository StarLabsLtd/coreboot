/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>

#include "../../src/security/tpm/capsule_anchor_grant.c"

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

static bool protected_storage(void *context, const void *storage, size_t size)
{
	return context || (storage && size);
}

static struct capsule_tpm_anchor_binding valid_binding(void)
{
	return (struct capsule_tpm_anchor_binding) {
		.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION,
		.nv_index = 0x01001234,
		.authority_name = { 0, TPM_ALG_SHA256, 1 },
		.policy_ref_size = 1,
		.policy_ref = { 1 },
		.write_locked = 1,
	};
}

static struct capsule_tpm_anchor_grant valid_grant(void)
{
	struct capsule_tpm_anchor_grant grant = {
		.revision = CAPSULE_TPM_ANCHOR_GRANT_REVISION,
		.size = sizeof(grant),
		.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION,
		.nv_index = 0x01001234,
		.generation = 1,
		.transaction = 2,
		.flags = CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS,
		.current.epoch = 1,
		.current.digest = { 1 },
		.candidate.epoch = 2,
		.candidate.digest = { 2 },
	};

	return grant;
}

static void check_pair(bool expected, const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	CHECK(ranges_overlap(left, left_size, right, right_size) == expected);
	CHECK(ranges_overlap(right, right_size, left, left_size) == expected);
}

static void test_range_boundaries(void)
{
	const void *last_byte = (const void *)(UINTPTR_MAX - 15U);
	const void *crossing = (const void *)(UINTPTR_MAX - 14U);

	check_pair(true, last_byte, 16U, last_byte, 16U);
	check_pair(true, (const void *)UINTPTR_MAX, 1U,
		(const void *)UINTPTR_MAX, 1U);
	check_pair(false, last_byte, 16U, (const void *)0x1000, 16U);
	check_pair(true, crossing, 16U, (const void *)0x1000, 16U);
	check_pair(true, (const void *)0x1000, 0x100U,
		(const void *)0x1040, 0x20U);
	check_pair(true, (const void *)0x1000, 0x100U,
		(const void *)0x10ff, 1U);
	check_pair(false, (const void *)0x1000, 0x100U,
		(const void *)0x1100, 0x20U);
	check_pair(true, (const void *)0x1000, 0U,
		(const void *)0x2000, 16U);
	check_pair(true, NULL, 16U, (const void *)0x2000, 16U);
}

static void test_install_aliases(void)
{
	struct capsule_tpm_anchor_grant grant = valid_grant();
	struct capsule_tpm_anchor_binding binding = valid_binding();
	union {
		struct capsule_tpm_anchor_grant grant;
		struct capsule_tpm_anchor_binding binding;
	} shared;

	memset(&authority, 0, sizeof(authority));
	CHECK(capsule_tpm_anchor_grant_install(&authority.grant, &binding,
		protected_storage, NULL) == CB_ERR);
	CHECK(authority.install_attempted && !authority.installed);

	memset(&authority, 0, sizeof(authority));
	CHECK(capsule_tpm_anchor_grant_install(&grant,
		(const struct capsule_tpm_anchor_binding *)&authority,
		protected_storage, NULL) == CB_ERR);
	CHECK(authority.install_attempted && !authority.installed);

	memset(&authority, 0, sizeof(authority));
	CHECK(capsule_tpm_anchor_grant_install(&shared.grant, &shared.binding,
		protected_storage, NULL) == CB_ERR);
	CHECK(authority.install_attempted && !authority.installed);
}

static void install_grant(struct capsule_tpm_anchor_grant *grant,
	struct capsule_tpm_anchor_binding *binding)
{
	memset(&authority, 0, sizeof(authority));
	CHECK(capsule_tpm_anchor_grant_install(grant, binding,
		protected_storage, NULL) == CB_SUCCESS);
}

static void test_consume_aliases(void)
{
	struct capsule_tpm_anchor_grant grant = valid_grant();
	struct capsule_tpm_anchor_binding binding = valid_binding();

	install_grant(&grant, &binding);
	CHECK(capsule_tpm_anchor_grant_consume(grant.generation,
		grant.transaction, (const void *)&authority, &grant.candidate) ==
		CB_ERR);
	CHECK(authority.consumed && authority.poisoned);

	install_grant(&grant, &binding);
	CHECK(capsule_tpm_anchor_grant_consume(grant.generation,
		grant.transaction, &grant.current, (const void *)&authority) ==
		CB_ERR);
	CHECK(authority.consumed && authority.poisoned);
}

int main(void)
{
	test_range_boundaries();
	test_install_aliases();
	test_consume_aliases();
	return 0;
}
