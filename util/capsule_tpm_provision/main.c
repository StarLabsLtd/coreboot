/* SPDX-License-Identifier: GPL-2.0-only */

#include "policy.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define ARTIFACT_FILE_COUNT 17

struct artifact_file {
	const char *name;
	const uint8_t *data;
	size_t size;
};

static void usage(const char *program)
{
	fprintf(stderr,
		"Usage: %s generate|validate --index HEX --epoch NUMBER \\\n"
		" --manifest-sha256 HEX --rsa-modulus HEX --policy-ref HEX \\\n"
		" --output DIRECTORY\n",
		program);
}

static int hex_nibble(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

static bool parse_hex(const char *text, uint8_t *bytes, size_t capacity,
	size_t *size)
{
	size_t length;

	if (!text || !bytes || !size)
		return false;
	length = strlen(text);
	if (!length || (length & 1) || length / 2 > capacity)
		return false;
	memset(bytes, 0, capacity);
	for (size_t i = 0; i < length / 2; i++) {
		int high = hex_nibble(text[2 * i]);
		int low = hex_nibble(text[2 * i + 1]);

		if (high < 0 || low < 0) {
			memset(bytes, 0, capacity);
			return false;
		}
		bytes[i] = (high << 4) | low;
	}
	*size = length / 2;
	return true;
}

static bool parse_u32(const char *text, uint32_t *value)
{
	char *end;
	uintmax_t parsed;

	errno = 0;
	parsed = strtoumax(text, &end, 0);
	if (errno || !text[0] || text[0] == '-' || text[0] == '+' || *end ||
	    parsed > UINT32_MAX)
		return false;
	*value = parsed;
	return true;
}

static bool parse_u64(const char *text, uint64_t *value)
{
	char *end;
	uintmax_t parsed;

	errno = 0;
	parsed = strtoumax(text, &end, 0);
	if (errno || !text[0] || text[0] == '-' || text[0] == '+' || *end ||
	    parsed > UINT64_MAX)
		return false;
	*value = parsed;
	return true;
}

static bool write_all(int file, const uint8_t *data, size_t size)
{
	while (size) {
		ssize_t written = write(file, data, size);

		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			return false;
		data += written;
		size -= written;
	}
	return true;
}

static bool write_artifact(int directory, const struct artifact_file *artifact)
{
	int file;
	bool result;

	file = openat(directory, artifact->name,
		O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
	if (file < 0)
		return false;
	result = write_all(file, artifact->data, artifact->size) &&
		fsync(file) == 0;
	if (close(file) != 0)
		result = false;
	return result;
}

static bool read_exact(int file, uint8_t *data, size_t size)
{
	uint8_t extra;

	while (size) {
		ssize_t received = read(file, data, size);

		if (received < 0 && errno == EINTR)
			continue;
		if (received <= 0)
			return false;
		data += received;
		size -= received;
	}
	return read(file, &extra, sizeof(extra)) == 0;
}

static bool validate_artifact(int directory,
	const struct artifact_file *artifact)
{
	uint8_t *data;
	struct stat status;
	int file;
	bool result = false;

	file = openat(directory, artifact->name,
		O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
	if (file < 0)
		return false;
	data = malloc(artifact->size);
	if (!data)
		goto out;
	if (fstat(file, &status) || !S_ISREG(status.st_mode) ||
	    status.st_size != (off_t)artifact->size ||
	    !read_exact(file, data, artifact->size) ||
	    memcmp(data, artifact->data, artifact->size)) {
		errno = EINVAL;
		goto out;
	}
	result = true;
out:
	free(data);
	close(file);
	return result;
}

static size_t artifact_index(const struct artifact_file *files,
	size_t file_count, const char *name)
{
	for (size_t i = 0; i < file_count; i++)
		if (!strcmp(files[i].name, name))
			return i;
	return file_count;
}

static bool validate_directory(int directory,
	const struct artifact_file *files, size_t file_count)
{
	bool seen[ARTIFACT_FILE_COUNT] = { 0 };
	struct dirent *entry;
	DIR *stream;
	int stream_file;
	int saved_errno;

	if (file_count != ARRAY_SIZE(seen)) {
		errno = EINVAL;
		return false;
	}
	stream_file = dup(directory);
	if (stream_file < 0)
		return false;
	stream = fdopendir(stream_file);
	if (!stream) {
		saved_errno = errno;
		close(stream_file);
		errno = saved_errno;
		return false;
	}
	for (;;) {
		size_t index;

		errno = 0;
		entry = readdir(stream);
		if (!entry)
			break;
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		index = artifact_index(files, file_count, entry->d_name);
		if (index == file_count || seen[index] ||
		    !validate_artifact(directory, &files[index])) {
			if (!errno)
				errno = EINVAL;
			goto failure;
		}
		seen[index] = true;
	}
	if (errno)
		goto failure;
	for (size_t i = 0; i < file_count; i++) {
		if (!seen[i]) {
			errno = EINVAL;
			goto failure;
		}
	}
	return closedir(stream) == 0;

failure:
	saved_errno = errno;
	closedir(stream);
	errno = saved_errno;
	return false;
}

static size_t artifact_files(
	const struct capsule_tpm_provision_artifacts *artifacts,
	struct artifact_file files[static ARTIFACT_FILE_COUNT])
{
	const struct artifact_file list[] = {
		{ "descriptor.bin", artifacts->descriptor,
			sizeof(artifacts->descriptor) },
		{ "anchor.bin", artifacts->anchor, sizeof(artifacts->anchor) },
		{ "rsa-public.bin", artifacts->rsa_public,
			artifacts->rsa_public_size },
		{ "authority-name.bin", artifacts->authority_name,
			sizeof(artifacts->authority_name) },
		{ "nv-public.bin", artifacts->nv_public,
			sizeof(artifacts->nv_public) },
		{ "nv-name.bin", artifacts->nv_name,
			sizeof(artifacts->nv_name) },
		{ "written-nv-name.bin", artifacts->written_nv_name,
			sizeof(artifacts->written_nv_name) },
		{ "write-branch.bin", artifacts->write_branch,
			sizeof(artifacts->write_branch) },
		{ "lock-branch.bin", artifacts->lock_branch,
			sizeof(artifacts->lock_branch) },
		{ "policy-or-digests.bin", artifacts->policy_or_digests,
			sizeof(artifacts->policy_or_digests) },
		{ "auth-policy.bin", artifacts->auth_policy,
			sizeof(artifacts->auth_policy) },
		{ "initial-write-cphash.bin", artifacts->write_cp_hash,
			sizeof(artifacts->write_cp_hash) },
		{ "initial-write-approved-policy.bin",
			artifacts->write_approved_policy,
			sizeof(artifacts->write_approved_policy) },
		{ "initial-write-authorization-hash.bin",
			artifacts->write_authorization_hash,
			sizeof(artifacts->write_authorization_hash) },
		{ "initial-lock-cphash.bin", artifacts->lock_cp_hash,
			sizeof(artifacts->lock_cp_hash) },
		{ "initial-lock-approved-policy.bin",
			artifacts->lock_approved_policy,
			sizeof(artifacts->lock_approved_policy) },
		{ "initial-lock-authorization-hash.bin",
			artifacts->lock_authorization_hash,
			sizeof(artifacts->lock_authorization_hash) },
	};

	memcpy(files, list, sizeof(list));
	return ARRAY_SIZE(list);
}

int main(int argc, char **argv)
{
	struct capsule_tpm_provision_input input = { 0 };
	struct capsule_tpm_provision_artifacts artifacts;
	struct artifact_file files[ARTIFACT_FILE_COUNT];
	const char *output = NULL;
	bool generate;
	bool have_index = false;
	bool have_epoch = false;
	bool have_manifest = false;
	bool have_modulus = false;
	bool have_policy_ref = false;
	size_t size;
	size_t file_count;
	int directory = -1;
	int result = 1;

	if (argc < 2 || (strcmp(argv[1], "generate") &&
		strcmp(argv[1], "validate"))) {
		usage(argv[0]);
		return 2;
	}
	generate = !strcmp(argv[1], "generate");
	for (int i = 2; i < argc; i += 2) {
		if (i + 1 == argc)
			goto usage;
		if (!strcmp(argv[i], "--index")) {
			if (have_index || !parse_u32(argv[i + 1], &input.nv_index))
				goto usage;
			have_index = true;
		} else if (!strcmp(argv[i], "--epoch")) {
			if (have_epoch || !parse_u64(argv[i + 1], &input.epoch))
				goto usage;
			have_epoch = true;
		} else if (!strcmp(argv[i], "--manifest-sha256")) {
			if (have_manifest || !parse_hex(argv[i + 1],
				input.manifest_digest,
				sizeof(input.manifest_digest), &size) ||
			    size != sizeof(input.manifest_digest))
				goto usage;
			have_manifest = true;
		} else if (!strcmp(argv[i], "--rsa-modulus")) {
			if (have_modulus || !parse_hex(argv[i + 1],
				input.rsa_modulus, sizeof(input.rsa_modulus),
				&size) || size > UINT16_MAX)
				goto usage;
			input.rsa_modulus_size = size;
			have_modulus = true;
		} else if (!strcmp(argv[i], "--policy-ref")) {
			if (have_policy_ref || !parse_hex(argv[i + 1],
				input.policy_ref, sizeof(input.policy_ref), &size) ||
			    size > UINT16_MAX)
				goto usage;
			input.policy_ref_size = size;
			have_policy_ref = true;
		} else if (!strcmp(argv[i], "--output")) {
			if (output)
				goto usage;
			output = argv[i + 1];
		} else {
			goto usage;
		}
	}
	if (!have_index || !have_epoch || !have_manifest || !have_modulus ||
	    !have_policy_ref || !output ||
	    !capsule_tpm_provision_generate(&input, &artifacts))
		goto usage;
	file_count = artifact_files(&artifacts, files);
	if (generate) {
		if (mkdir(output, 0700) ||
		    (directory = open(output,
			O_RDONLY | O_DIRECTORY | O_NOFOLLOW)) < 0)
			goto failure;
		for (size_t i = 0; i < file_count; i++)
			if (!write_artifact(directory, &files[i]))
				goto failure;
		if (fsync(directory))
			goto failure;
	} else {
		directory = open(output, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
		if (directory < 0)
			goto failure;
		if (!validate_directory(directory, files, file_count))
			goto failure;
	}
	result = 0;
	goto out;
usage:
	usage(argv[0]);
	result = 2;
	goto out;
failure:
	fprintf(stderr, "%s failed: %s\n", generate ? "generation" :
		"validation", strerror(errno));
out:
	if (directory >= 0)
		close(directory);
	memset(&input, 0, sizeof(input));
	memset(&artifacts, 0, sizeof(artifacts));
	return result;
}
