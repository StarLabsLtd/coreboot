/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <payload_mm_fmp_auth_policy.h>
#include "payload_mm_fmp_owner_internal.h"

#define ROM_SIZE 4096U
#define AUTH_SIZE (9U * 1024U * 1024U)

static uint8_t rom[ROM_SIZE] __aligned(8);
static uint8_t auth_image[AUTH_SIZE] __aligned(8);
static size_t auth_used;
static uint8_t trust[PAYLOAD_MM_MAX_TRUST_XDR_SIZE];
static const uint8_t mock_trust[] = { 0, 0, 0, 1, 0x30, 0, 0, 0 };
static size_t trust_size;
static struct payload_mm_fmp_owner_record state;
static bool owner_ready = true;
static bool protected_ok = true;
static bool verify_ok = true;
static bool mutate_policy;
static bool mutate_authority;
static bool reenter;
static bool mutate_owner_read;
static unsigned int verifies;

static size_t read_file(const char *path, uint8_t *data, size_t capacity)
{
	ssize_t count;
	size_t total = 0;
	int descriptor = open(path, O_RDONLY);

	assert(descriptor >= 0);
	while (total < capacity &&
	       (count = read(descriptor, data + total, capacity - total)) > 0)
		total += count;
	assert(count == 0 && close(descriptor) == 0);
	return total;
}

static void write_file(const char *path, const uint8_t *data, size_t size)
{
	ssize_t count;
	size_t total = 0;
	int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

	assert(descriptor >= 0);
	while (total < size &&
	       (count = write(descriptor, data + total, size - total)) > 0)
		total += count;
	assert(total == size && close(descriptor) == 0);
}

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static void put_le16(uint8_t *data, uint16_t value)
{
	data[0] = value;
	data[1] = value >> 8;
}

static void put_le32(uint8_t *data, uint32_t value)
{
	data[0] = value;
	data[1] = value >> 8;
	data[2] = value >> 16;
	data[3] = value >> 24;
}

static void put_be32(uint8_t *data, uint32_t value)
{
	data[0] = value >> 24;
	data[1] = value >> 16;
	data[2] = value >> 8;
	data[3] = value;
}

static void area(uint8_t *data, uint32_t offset, uint32_t size,
	const char *name)
{
	put_le32(data, offset);
	put_le32(data + 4, size);
	memcpy(data + 8, name, strlen(name));
}

static void build_rom(const char *vendor, const char *part)
{
	static const char version[] = "COREBOOT_VERSION: test\n";
	uint8_t *file;
	char info[256];
	int info_size;

	memset(rom, 0xff, sizeof(rom));
	memset(rom, 0, 256);
	memcpy(rom, "__FMAP__", 8);
	rom[8] = 1;
	put_le32(rom + 18, sizeof(rom));
	memcpy(rom + 22, "TEST", 4);
	put_le16(rom + 54, 2);
	area(rom + 56, 0, 256, "FMAP");
	area(rom + 98, 256, 2048, "COREBOOT");
	file = rom + 256;
	memset(file, 0, 192);
	memcpy(file, "LARCHIVE", 8);
	info_size = snprintf(info, sizeof(info), "%sMAINBOARD_VENDOR: %s\n"
		"MAINBOARD_PART_NUMBER: %s\n", version, vendor, part);
	assert(info_size > 0 && (size_t)info_size < sizeof(info));
	put_be32(file + 8, info_size);
	put_be32(file + 12, 0x50);
	put_be32(file + 16, 0);
	put_be32(file + 20, 64);
	memcpy(file + 24, "build_info", sizeof("build_info"));
	memcpy(file + 64, info, info_size);
}

static void build_payload_with(const uint8_t *dependency,
	size_t dependency_size, uint32_t header_size, uint32_t version,
	uint32_t lowest_version)
{
	memset(auth_image, 0, sizeof(auth_image));
	assert(dependency_size + header_size + sizeof(rom) <= sizeof(auth_image));
	if (dependency_size)
		memcpy(auth_image, dependency, dependency_size);
	put_le32(auth_image + dependency_size, 0x3153534d);
	put_le32(auth_image + dependency_size + 4, header_size);
	put_le32(auth_image + dependency_size + 8, version);
	put_le32(auth_image + dependency_size + 12, lowest_version);
	memset(auth_image + dependency_size + 16, 0xa5,
		header_size >= 16 ? header_size - 16 : 0);
	memcpy(auth_image + dependency_size + header_size, rom, sizeof(rom));
	auth_used = dependency_size + header_size + sizeof(rom);
}

