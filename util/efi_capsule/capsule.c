/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/bsd/sysincludes.h>
#include <commonlib/bsd/compiler.h>
#include <commonlib/bsd/fmap_serialized.h>
#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/x509v3.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct capsule_header {
	uint8_t guid[16];
	uint32_t header_size, flags, image_size, reserved;
} __packed;

struct fmp_header {
	uint32_t version;
	uint16_t driver_count, image_count;
	uint64_t image_offset;
} __packed;

struct image_header {
	uint32_t version;
	uint8_t guid[16], index, reserved[3];
	uint32_t image_size, vendor_size;
	uint64_t hardware_instance, support;
} __packed;

struct authentication_header {
	uint64_t monotonic_count;
	uint32_t certificate_size;
	uint16_t revision, type;
	uint8_t guid[16];
} __packed;

struct capsule_prefix {
	struct capsule_header capsule;
	struct fmp_header fmp;
	struct image_header image;
	struct authentication_header authentication;
} __packed;

struct payload_header {
	uint8_t signature[4];
	uint32_t header_size, version, lowest_version;
} __packed;

struct region_manifest {
	uint8_t signature[4];
	uint16_t version, count;
} __packed;

_Static_assert(sizeof(struct capsule_prefix) == 128, "capsule header layout");
_Static_assert(sizeof(struct payload_header) == 16, "payload header layout");
_Static_assert(sizeof(struct region_manifest) == 8, "region manifest layout");

struct options {
	const char *rom, *output, *guid, *version, *lowest_version, *localversion;
	const char *regions, *signer, *intermediate, *root;
	bool initiate_reset;
};

static bool guid_bytes(const char *text, uint8_t out[16])
{
	uint8_t guid[16];
	size_t byte = 0;

	if (strlen(text) != 36)
		return false;
	for (size_t i = 0; i < 36;) {
		char digits[3] = { 0 };

		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (text[i++] != '-')
				return false;
			continue;
		}
		if (!isxdigit((unsigned char)text[i]) ||
		    !isxdigit((unsigned char)text[i + 1]))
			return false;
		digits[0] = text[i++];
		digits[1] = text[i++];
		guid[byte++] = strtoul(digits, NULL, 16);
	}
	out[0] = guid[3];
	out[1] = guid[2];
	out[2] = guid[1];
	out[3] = guid[0];
	out[4] = guid[5];
	out[5] = guid[4];
	out[6] = guid[7];
	out[7] = guid[6];
	memcpy(out + 8, guid + 8, 8);
	return true;
}

static bool number(const char *text, uint32_t *value)
{
	char *end;
	unsigned long long parsed;

	if (!text || !*text || *text == '-' || *text == '+')
		return false;
	errno = 0;
	parsed = strtoull(text, &end, 0);
	if (errno || *end || parsed > UINT32_MAX)
		return false;
	*value = parsed;
	return true;
}

static bool versions(const struct options *options, uint32_t *version, uint32_t *lowest)
{
	const char *text = options->localversion;
	char *end;
	unsigned long major, minor;

	if (!number(options->version, version) || !number(options->lowest_version, lowest))
		return false;
	if (!*version) {
		if (!text)
			return false;
		while (*text && (*text < '0' || *text > '9'))
			text++;
		errno = 0;
		major = strtoul(text, &end, 10);
		if (errno || end == text || *end != '.' || major > UINT16_MAX)
			return false;
		text = end + 1;
		if (*text < '0' || *text > '9')
			return false;
		errno = 0;
		minor = strtoul(text, &end, 10);
		if (errno || end == text || minor > UINT16_MAX)
			return false;
		*version = major << 16 | minor;
	}
	if (!*lowest)
		*lowest = *version;
	return *lowest <= *version;
}

