/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_loader_composition.h>

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool objects_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	return first_base <= second_base + second_size - 1U &&
		second_base <= first_base + first_size - 1U;
}

const struct smm_invocation_evidence *
smm_invocation_loader_composition_evidence(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_evidence *evidence)
{
	if (!object_valid(composition, sizeof(*composition),
		_Alignof(*composition)) ||
	    !object_valid(evidence, sizeof(*evidence), _Alignof(*evidence)) ||
	    objects_overlap(composition, sizeof(*composition), evidence,
		sizeof(*evidence)) ||
	    __atomic_load_n(&composition->state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_LOADER_COMPOSITION_READY ||
	    composition->owner_attempt != 1U || composition->reserved[0] ||
	    composition->reserved[1] ||
	    composition->evidence_identity != (uint64_t)(uintptr_t)evidence ||
	    __atomic_load_n(&composition->state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_LOADER_COMPOSITION_READY)
		return NULL;
	return evidence;
}