static void build_payload(bool with_dependency, uint32_t version,
	uint32_t lowest_version)
{
	static const uint8_t expression[] = {
		1, 2, 0, 0, 0, 1, 3, 0, 0, 0, 0x0a, 0x0d,
	};

	build_payload_with(with_dependency ? expression : NULL,
		with_dependency ? sizeof(expression) : 0, 16, version,
		lowest_version);
}

static void build_declared_dependency(uint32_t declared_length)
{
	uint8_t expression[] = { 0x0e, 0, 0, 0, 0, 0x06, 0x0d };

	put_le32(expression + 1, declared_length);
	build_payload_with(expression, sizeof(expression), 16, 3, 2);
}

static void build_guid_dependency(void)
{
	uint8_t expression[24] = { 1, 2, 0, 0, 0, 0 };

	expression[5] = 0;
	expression[6] = 1;
	expression[22] = 0x0a;
	expression[23] = 0x0d;
	build_payload_with(expression, sizeof(expression), 16, 3, 2);
}

bool payload_mm_fmp_owner_ready(void)
{
	return owner_ready;
}

bool payload_mm_fmp_owner_storage_overlaps(const void *buffer, size_t size)
{
	(void)buffer;
	(void)size;
	return false;
}

enum cb_err payload_mm_fmp_owner_read(uint32_t key,
	struct payload_mm_fmp_owner_record *record)
{
	assert(key == PAYLOAD_MM_FMP_STATE_KEY_STATE);
	*record = state;
	if (mutate_owner_read)
		record->data[4] ^= 1;
	return CB_SUCCESS;
}

bool payload_mm_authvar_buffers_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t l = (uintptr_t)left;
	uintptr_t r = (uintptr_t)right;

	if (!left_size || !right_size)
		return false;
	return l <= r + right_size - 1 && r <= l + left_size - 1;
}

#ifndef REAL_AUTH
enum payload_mm_verify_status payload_mm_authenticate_image(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *image,
	const struct payload_mm_crypto_span *trust_xdr,
	struct payload_mm_authenticated_image *authenticated)
{
	(void)owner;
	assert(image->data == auth_image && image->size == auth_used);
	assert(trust_xdr->size == sizeof(mock_trust) &&
		!memcmp(trust_xdr->data, mock_trust, sizeof(mock_trust)));
	verifies++;
	if (reenter) {
		struct capsule_broker_raw_image raw_image;

		assert(payload_mm_fmp_authenticate_provider(NULL, image->data,
			image->size, 3, &state, &raw_image) == CB_ERR);
	}
	if (!verify_ok)
		return PAYLOAD_MM_VERIFY_REJECTED;
	*authenticated = (struct payload_mm_authenticated_image) {
		.payload = { auth_image, auth_used },
	};
	return PAYLOAD_MM_VERIFY_OK;
}
#endif

static bool protected_storage(void *context, const void *storage, size_t size)
{
	struct payload_mm_fmp_auth_policy *policy = context;

	assert(storage != NULL && size != 0);
	if (mutate_policy) {
		policy->trusted_lowest_version++;
		trust[4] ^= 1;
	}
	if (mutate_authority)
		((uint8_t *)storage)[size - 1] ^= 1;
	return protected_ok;
}

static struct payload_mm_fmp_auth_policy policy(void)
{
	static const char vendor[] = "Star Labs";
	static const char part[] = "Lite";
	struct payload_mm_fmp_auth_policy value = {
		.revision = PAYLOAD_MM_FMP_AUTH_POLICY_REVISION,
		.size = sizeof(value),
		.trusted_lowest_version = 2,
		.image_size = sizeof(rom),
		.trust_xdr = trust,
		.trust_xdr_size = trust_size,
		.mainboard_vendor = vendor,
		.mainboard_vendor_size = sizeof(vendor) - 1,
		.mainboard_part = part,
		.mainboard_part_size = sizeof(part) - 1,
	};

	value.image_type.b[0] = 1;
	return value;
}

static void prepare(void)
{
	build_rom("Star Labs", "Lite");
	build_payload(false, 3, 2);
	trust_size = 8;
	memcpy(trust, mock_trust, trust_size);
	state = (struct payload_mm_fmp_owner_record) {
		.sequence = 1,
		.present = 1,
		.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
		.data_size = PAYLOAD_MM_FMP_STATE_WIRE_SIZE,
	};
	state.data[0] = 1;
	state.data[1] = 1;
	put_le32(state.data + 4, 3);
	put_le32(state.data + 8, 2);
}