static uint8_t *read_rom(const char *path, size_t *size)
{
	struct stat st;
	uint8_t *data = NULL;
	FILE *file = fopen(path, "rb");

	if (!file)
		return NULL;
	if (fstat(fileno(file), &st) || !S_ISREG(st.st_mode) ||
	    st.st_size < (off_t)sizeof(struct fmap) || st.st_size > INT_MAX - 2 * 1024 * 1024)
		goto out;
	*size = st.st_size;
	data = malloc(*size);
	if (data && fread(data, 1, *size, file) != *size) {
		free(data);
		data = NULL;
	}
out:
	fclose(file);
	return data;
}

static const struct fmap *find_fmap(const uint8_t *rom, size_t size)
{
	const struct fmap *found = NULL;

	for (size_t offset = 0; offset <= size - sizeof(struct fmap); offset++) {
		const struct fmap *map = (const void *)(rom + offset);

		if (memcmp(map->signature, FMAP_SIGNATURE, sizeof(map->signature)) ||
		    map->ver_major != FMAP_VER_MAJOR || le32toh(map->size) != size ||
		    le16toh(map->nareas) > (size - offset - sizeof(*map)) / sizeof(map->areas[0]))
			continue;
		if (found)
			return NULL;
		found = map;
	}
	return found;
}

static bool vboot_nvram_reset(const uint8_t *rom, size_t rom_size,
	const struct fmap *map, const struct fmap_area *nvram)
{
	uint64_t offset = le32toh(nvram->offset);
	uint64_t end = offset + le32toh(nvram->size);
	bool found_wp_ro = false;

	if (strcmp((const char *)nvram->name, "RW_NVRAM"))
		return false;
	for (size_t i = 0; i < le16toh(map->nareas); i++) {
		const struct fmap_area *area = &map->areas[i];
		uint64_t start = le32toh(area->offset);
		uint64_t size = le32toh(area->size);
		bool wp_ro;

		if (area == nvram)
			continue;
		if (!memchr(area->name, 0, sizeof(area->name)))
			return false;
		wp_ro = !strcmp((const char *)area->name, "WP_RO");
		if (wp_ro) {
			if (found_wp_ro)
				return false;
			found_wp_ro = true;
		}
		if (!wp_ro && !(le16toh(area->flags) & FMAP_AREA_PRESERVE) &&
		    strcmp((const char *)area->name, "FMAP") &&
		    strcmp((const char *)area->name, "SMMSTORE") &&
		    strcmp((const char *)area->name, "SRK_PIN_V1"))
			continue;
		if (!size || start > rom_size || size > rom_size - start ||
		    (offset < start + size && start < end))
			return false;
	}
	/* The vboot release policy resets NVRAM; it never installs saved state. */
	for (size_t i = offset; i < end; i++)
		if (rom[i] != 0xff)
			return false;
	return found_wp_ro;
}

