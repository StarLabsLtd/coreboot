/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_CERTDB_H
#define BOOT_PAYLOAD_MM_AUTHVAR_CERTDB_H

#include <stddef.h>
#include <stdint.h>

enum payload_mm_authvar_certdb_result {
	PAYLOAD_MM_AUTHVAR_CERTDB_OK = 0,
	PAYLOAD_MM_AUTHVAR_CERTDB_INVALID,
	PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED,
	PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND,
	PAYLOAD_MM_AUTHVAR_CERTDB_EXISTS,
	PAYLOAD_MM_AUTHVAR_CERTDB_NO_SPACE,
};

enum payload_mm_authvar_certdb_operation {
	PAYLOAD_MM_AUTHVAR_CERTDB_ADD = 0,
	PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE,
};

struct payload_mm_authvar_certdb_binding {
	const uint8_t *data;
	size_t size;
};

/*
 * Validate the complete EDK2 26.09 certdb serialization before returning one
 * borrowed binding.  name is canonical non-empty UTF-16LE without a trailing
 * NUL, matching the packed certdb key.  Every input and output range must be
 * disjoint.  binding remains byte-exact untouched unless the result is OK.
 */
enum payload_mm_authvar_certdb_result payload_mm_authvar_certdb_find(
	const void *source, size_t source_size, const uint8_t vendor_guid[16],
	const void *name, size_t name_size,
	struct payload_mm_authvar_certdb_binding *binding);

/*
 * Compose one complete certdb replacement without accessing media.  ADD
 * appends one opaque non-empty binding and REMOVE compacts one existing node;
 * replacing an existing binding is deliberately unsupported.  source,
 * output, key, binding data and output_size must be disjoint.  Output bytes
 * and output_size remain byte-exact untouched on every failure.
 */
enum payload_mm_authvar_certdb_result payload_mm_authvar_certdb_compose(
	const void *source, size_t source_size,
	enum payload_mm_authvar_certdb_operation operation,
	const uint8_t vendor_guid[16], const void *name, size_t name_size,
	const void *binding, size_t binding_size,
	void *output, size_t output_capacity, size_t *output_size);

#endif
