/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#ifndef BOOT_AUTHVAR_POLICY_H
#define BOOT_AUTHVAR_POLICY_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define AUTHVAR_POLICY_BYTES 512U
#define AUTHVAR_POLICY_SLOTS 128U
#define AUTHVAR_POLICY_REVISION 0x00010000U

struct authvar_policy_entry {
	uint32_t version;
	uint16_t size, offset_to_name;
	uint8_t name_space[16];
	uint32_t min_size, max_size, attributes_must_have, attributes_cant_have;
	uint8_t lock_policy_type, padding[3];
} __packed;

_Static_assert(sizeof(struct authvar_policy_entry) == 44 &&
	offsetof(struct authvar_policy_entry, name_space) == 8 &&
	offsetof(struct authvar_policy_entry, lock_policy_type) == 40,
	"VariablePolicy entry wire layout");

struct authvar_policy {
	_Alignas(uint16_t) uint8_t entries[AUTHVAR_POLICY_SLOTS][AUTHVAR_POLICY_BYTES];
	size_t count;
	bool locked;
};

typedef uint64_t authvar_policy_get_fn(void *context, const uint16_t *name,
	const void *guid, uint32_t *attributes, size_t *size, void *data);

/* Calls, including the borrowed getter, are serialized with registry changes. */
bool authvar_policy_has_match(const struct authvar_policy *policy,
	const uint16_t *name, size_t name_size, const void *guid);
uint64_t authvar_policy_register(struct authvar_policy *policy,
	const struct authvar_policy_entry *entry);
/* rejected identifies only a policy refusal, not a failure returned by get. */
uint64_t authvar_policy_check(const struct authvar_policy *policy,
	const uint16_t *name, const void *guid, uint32_t attributes, size_t size,
	authvar_policy_get_fn *get, void *context,
	const struct authvar_policy_entry **rejected);

#endif