int main(int argc, char **argv)
{
	struct payload_mm_fmp_auth_policy value;
	struct capsule_broker_raw_image raw_image;
	const char *test;

	if (argc == 3 && !strcmp(argv[1], "emit")) {
		prepare();
		write_file(argv[2], auth_image, auth_used);
		return 0;
	}
#ifndef REAL_AUTH
	if (argc == 5 && !strcmp(argv[1], "board")) {
		size_t rom_size;

		prepare();
		rom_size = read_file(argv[2], auth_image + 16,
			sizeof(auth_image) - 16);
		assert(rom_size != 0);
		put_le32(auth_image, 0x3153534d);
		put_le32(auth_image + 4, 16);
		put_le32(auth_image + 8, 3);
		put_le32(auth_image + 12, 2);
		auth_used = rom_size + 16;
		value = policy();
		value.image_size = rom_size;
		value.mainboard_vendor = argv[3];
		value.mainboard_vendor_size = strlen(argv[3]);
		value.mainboard_part = argv[4];
		value.mainboard_part_size = strlen(argv[4]);
		assert(payload_mm_fmp_auth_policy_install(&value,
			protected_storage, &value) == CB_SUCCESS);
		assert(payload_mm_fmp_authenticate_provider(NULL, auth_image,
			auth_used, 3, &state, &raw_image) == CB_SUCCESS);
		assert(raw_image.size == rom_size &&
			raw_image.offset + raw_image.size == auth_used);
		return 0;
	}
#endif
#ifdef REAL_AUTH
	if (argc == 4 && !strcmp(argv[1], "real")) {
		prepare();
		auth_used = read_file(argv[2], auth_image, sizeof(auth_image));
		trust_size = read_file(argv[3], trust, sizeof(trust));
		assert(auth_used != 0 && trust_size != 0);
		value = policy();
		assert(payload_mm_fmp_auth_policy_install(&value,
			protected_storage, &value) == CB_SUCCESS);
		assert(payload_mm_fmp_authenticate_provider(NULL, auth_image,
			auth_used, 3, &state, &raw_image) == CB_SUCCESS);
		assert(raw_image.size == sizeof(rom) && raw_image.offset < auth_used &&
			raw_image.size <= auth_used - raw_image.offset);
		return 0;
	}
#endif
	assert(argc == 2);
	test = argv[1];
	prepare();
	value = policy();
	if (!strcmp(test, "install-owner"))
		owner_ready = false;
	else if (!strcmp(test, "install-protection"))
		protected_ok = false;
	else if (!strcmp(test, "install-mutation"))
		mutate_policy = true;
	else if (!strcmp(test, "install-authority-mutation"))
		mutate_authority = true;
	else if (!strcmp(test, "install-xdr"))
		value.trust_xdr_size--;
	else if (!strcmp(test, "install-source"))
		value.mainboard_vendor_size = 0;
	else if (!strcmp(test, "install-image-size"))
		value.image_size = PAYLOAD_MM_FMP_MAX_ROM_SIZE + 1;
	assert(payload_mm_fmp_auth_policy_install(&value, protected_storage,
		&value) == ((!owner_ready || !protected_ok ||
		mutate_policy || mutate_authority || !strcmp(test, "install-xdr") ||
		!strcmp(test, "install-source") ||
		!strcmp(test, "install-image-size")) ?
		CB_ERR : CB_SUCCESS));
	if (!owner_ready || !protected_ok || mutate_policy || mutate_authority ||
	    !strcmp(test, "install-xdr") || !strcmp(test, "install-source") ||
	    !strcmp(test, "install-image-size"))
		return 0;
	/* Installation is one-shot and copied all caller-owned input. */
	assert(payload_mm_fmp_auth_policy_install(&value, protected_storage,
		&value) == CB_ERR);
	if (!strcmp(test, "version"))
		build_payload(false, 4, 2);
	else if (!strcmp(test, "payload-floor"))
		build_payload(false, 3, 4);
	else if (!strcmp(test, "floor")) {
		put_le32(state.data + 8, 4);
	} else if (!strcmp(test, "missing-version")) {
		state.data[0] = 0;
	} else if (!strcmp(test, "dependency"))
		build_payload(true, 3, 2);
	else if (!strcmp(test, "dependency-declared"))
		build_declared_dependency(7);
	else if (!strcmp(test, "dependency-guid"))
		build_guid_dependency();
	else if (!strcmp(test, "dependency-declared-mismatch"))
		build_declared_dependency(8);
	else if (!strcmp(test, "dependency-declared-truncated")) {
		static const uint8_t expression[] = { 0x0e, 7, 0, 0 };

		build_payload_with(expression, sizeof(expression), 16, 3, 2);
	} else if (!strcmp(test, "dependency-guid-truncated")) {
		static const uint8_t expression[] = { 0, 1, 0, 0, 0, 0, 0, 0 };

		build_payload_with(expression, sizeof(expression), 16, 3, 2);
	} else if (!strcmp(test, "dependency-trailing")) {
		static const uint8_t expression[] = { 0x06, 0x0d, 0xff };

		build_payload_with(expression, sizeof(expression), 16, 3, 2);
	} else if (!strcmp(test, "dependency-false")) {
		build_payload(true, 3, 2);
		auth_image[1] = 4;
	} else if (!strcmp(test, "foreign-board")) {
		build_rom("Other", "Lite");
		build_payload(false, 3, 2);
	} else if (!strcmp(test, "duplicate-fmap")) {
		memcpy(rom + 2304, rom, 140);
		put_le32(rom + 2304 + 56, 2304);
		build_payload(false, 3, 2);
	} else if (!strcmp(test, "duplicate-build-info")) {
		memcpy(rom + 256 + 192, rom + 256, 128);
		build_payload(false, 3, 2);
	} else if (!strcmp(test, "verify-failure"))
		verify_ok = false;
	else if (!strcmp(test, "header-extension"))
		build_payload_with(NULL, 0, 32, 3, 2);
	else if (!strcmp(test, "header-extension-dependency")) {
		static const uint8_t expression[] = { 0x06, 0x0d };

		build_payload_with(expression, sizeof(expression), 32, 3, 2);
	} else if (!strcmp(test, "header-small"))
		build_payload_with(NULL, 0, 15, 3, 2);
	else if (!strcmp(test, "header-overflow")) {
		build_payload(false, 3, 2);
		put_le32(auth_image + 4, UINT32_MAX);
	} else if (!strcmp(test, "header-no-body")) {
		build_payload(false, 3, 2);
		put_le32(auth_image + 4, auth_used);
	} else if (!strcmp(test, "reentry"))
		reenter = true;
	else if (!strcmp(test, "source-copy")) {
		trust[4] ^= 1;
		value.trusted_lowest_version = UINT32_MAX;
	}
	if (!strcmp(test, "owner-same-sequence")) {
		struct payload_mm_fmp_owner_record expected = state;

		state.data[4] ^= 1;
		assert(payload_mm_fmp_authenticate_provider(NULL, auth_image,
			auth_used, 3, &expected, &raw_image) == CB_ERR);
		assert(raw_image.offset == 0 && raw_image.size == 0);
		assert(state.sequence == expected.sequence);
		assert(memcmp(&state, &expected, sizeof(state)) != 0);
		return 0;
	}
	if (!strcmp(test, "owner-aba")) {
		struct payload_mm_fmp_owner_record expected = state;

		mutate_owner_read = true;
		assert(payload_mm_fmp_authenticate_provider(NULL, auth_image,
			auth_used, 3, &expected, &raw_image) == CB_ERR);
		assert(raw_image.offset == 0 && raw_image.size == 0);
		assert(!memcmp(&state, &expected, sizeof(state)));
		return 0;
	}
	if (!strcmp(test, "success") || !strcmp(test, "dependency") ||
	    !strcmp(test, "dependency-declared") ||
	    !strcmp(test, "dependency-guid") ||
	    !strcmp(test, "header-extension") ||
	    !strcmp(test, "header-extension-dependency") ||
	    !strcmp(test, "reentry") || !strcmp(test, "source-copy")) {
		assert(payload_mm_fmp_authenticate_provider(NULL, auth_image,
			auth_used, 3, &state, &raw_image) == CB_SUCCESS);
		assert(raw_image.size == sizeof(rom) && raw_image.offset < auth_used &&
			raw_image.size <= auth_used - raw_image.offset);
	} else {
		assert(payload_mm_fmp_authenticate_provider(NULL, auth_image,
			auth_used, 3, &state, &raw_image) == CB_ERR);
		assert(raw_image.offset == 0 && raw_image.size == 0);
	}
	if (!strcmp(test, "floor"))
		assert(verifies == 0);
	return 0;
}
