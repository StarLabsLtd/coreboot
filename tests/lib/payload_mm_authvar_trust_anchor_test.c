/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_cms.h"
#include "crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct buffer {
	uint8_t *data;
	size_t size;
};

static int failures;

static void expect(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}

static struct buffer read_file(const char *directory, const char *name)
{
	struct buffer result = { 0 };
	char path[4096];
	long length;
	FILE *file = NULL;

	if (snprintf(path, sizeof(path), "%s/%s", directory, name) < 0)
		return result;
	file = fopen(path, "rb");
	if (!file || fseek(file, 0L, SEEK_END) || (length = ftell(file)) <= 0L ||
	    fseek(file, 0L, SEEK_SET))
		goto out;
	result.data = malloc((size_t)length);
	if (!result.data)
		goto out;
	if (fread(result.data, 1, (size_t)length, file) != (size_t)length) {
		free(result.data);
		result.data = NULL;
		goto out;
	}
	result.size = (size_t)length;
out:
	if (file)
		fclose(file);
	return result;
}

static bool arena_is_zero(const struct payload_mm_crypto_owner *owner)
{
	for (size_t index = 0U; index < sizeof(owner->arena); index++)
		if (owner->arena[index])
			return false;
	return true;
}

static enum payload_mm_verify_status cms_verify(
	struct payload_mm_crypto_owner *owner, const struct buffer *cms,
	const struct buffer *content, struct payload_mm_cms_verified_signer *verified)
{
	const struct payload_mm_crypto_span signed_data = { cms->data, cms->size };
	const struct payload_mm_crypto_span content_span = {
		content->data, content->size,
	};

	return payload_mm_cms_verify_detached_untrusted(owner, &signed_data,
		&content_span, 1U, verified);
}

static enum payload_mm_verify_status trust_verify(
	struct payload_mm_crypto_owner *owner, const struct buffer *cms,
	const struct payload_mm_cms_verified_signer *verified,
	const struct buffer *anchor)
{
	const struct payload_mm_crypto_span signed_data = { cms->data, cms->size };
	const struct payload_mm_crypto_span trust_anchor = {
		anchor->data, anchor->size,
	};

	return payload_mm_authvar_trust_anchor_verify(owner, &signed_data,
		verified, &trust_anchor);
}

static void check_chain(const struct buffer *cms, const struct buffer *content,
	const struct buffer *anchor, size_t certificate_count, const char *label)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_cms_verified_signer verified;
	enum payload_mm_verify_status status;

	memset(&owner, 0, sizeof(owner));
	expect(cms_verify(&owner, cms, content, &verified) ==
		PAYLOAD_MM_VERIFY_OK, "detached CMS setup failed");
	expect(verified.certificate_count == certificate_count,
		"unexpected CMS certificate count");
	memset(&owner, 0, sizeof(owner));
	status = trust_verify(&owner, cms, &verified, anchor);
	if (status != PAYLOAD_MM_VERIFY_OK)
		fprintf(stderr, "FAIL: %s chain status %u\n", label, status);
	expect(status == PAYLOAD_MM_VERIFY_OK, "valid signer chain rejected");
	expect(!owner.busy && owner.arena_used == 0U && arena_is_zero(&owner),
		"successful trust verification retained protected state");
}

