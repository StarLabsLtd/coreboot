/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/capsule_anchor_grant.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

struct protection_context {
	struct capsule_tpm_anchor_grant *grant;
	struct capsule_tpm_anchor_binding *binding;
	bool protected;
	bool mutate_grant;
	bool mutate_binding;
	bool mutate_authority;
	const void *authority;
	size_t authority_size;
};

static struct capsule_tpm_anchor_binding valid_binding(void)
{
	struct capsule_tpm_anchor_binding binding = {
		.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION,
		.nv_index = 0x01001234,
		.authority_name = { 0, 0x0b, 1 },
		.policy_ref_size = 7,
		.policy_ref = { 'c', 'a', 'p', 's', 'u', 'l', 'e' },
		.write_locked = 1,
	};

	return binding;
}

static struct capsule_tpm_anchor_grant valid_grant(void)
{
	struct capsule_tpm_anchor_grant grant = {
		.revision = CAPSULE_TPM_ANCHOR_GRANT_REVISION,
		.size = sizeof(grant),
		.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION,
		.nv_index = 0x01001234,
		.generation = 9,
		.transaction = 17,
		.flags = CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS,
		.current.epoch = 4,
		.candidate.epoch = 5,
	};

	for (size_t i = 0; i < sizeof(grant.current.digest); i++) {
		grant.current.digest[i] = i + 1;
		grant.candidate.digest[i] = 0x80 + i;
	}
	return grant;
}

static bool protected_storage(void *opaque, const void *storage, size_t size)
{
	struct protection_context *context = opaque;

	CHECK(storage != NULL);
	CHECK(size != 0);
	context->authority = storage;
	context->authority_size = size;
	if (context->mutate_grant)
		context->grant->generation++;
	if (context->mutate_binding)
		context->binding->nv_index++;
	if (context->mutate_authority)
		((uint8_t *)storage)[0] ^= 1;
	return context->protected;
}

static bool grant_cleared(const struct protection_context *context)
{
	const uint8_t *bytes = context->authority;
	uint8_t value = 0;

	CHECK(context->authority != NULL);
	CHECK(context->authority_size >= sizeof(struct capsule_tpm_anchor_grant));
	for (size_t i = 0; i < sizeof(struct capsule_tpm_anchor_grant); i++)
		value |= bytes[i];
	return value == 0;
}

static void test_validator(void)
{
	struct capsule_tpm_anchor_grant grant = valid_grant();
	struct capsule_tpm_anchor_binding binding = valid_binding();

	CHECK(capsule_tpm_anchor_grant_validate(&grant, &binding) == CB_SUCCESS);
	CHECK(capsule_tpm_anchor_grant_validate(NULL, &binding) == CB_ERR_ARG);
	CHECK(capsule_tpm_anchor_grant_validate(&grant, NULL) == CB_ERR_ARG);

#define BAD_GRANT(field, value) do { \
	grant = valid_grant(); \
	grant.field = (value); \
	CHECK(capsule_tpm_anchor_grant_validate(&grant, &binding) == CB_ERR); \
} while (0)
	BAD_GRANT(revision, 0);
	BAD_GRANT(revision, 2);
	BAD_GRANT(size, sizeof(grant) - 1);
	BAD_GRANT(policy_revision, 2);
	BAD_GRANT(nv_index, binding.nv_index + 1);
	BAD_GRANT(generation, 0);
	BAD_GRANT(transaction, 0);
	BAD_GRANT(flags, CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS & ~BIT(0));
	BAD_GRANT(flags, CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS | BIT(31));
	BAD_GRANT(reserved, 1);
	BAD_GRANT(current.epoch, 0);
	BAD_GRANT(current.epoch, UINT64_MAX);
	BAD_GRANT(candidate.epoch, 4);
	BAD_GRANT(candidate.epoch, 6);
#undef BAD_GRANT
	grant = valid_grant();
	memset(grant.current.digest, 0, sizeof(grant.current.digest));
	CHECK(capsule_tpm_anchor_grant_validate(&grant, &binding) == CB_ERR);
	grant = valid_grant();
	memset(grant.candidate.digest, 0, sizeof(grant.candidate.digest));
	CHECK(capsule_tpm_anchor_grant_validate(&grant, &binding) == CB_ERR);
	grant = valid_grant();
	memcpy(grant.candidate.digest, grant.current.digest,
		sizeof(grant.current.digest));
	CHECK(capsule_tpm_anchor_grant_validate(&grant, &binding) == CB_ERR);

	grant = valid_grant();
	binding = valid_binding();
	binding.policy_ref_size = 0;
	CHECK(capsule_tpm_anchor_grant_validate(&grant, &binding) == CB_ERR);
	binding = valid_binding();
	memset(binding.authority_name, 0, sizeof(binding.authority_name));
	CHECK(capsule_tpm_anchor_grant_validate(&grant, &binding) == CB_ERR);
	binding = valid_binding();
	binding.reserved[2] = 1;
	CHECK(capsule_tpm_anchor_grant_validate(&grant, &binding) == CB_ERR);
	binding = valid_binding();
	binding.write_locked = 0;
	CHECK(capsule_tpm_anchor_grant_validate(&grant, &binding) == CB_ERR);
	binding = valid_binding();
	binding.authority_name[1]++;
	CHECK(capsule_tpm_anchor_grant_validate(&grant, &binding) == CB_ERR);
	binding = valid_binding();
	memset(binding.authority_name + 2, 0,
		sizeof(binding.authority_name) - 2);
	CHECK(capsule_tpm_anchor_grant_validate(&grant, &binding) == CB_ERR);
	binding = valid_binding();
	binding.policy_ref_size = CAPSULE_TPM_ANCHOR_POLICY_REF_MAX_SIZE + 1;
	CHECK(capsule_tpm_anchor_grant_validate(&grant, &binding) == CB_ERR);
	binding = valid_binding();
	binding.policy_ref[binding.policy_ref_size] = 1;
	CHECK(capsule_tpm_anchor_grant_validate(&grant, &binding) == CB_ERR);
}