static uint8_t *build_payload(const uint8_t *rom, size_t rom_size,
	const struct fmap *map, const struct options *options, size_t *payload_size)
{
	char (*names)[16] = NULL;
	char *regions = strdup(options->regions), *save, *name;
	uint8_t *payload = NULL;
	struct payload_header header = { .signature = { 'M', 'S', 'S', '1' } };
	struct region_manifest manifest = { .signature = { 'R', 'M', 'A', 'P' } };
	uint32_t version, lowest;
	size_t count = 0, capacity = strlen(options->regions) + 1;

	if (!regions || !versions(options, &version, &lowest))
		goto out;
	/* Each nonempty name consumes at least one byte and one delimiter. */
	names = calloc(capacity, sizeof(*names));
	if (!names)
		goto out;
	for (name = strtok_r(regions, " \t", &save); name; name = strtok_r(NULL, " \t", &save)) {
		const struct fmap_area *area = NULL;
		size_t length = strlen(name);

		if (!length || length >= sizeof(*names) || count == UINT16_MAX)
			goto out;
		for (size_t i = 0; i < length; i++)
			if ((unsigned char)name[i] <= 0x20 || (unsigned char)name[i] > 0x7e)
				goto out;
		for (size_t i = 0; i < count; i++)
			if (!strncmp(names[i], name, sizeof(*names)))
				goto out;
		for (size_t i = 0; i < le16toh(map->nareas); i++) {
			const struct fmap_area *candidate = &map->areas[i];

			if (memchr(candidate->name, 0, sizeof(candidate->name)) &&
			    !strcmp((const char *)candidate->name, name)) {
				if (area)
					goto out;
				area = candidate;
			}
		}
		if (!area || !le32toh(area->size) || le32toh(area->offset) > rom_size ||
		    le32toh(area->size) > rom_size - le32toh(area->offset) ||
		    ((le16toh(area->flags) & FMAP_AREA_PRESERVE) &&
		     !vboot_nvram_reset(rom, rom_size, map, area))) {
			fprintf(stderr, "Invalid or preserved capsule region: %s\n", name);
			goto out;
		}
		memcpy(names[count++], name, length);
	}
	if (!count)
		goto out;
	*payload_size = sizeof(header) + rom_size + count * sizeof(*names) + sizeof(manifest);
	if (*payload_size > INT_MAX - sizeof(uint64_t))
		goto out;
	/* UEFI signs the payload followed by the little-endian monotonic count.
	 * That count is carried in the authentication header, not the payload. */
	payload = calloc(1, *payload_size + sizeof(uint64_t));
	if (!payload)
		goto out;
	header.header_size = htole32(sizeof(header));
	header.version = htole32(version);
	header.lowest_version = htole32(lowest);
	manifest.version = htole16(1);
	manifest.count = htole16(count);
	memcpy(payload, &header, sizeof(header));
	memcpy(payload + sizeof(header), rom, rom_size);
	memcpy(payload + sizeof(header) + rom_size, names, count * sizeof(*names));
	memcpy(payload + *payload_size - sizeof(manifest), &manifest, sizeof(manifest));
out:
	free(names);
	free(regions);
	return payload;
}

static X509 *read_certificate(const char *path)
{
	BIO *input = BIO_new_file(path, "r");
	X509 *certificate = input ? PEM_read_bio_X509(input, NULL, NULL, NULL) : NULL;

	BIO_free(input);
	return certificate;
}

static bool supported_certificate(X509 *certificate, bool root)
{
	EVP_PKEY *key = X509_get0_pubkey(certificate);

	/* Match the native capsule verifier's RSA/SHA-256 profile. */
	if (!key || EVP_PKEY_base_id(key) != EVP_PKEY_RSA ||
	    EVP_PKEY_bits(key) < 2048 || EVP_PKEY_bits(key) > 4096 ||
	    X509_get_signature_nid(certificate) != NID_sha256WithRSAEncryption)
		return false;
	for (int i = 0; i < X509_get_ext_count(certificate); i++) {
		X509_EXTENSION *extension = X509_get_ext(certificate, i);
		int nid = OBJ_obj2nid(X509_EXTENSION_get_object(extension));

		if (!X509_EXTENSION_get_critical(extension))
			continue;
		if (nid == NID_basic_constraints || nid == NID_key_usage ||
		    nid == NID_ext_key_usage)
			continue;
		/* CDK2 checks the anchor itself; BearSSL checks the other certificates. */
		if (root)
			return false;
		switch (nid) {
		case NID_subject_alt_name:
		case NID_authority_key_identifier:
		case NID_subject_key_identifier:
		case NID_issuer_alt_name:
		case NID_subject_directory_attributes:
		case NID_crl_distribution_points:
		case NID_freshest_crl:
		case NID_info_access:
		case NID_sinfo_access:
			break;
		case NID_certificate_policies: {
			CERTIFICATEPOLICIES *policies = X509V3_EXT_d2i(extension);
			bool supported = policies != NULL;

			for (int p = 0; p < sk_POLICYINFO_num(policies); p++) {
				POLICYINFO *policy = sk_POLICYINFO_value(policies, p);

				for (int q = 0; q < sk_POLICYQUALINFO_num(policy->qualifiers); q++)
					if (OBJ_obj2nid(sk_POLICYQUALINFO_value(
					    policy->qualifiers, q)->pqualid) != NID_id_qt_cps)
						supported = false;
			}
			CERTIFICATEPOLICIES_free(policies);
			if (!supported)
				return false;
			break;
		}
		default:
			return false;
		}
	}
	return true;
}