static void check_failures(const struct buffer *cms, const struct buffer *content,
	const struct buffer *anchor, const struct buffer *wrong_anchor)
{
	static struct payload_mm_crypto_owner owner;
	static struct payload_mm_crypto_owner blocker;
	struct payload_mm_cms_verified_signer verified;
	struct payload_mm_cms_verified_signer changed;
	struct payload_mm_crypto_span signed_data = { cms->data, cms->size };
	struct payload_mm_crypto_span anchor_span = { anchor->data, anchor->size };
	const uint8_t malformed_der[] = { 0x30U, 0x00U };
	const struct buffer malformed_anchor = {
		(uint8_t *)(uintptr_t)malformed_der, sizeof(malformed_der),
	};
	size_t allocation_count;

	memset(&owner, 0, sizeof(owner));
	expect(cms_verify(&owner, cms, content, &verified) ==
		PAYLOAD_MM_VERIFY_OK, "could not prepare verified CMS views");
	memset(&owner, 0, sizeof(owner));
	expect(trust_verify(&owner, cms, &verified, wrong_anchor) ==
		PAYLOAD_MM_VERIFY_REJECTED, "unrelated trust anchor accepted");
	expect(!owner.busy && arena_is_zero(&owner),
		"wrong-anchor failure retained protected state");

	changed = verified;
	changed.signer_certificate.data++;
	memset(&owner, 0, sizeof(owner));
	expect(trust_verify(&owner, cms, &changed, anchor) ==
		PAYLOAD_MM_VERIFY_INVALID, "non-member signer accepted");
	changed = verified;
	changed.certificates[1] = changed.certificates[0];
	changed.certificate_count = 2U;
	expect(trust_verify(&owner, cms, &changed, anchor) ==
		PAYLOAD_MM_VERIFY_INVALID, "duplicate certificate view accepted");
	changed = verified;
	changed.certificates[0].data = anchor->data;
	expect(trust_verify(&owner, cms, &changed, anchor) ==
		PAYLOAD_MM_VERIFY_INVALID, "out-of-CMS certificate view accepted");
	changed = verified;
	changed.certificates[1].data = changed.certificates[0].data + 1U;
	changed.certificates[1].size = changed.certificates[0].size - 1U;
	changed.certificate_count = 2U;
	expect(trust_verify(&owner, cms, &changed, anchor) ==
		PAYLOAD_MM_VERIFY_INVALID, "partially overlapping views accepted");
	changed = verified;
	changed.certificate_count = 0U;
	expect(trust_verify(&owner, cms, &changed, anchor) ==
		PAYLOAD_MM_VERIFY_INVALID, "empty certificate set accepted");
	changed.certificate_count = PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES + 1U;
	expect(trust_verify(&owner, cms, &changed, anchor) ==
		PAYLOAD_MM_VERIFY_INVALID, "oversized certificate set accepted");
	anchor_span = verified.signer_certificate;
	expect(payload_mm_authvar_trust_anchor_verify(&owner, &signed_data,
		&verified, &anchor_span) == PAYLOAD_MM_VERIFY_INVALID,
		"anchor aliasing CMS bytes accepted");
	expect(payload_mm_authvar_trust_anchor_verify(&owner, &signed_data,
		&verified, &signed_data) == PAYLOAD_MM_VERIFY_INVALID,
		"aliased CMS and anchor descriptors accepted");
	memset(&owner, 0, sizeof(owner));
	expect(trust_verify(&owner, cms, &verified, &malformed_anchor) ==
		PAYLOAD_MM_VERIFY_MALFORMED, "malformed trust anchor misclassified");

	memset(&owner, 0, sizeof(owner));
	memset(&blocker, 0, sizeof(blocker));
	expect(payload_mm_crypto_begin(&blocker) == PAYLOAD_MM_VERIFY_OK,
		"could not acquire crypto blocker");
	expect(trust_verify(&owner, cms, &verified, anchor) ==
		PAYLOAD_MM_VERIFY_BUSY, "nested trust verification was not busy");
	expect(payload_mm_crypto_end(&blocker, PAYLOAD_MM_VERIFY_OK) ==
		PAYLOAD_MM_VERIFY_OK, "crypto blocker did not unwind");

	memset(&owner, 0, sizeof(owner));
	expect(trust_verify(&owner, cms, &verified, anchor) ==
		PAYLOAD_MM_VERIFY_OK, "allocation-count baseline failed");
	allocation_count = owner.allocation_count;
	expect(allocation_count != 0U, "trust verification made no allocations");
	for (size_t fail_at = 1U; fail_at <= allocation_count; fail_at++) {
		memset(&owner, 0, sizeof(owner));
		owner.fail_allocation = fail_at;
		expect(trust_verify(&owner, cms, &verified, anchor) ==
			PAYLOAD_MM_VERIFY_NO_MEMORY,
			"injected allocation failure was not reported");
		expect(!owner.busy && owner.arena_used == 0U && arena_is_zero(&owner),
			"allocation failure retained protected state");
	}
}

