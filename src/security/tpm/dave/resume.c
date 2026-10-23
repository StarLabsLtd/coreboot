/* SPDX-License-Identifier: BSD-2-Clause-Patent AND LicenseRef-LICENSE-TBC */
/* Copyright (c) 2026 Star Labs Systems. All rights reserved. */
/* Adapted from DAVE SecureKeyTpmFactoryLib, SecureKeyTpmAuthLib and
 * SecureKeyTpmProtectedCommandLib at 0f425ebcc9e8.
 * CPU entropy qualification follows EDK2 BaseRngLib/Rand/RdRand.c:
 * Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.
 * Copyright (c) 2023 Arm Limited. All rights reserved.
 * Copyright (c) 2022 Pedro Falcato. All rights reserved.
 * Copyright (c) 2021 NUVIA Inc. All rights reserved.
 * Copyright (c) 2015 Intel Corporation. All rights reserved.
 */

#include <arch/cpu.h>
#include <commonlib/helpers.h>
#include <drivers/crb/tpm.h>
#include <drivers/smmstore/srk.h>
#include <mbedtls/platform_util.h>
#include <random.h>
#include <security/tpm/dave/session.h>
#include <string.h>

#include "crypto.h"
#include "resume.h"

_Static_assert(CONFIG_SMM_MODULE_STACK_SIZE >= 0x4000,
	"DAVE TPM sessions require a 16 KiB SMM stack");

static struct payload_mm_crypto_owner crypto_owner;
static bool failed;

static uint16_t get16(const uint8_t *p)
{
	return (uint16_t)p[0] << 8 | p[1];
}

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint64_t exchange(void *context, const uint8_t *command, size_t command_size,
	uint8_t *response, size_t capacity)
{
	(void)context;
	return crb_tpm_process_command(command, command_size, response, capacity) ?
		DAVE_TPM_SUCCESS : DAVE_TPM_DEVICE_ERROR;
}

static int cpu_random(void *context, unsigned char *output, size_t size)
{
	size_t offset = 0;
	uint64_t sample = 0;
	int result = -1;

	(void)context;
	while (offset < size) {
		size_t count = MIN(size - offset, sizeof(sample));

		if (get_random_number_64(&sample) != CB_SUCCESS)
			goto out;
		memcpy(output + offset, &sample, count);
		offset += count;
	}
	result = 0;
out:
	mbedtls_platform_zeroize(&sample, sizeof(sample));
	if (result)
		mbedtls_platform_zeroize(output, size);
	return result;
}

static bool qualify_entropy(void)
{
	uint64_t previous = 0, sample = 0;
	unsigned int changes = 0;
	bool valid = false;

	if (cpuid_get_max_func() < 1 || !(cpuid(1).ecx & (1U << 30)))
		return false;
	for (unsigned int i = 0; i < 8; i++) {
		if (cpu_random(NULL, (void *)&sample, sizeof(sample)))
			goto out;
		if (i && previous != sample)
			changes++;
		previous = sample;
	}
	valid = changes >= 5;
out:
	mbedtls_platform_zeroize(&sample, sizeof(sample));
	mbedtls_platform_zeroize(&previous, sizeof(previous));
	return valid;
}

static bool finish_crypto(uint64_t status)
{
	return payload_mm_crypto_end(&crypto_owner, status == DAVE_TPM_SUCCESS ?
		PAYLOAD_MM_VERIFY_OK : PAYLOAD_MM_VERIFY_REJECTED) == PAYLOAD_MM_VERIFY_OK;
}

static bool committed(struct tpm_reader *reader)
{
	static const uint32_t indexes[] = {
		0x01500020, 0x01500022, 0x01500023, 0x01500024, 0x01500025,
	};
	struct nv_profile marker = {0};
	struct tpm_session session = {0};
	uint8_t public[48], name[34], value = 0;
	size_t size;
	uint64_t status;
	bool valid;

	/* Presence/WRITTEN is classification, not an authenticated mirror proof.
	 * Only the committed marker is consumed as pinned authenticated content.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(indexes); i++) {
		status = dave_tpm_public_area(reader, indexes[i], true,
			public, sizeof(public), &size, name);
		if (status != DAVE_TPM_SUCCESS || size < 12 ||
		    get32(public + 2) != indexes[i] ||
		    (i && !(get32(public + 8) & (1U << 29))))
			return false;
	}
	if (size != sizeof(public) || get16(public + 12) != 32)
		return false;
	marker.index = indexes[ARRAY_SIZE(indexes) - 1];
	marker.attributes = get32(public + 8);
	marker.size = get16(public + 46);
	memcpy(marker.name, name, sizeof(name));
	if (payload_mm_crypto_begin_reusable(&crypto_owner) != PAYLOAD_MM_VERIFY_OK)
		return false;
	status = dave_tpm_start_session(reader, &session, false);
	if (status == DAVE_TPM_SUCCESS)
		status = dave_tpm_authenticated_read(reader, &session, &marker, 1, 0,
			&value, false);
	status = dave_tpm_finish_session(reader, &session, status);
	valid = finish_crypto(status) && value == 0xa5;
	mbedtls_platform_zeroize(&session, sizeof(session));
	return valid;
}

static bool closed(struct tpm_reader *reader)
{
	static const uint8_t probe[28] = {
		0x80, 0x02, 0, 0, 0, 28, 0, 0, 0x01, 0x27,
		0x40, 0, 0, 0x0c, 0, 0, 0, 9, 0x40, 0, 0, 9,
		0, 0, 0, 0, 0, 1,
	};
	struct dave_tpm_lockout lockout = {0};
	uint8_t response[10];
	uint32_t attributes = 0;
	size_t size;
	uint64_t status;

	/* Each session's EC objects are freed before ending its arena lifetime.
	 * No allocation or cleanup state is reused across the property reads.
	 */
	if (payload_mm_crypto_begin_reusable(&crypto_owner) != PAYLOAD_MM_VERIFY_OK)
		return false;
	status = dave_tpm_permanent_attributes(reader, &attributes);
	if (!finish_crypto(status) || (attributes & 0x107) != 0x107)
		return false;
	if (payload_mm_crypto_begin_reusable(&crypto_owner) != PAYLOAD_MM_VERIFY_OK)
		return false;
	status = dave_tpm_lockout_parameters(reader, &lockout);
	if (!finish_crypto(status) || lockout.max_tries != 32 ||
	    lockout.recovery_seconds != 7200 || lockout.lockout_recovery_seconds != 86400)
		return false;
	status = dave_tpm_submit(reader, probe, sizeof(probe), response, sizeof(response),
		0x8002, &size);
	return status == DAVE_TPM_DEVICE_ERROR && size == sizeof(response) &&
		get32(response + 6) == 0x9a2;
}

bool dave_s3_verify_tpm(void)
{
	struct tpm_reader reader = { .exchange = exchange, .random = cpu_random };
	bool valid = false;

	if (failed)
		return false;
	/* An uncertain cleanup cannot become a fresh authority on another SMI. */
	failed = true;
	if (!qualify_entropy() || !smmstore_copy_srk_pin(reader.pin) ||
	    crb_tpm_init() != TPM_SUCCESS)
		goto out;
	valid = committed(&reader) && closed(&reader) && !reader.cleanup_failed;
out:
	mbedtls_platform_zeroize(&reader, sizeof(reader));
	if (valid)
		failed = false;
	return valid;
}