static PKCS7 *sign_payload(const uint8_t *payload, size_t size, const struct options *options)
{
	X509 *signer = read_certificate(options->signer);
	X509 *intermediate = read_certificate(options->intermediate);
	X509 *root = read_certificate(options->root);
	X509_STORE *store = X509_STORE_new();
	STACK_OF(X509) *chain = sk_X509_new_null();
	BIO *key_file = BIO_new_file(options->signer, "r");
	EVP_PKEY *key = key_file ? PEM_read_bio_PrivateKey(key_file, NULL, NULL, NULL) : NULL;
	BIO *data = BIO_new_mem_buf(payload, size + sizeof(uint64_t));
	PKCS7 *signature = NULL;
	int flags = PKCS7_BINARY | PKCS7_DETACHED | PKCS7_NOATTR;

	if (!signer || !intermediate || !root || !store || !chain || !key || !data)
		goto out;
	if (!supported_certificate(signer, false) ||
	    !supported_certificate(intermediate, false) || !supported_certificate(root, true)) {
		fprintf(stderr, "Capsule certificates are not supported by the native verifier\n");
		goto out;
	}
	if (X509_check_ca(root) != 1) {
		fprintf(stderr, "The configured trust certificate is not a CA\n");
		goto out;
	}
	if (!sk_X509_push(chain, intermediate) || !sk_X509_push(chain, root) ||
	    !X509_STORE_add_cert(store, root) || !X509_STORE_set_purpose(store, X509_PURPOSE_ANY) ||
	    !X509_STORE_set_flags(store, X509_V_FLAG_PARTIAL_CHAIN))
		goto out;
	signature = PKCS7_sign(NULL, NULL, chain, NULL, flags | PKCS7_PARTIAL);
	if (!signature || !PKCS7_sign_add_signer(signature, signer, key, EVP_sha256(), flags) ||
	    !PKCS7_final(signature, data, flags) || BIO_reset(data) != 1 ||
	    PKCS7_verify(signature, NULL, store, data, NULL, PKCS7_BINARY) != 1) {
		PKCS7_free(signature);
		signature = NULL;
	}
out:
	BIO_free(data);
	BIO_free(key_file);
	EVP_PKEY_free(key);
	sk_X509_free(chain);
	X509_STORE_free(store);
	X509_free(root);
	X509_free(intermediate);
	X509_free(signer);
	return signature;
}

static bool write_capsule(const struct options *options, const uint8_t *payload,
	size_t payload_size, PKCS7 *signature)
{
	struct capsule_prefix prefix = { 0 };
	unsigned char *der = NULL;
	int der_size = i2d_PKCS7(signature, &der);
	FILE *file;
	bool success;
	size_t image_size, total;

	if (der_size <= 0)
		return false;
	image_size = sizeof(prefix.authentication) + der_size + payload_size;
	total = sizeof(prefix) + der_size + payload_size;
	if (total > UINT32_MAX || !guid_bytes(options->guid, prefix.image.guid)) {
		OPENSSL_free(der);
		return false;
	}
	guid_bytes("6dcbd5ed-e82d-4c44-bda1-7194199ad92a", prefix.capsule.guid);
	guid_bytes("4aafd29d-68df-49ee-8aa9-347d375665a7", prefix.authentication.guid);
	prefix.capsule.header_size = htole32(sizeof(prefix.capsule));
	prefix.capsule.flags = htole32(0x10000 | (options->initiate_reset ? 0x40000 : 0));
	prefix.capsule.image_size = htole32(total);
	prefix.fmp.version = htole32(1);
	prefix.fmp.image_count = htole16(1);
	prefix.fmp.image_offset = htole64(sizeof(prefix.fmp));
	prefix.image.version = htole32(3);
	prefix.image.index = 1;
	prefix.image.image_size = htole32(image_size);
	prefix.image.support = htole64(1);
	prefix.authentication.certificate_size =
		htole32(sizeof(prefix.authentication) - sizeof(uint64_t) + der_size);
	prefix.authentication.revision = htole16(0x200);
	prefix.authentication.type = htole16(0xef1);
	file = fopen(options->output, "wbx");
	if (!file) {
		OPENSSL_free(der);
		return false;
	}
	success = fwrite(&prefix, 1, sizeof(prefix), file) == sizeof(prefix) &&
		fwrite(der, 1, der_size, file) == (size_t)der_size &&
		fwrite(payload, 1, payload_size, file) == payload_size;
	if (fclose(file))
		success = false;
	if (!success)
		remove(options->output);
	OPENSSL_free(der);
	return success;
}