static void install(struct capsule_tpm_anchor_grant *grant,
	struct capsule_tpm_anchor_binding *binding,
	struct protection_context *context)
{
	*context = (struct protection_context) {
		.grant = grant,
		.binding = binding,
		.protected = true,
	};
	CHECK(capsule_tpm_anchor_grant_install(grant, binding,
		protected_storage, context) == CB_SUCCESS);
	CHECK(capsule_tpm_anchor_grant_ready());
}

static void test_success(void)
{
	struct capsule_tpm_anchor_grant grant = valid_grant();
	struct capsule_tpm_anchor_binding binding = valid_binding();
	struct protection_context context;

	install(&grant, &binding, &context);
	CHECK(capsule_tpm_anchor_grant_consume(grant.generation,
		grant.transaction, &grant.current, &grant.candidate) == CB_SUCCESS);
	CHECK(grant_cleared(&context));
	CHECK(!capsule_tpm_anchor_grant_ready());
	CHECK(capsule_tpm_anchor_grant_consume(grant.generation,
		grant.transaction, &grant.current, &grant.candidate) == CB_ERR);
	CHECK(grant_cleared(&context));
}

static void test_mismatch(const char *name)
{
	struct capsule_tpm_anchor_grant grant = valid_grant();
	struct capsule_tpm_anchor_binding binding = valid_binding();
	struct capsule_tpm_anchor_value current;
	struct capsule_tpm_anchor_value candidate;
	struct protection_context context;
	uint64_t generation;
	uint64_t transaction;

	install(&grant, &binding, &context);
	current = grant.current;
	candidate = grant.candidate;
	generation = grant.generation;
	transaction = grant.transaction;
	if (!strcmp(name, "generation"))
		generation++;
	else if (!strcmp(name, "transaction"))
		transaction++;
	else if (!strcmp(name, "current"))
		current.digest[3] ^= 1;
	else if (!strcmp(name, "candidate"))
		candidate.digest[3] ^= 1;
	else if (!strcmp(name, "null-current")) {
		CHECK(capsule_tpm_anchor_grant_consume(generation, transaction,
			NULL, &candidate) == CB_ERR);
		goto consumed;
	} else if (!strcmp(name, "alias")) {
		CHECK(capsule_tpm_anchor_grant_consume(generation, transaction,
			&current, &current) == CB_ERR);
		goto consumed;
	} else if (!strcmp(name, "authority-overlap")) {
		CHECK(capsule_tpm_anchor_grant_consume(generation, transaction,
			context.authority, &candidate) == CB_ERR);
		goto consumed;
	} else {
		CHECK(false);
	}
	CHECK(capsule_tpm_anchor_grant_consume(generation, transaction,
		&current, &candidate) == CB_ERR);
consumed:
	CHECK(grant_cleared(&context));
	CHECK(!capsule_tpm_anchor_grant_ready());
	CHECK(capsule_tpm_anchor_grant_consume(grant.generation,
		grant.transaction, &grant.current, &grant.candidate) == CB_ERR);
	CHECK(grant_cleared(&context));
}

static void test_install_failure(const char *name)
{
	struct capsule_tpm_anchor_grant grant = valid_grant();
	struct capsule_tpm_anchor_grant retry = valid_grant();
	struct capsule_tpm_anchor_binding binding = valid_binding();
	struct protection_context context = {
		.grant = &grant,
		.binding = &binding,
		.protected = true,
	};

	if (!strcmp(name, "unprotected"))
		context.protected = false;
	else if (!strcmp(name, "mutate-grant"))
		context.mutate_grant = true;
	else if (!strcmp(name, "mutate-binding"))
		context.mutate_binding = true;
	else if (!strcmp(name, "mutate-authority"))
		context.mutate_authority = true;
	else if (!strcmp(name, "malformed"))
		grant.reserved = 1;
	else
		CHECK(false);
	CHECK(capsule_tpm_anchor_grant_install(&grant, &binding,
		protected_storage, &context) == CB_ERR);
	CHECK(!capsule_tpm_anchor_grant_ready());
	context.mutate_grant = false;
	context.mutate_binding = false;
	context.mutate_authority = false;
	context.protected = true;
	CHECK(capsule_tpm_anchor_grant_install(&retry, &binding,
		protected_storage, &context) == CB_ERR);
}

int main(int argc, char **argv)
{
	CHECK(argc == 2);
	if (!strcmp(argv[1], "validator"))
		test_validator();
	else if (!strcmp(argv[1], "success"))
		test_success();
	else if (!strncmp(argv[1], "mismatch-", 9))
		test_mismatch(argv[1] + 9);
	else if (!strncmp(argv[1], "install-", 8))
		test_install_failure(argv[1] + 8);
	else
		CHECK(false);
	return 0;
}
