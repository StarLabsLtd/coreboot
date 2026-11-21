/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_POLICY_H
#define BOOT_PAYLOAD_MM_AUTHVAR_POLICY_H

#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>

/* Internal protected request, not a wire ABI. No caller timestamp or plan. */
struct payload_mm_authvar_policy_request {
	uint32_t operation;
	uint32_t attributes;
	uint8_t vendor_guid[16];
	const void *name;
	size_t name_size;
	const void *data;
	size_t data_size;
};

enum payload_mm_authvar_mode_key {
	PAYLOAD_MM_AUTHVAR_MODE_KEY_PK,
	PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE,
	PAYLOAD_MM_AUTHVAR_MODE_KEY_VENDOR_KEYS_NV,
	PAYLOAD_MM_AUTHVAR_MODE_KEY_CUSTOM_MODE,
};

/* Bind an internal request to one of the existing fixed mode-key identities. */
bool payload_mm_authvar_mode_request(uint32_t key,
	struct payload_mm_authvar_policy_request *request);
bool payload_mm_authvar_confirmed_key_request(uint32_t key, uint32_t mutation,
	const void *data, size_t size, struct payload_mm_authvar_policy_request *request);

enum payload_mm_authvar_mutation_kind {
	PAYLOAD_MM_AUTHVAR_MUTATION_WRITE = 1,
	PAYLOAD_MM_AUTHVAR_MUTATION_DELETE = 2,
};

struct payload_mm_authvar_policy_mutation {
	uint32_t kind;
	uint32_t attributes;
	uint32_t data_size;
	uint8_t timestamp[16];
};

#endif