static void check_descriptor_in_cms(const struct buffer *certificate,
	const struct buffer *anchor)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_crypto_span signed_data;
	struct payload_mm_crypto_span anchor_span = { anchor->data, anchor->size };
	struct payload_mm_cms_verified_signer *inside;
	size_t total = sizeof(*inside) + certificate->size;
	uint8_t *storage = malloc(total);

	expect(storage != NULL, "could not allocate descriptor-alias fixture");
	if (!storage)
		return;
	inside = (void *)storage;
	memset(inside, 0, sizeof(*inside));
	memcpy(storage + sizeof(*inside), certificate->data, certificate->size);
	inside->certificate_count = 1U;
	inside->certificates[0] = (struct payload_mm_crypto_span) {
		storage + sizeof(*inside), certificate->size,
	};
	inside->signer_certificate = inside->certificates[0];
	signed_data = (struct payload_mm_crypto_span) { storage, total };
	memset(&owner, 0, sizeof(owner));
	expect(payload_mm_authvar_trust_anchor_verify(&owner, &signed_data,
		inside, &anchor_span) == PAYLOAD_MM_VERIFY_INVALID,
		"verified descriptor sourced from CMS bytes accepted");
	free(storage);
}

static void check_synthetic_set(const struct buffer *signer,
	const struct buffer *extra, const struct buffer *anchor, size_t count,
	const char *message)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_cms_verified_signer verified = { 0 };
	struct payload_mm_crypto_span signed_data;
	struct payload_mm_crypto_span anchor_span = { anchor->data, anchor->size };
	size_t total = signer->size * count + (extra ? extra->size : 0U);
	uint8_t *cms = malloc(total);

	expect(cms != NULL, "could not allocate synthetic CMS container");
	if (!cms)
		return;
	for (size_t index = 0U; index < count; index++) {
		memcpy(cms + index * signer->size, signer->data, signer->size);
		verified.certificates[index] = (struct payload_mm_crypto_span) {
			cms + index * signer->size, signer->size,
		};
	}
	verified.certificate_count = count;
	verified.signer_certificate = verified.certificates[0];
	if (extra) {
		memcpy(cms + signer->size * count, extra->data, extra->size);
		verified.certificates[count] = (struct payload_mm_crypto_span) {
			cms + signer->size * count, extra->size,
		};
		verified.certificate_count++;
	}
	signed_data = (struct payload_mm_crypto_span) { cms, total };
	memset(&owner, 0, sizeof(owner));
	expect(payload_mm_authvar_trust_anchor_verify(&owner, &signed_data,
		&verified, &anchor_span) == PAYLOAD_MM_VERIFY_OK, message);
	expect(!owner.busy && arena_is_zero(&owner),
		"synthetic set retained protected state");
	free(cms);
}

int main(int argc, char **argv)
{
	struct buffer content;
	struct buffer direct_cms;
	struct buffer root;
	struct buffer wrong_root;
	struct buffer intermediate_cms;
	struct buffer maximum_cms;
	struct buffer direct_certificate;
	struct buffer unrelated_certificate;

	if (argc != 2)
		return 2;
	content = read_file(argv[1], "content.bin");
	direct_cms = read_file(argv[1], "direct.der");
	root = read_file(argv[1], "root.der");
	wrong_root = read_file(argv[1], "wrong-root.der");
	intermediate_cms = read_file(argv[1], "intermediate.der");
	maximum_cms = read_file(argv[1], "maximum.der");
	direct_certificate = read_file(argv[1], "direct-cert.der");
	unrelated_certificate = read_file(argv[1], "unrelated.der");
	expect(content.data && direct_cms.data && root.data && wrong_root.data &&
		intermediate_cms.data && maximum_cms.data && direct_certificate.data &&
		unrelated_certificate.data,
		"could not read generated fixtures");
	if (failures)
		return 1;

	check_chain(&direct_cms, &content, &root, 1U, "direct");
	check_chain(&intermediate_cms, &content, &root, 2U, "intermediate");
	check_chain(&maximum_cms, &content, &root,
		PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES, "maximum");
	check_synthetic_set(&direct_certificate, NULL, &root,
		PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES,
		"eight CMS certificates plus anchor were rejected");
	check_synthetic_set(&direct_certificate, &unrelated_certificate, &root,
		1U, "unrelated embedded certificate broke a valid chain");
	check_descriptor_in_cms(&direct_certificate, &root);
	check_failures(&direct_cms, &content, &root, &wrong_root);

	free(unrelated_certificate.data);
	free(direct_certificate.data);
	free(maximum_cms.data);
	free(intermediate_cms.data);
	free(wrong_root.data);
	free(root.data);
	free(direct_cms.data);
	free(content.data);
	return failures != 0;
}