int main(int argc, char **argv)
{
	struct options options = { 0 };
	static const struct option arguments[] = {
		{ "rom", required_argument, NULL, 'i' },
		{ "output", required_argument, NULL, 'o' },
		{ "guid", required_argument, NULL, 'g' },
		{ "version", required_argument, NULL, 'v' },
		{ "lsv", required_argument, NULL, 'l' },
		{ "localversion", required_argument, NULL, 'V' },
		{ "regions", required_argument, NULL, 'r' },
		{ "signer", required_argument, NULL, 's' },
		{ "intermediate", required_argument, NULL, 'c' },
		{ "root", required_argument, NULL, 't' },
		{ "initiate-reset", no_argument, NULL, 'R' },
		{ NULL, 0, NULL, 0 }
	};
	uint8_t *rom = NULL, *payload = NULL;
	size_t rom_size = 0, payload_size = 0;
	const struct fmap *map;
	PKCS7 *signature = NULL;
	int option, result = EXIT_FAILURE;

	while ((option = getopt_long(argc, argv, "", arguments, NULL)) != -1) {
		switch (option) {
		case 'i': options.rom = optarg; break;
		case 'o': options.output = optarg; break;
		case 'g': options.guid = optarg; break;
		case 'v': options.version = optarg; break;
		case 'l': options.lowest_version = optarg; break;
		case 'V': options.localversion = optarg; break;
		case 'r': options.regions = optarg; break;
		case 's': options.signer = optarg; break;
		case 'c': options.intermediate = optarg; break;
		case 't': options.root = optarg; break;
		case 'R': options.initiate_reset = true; break;
		default: return EXIT_FAILURE;
		}
	}
	if (optind != argc || !options.rom || !options.output || !options.guid ||
	    !options.version || !options.lowest_version || !options.regions ||
	    !options.signer || !options.intermediate || !options.root) {
		fprintf(stderr, "Usage: %s --rom ROM --output CAPSULE --guid GUID --version N --lsv N "
			"[--localversion VERSION] --regions 'COREBOOT EC' --signer PEM "
			"--intermediate PEM --root PEM [--initiate-reset]\n", argv[0]);
		return EXIT_FAILURE;
	}
	rom = read_rom(options.rom, &rom_size);
	if (!rom || !(map = find_fmap(rom, rom_size))) {
		fprintf(stderr, "ROM must contain one valid, complete FMAP\n");
		goto out;
	}
	payload = build_payload(rom, rom_size, map, &options, &payload_size);
	if (!payload) {
		fprintf(stderr, "Invalid capsule version or region manifest\n");
		goto out;
	}
	signature = sign_payload(payload, payload_size, &options);
	if (!signature || !write_capsule(&options, payload, payload_size, signature)) {
		fprintf(stderr, "Could not sign, verify or write capsule\n");
		ERR_print_errors_fp(stderr);
		goto out;
	}
	printf("Signed capsule: %s\n", options.output);
	result = EXIT_SUCCESS;
out:
	PKCS7_free(signature);
	free(payload);
	free(rom);
	return result;
}
