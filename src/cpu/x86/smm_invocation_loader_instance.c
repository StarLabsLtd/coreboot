/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_loader_instance.h>
#include <string.h>

#if defined(__TEST__)
void smm_invocation_loader_instance_test_hook(uint32_t point);
#define LOADER_INSTANCE_TEST_HOOK(point) smm_invocation_loader_instance_test_hook(point)
#else
#define LOADER_INSTANCE_TEST_HOOK(point) do { } while (0)
#endif

static void scrub_bytes(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool lifecycle_valid(uint32_t lifecycle)
{
	return lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD ||
		lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD;
}

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

void smm_invocation_loader_instance_scrub(
	struct smm_invocation_loader_instance *instance)
{
	if (!object_valid(instance, sizeof(*instance), _Alignof(*instance)))
		return;

	__atomic_store_n(&instance->state, SMM_INVOCATION_LOADER_INSTANCE_SCRUBBING,
		__ATOMIC_RELEASE);
	scrub_bytes((uint8_t *)instance + sizeof(instance->state),
		sizeof(*instance) - sizeof(instance->state));
	__atomic_store_n(&instance->state, SMM_INVOCATION_LOADER_INSTANCE_EMPTY,
		__ATOMIC_RELEASE);
}

int smm_invocation_loader_instance_loader_result(
	struct smm_invocation_loader_instance *instance, int result)
{
	if (result)
		smm_invocation_loader_instance_scrub(instance);
	return result;
}

enum cb_err smm_invocation_loader_instance_publish(
	struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_loader_instance_seed *seed)
{
	struct smm_invocation_loader_instance_seed copy;
	const struct smm_invocation_loader_instance empty = { 0 };
	uint32_t expected = SMM_INVOCATION_LOADER_INSTANCE_EMPTY;

	if (!object_valid(instance, sizeof(*instance), _Alignof(*instance)))
		return CB_ERR_ARG;
	if (!object_valid(seed, sizeof(*seed), _Alignof(*seed)) ||
	    objects_overlap(instance, sizeof(*instance), seed, sizeof(*seed)))
		return CB_ERR_ARG;
	memcpy(&copy, seed, sizeof(copy));
	if (copy.revision != SMM_INVOCATION_LOADER_INSTANCE_REVISION ||
	    copy.size != sizeof(copy) || !lifecycle_valid(copy.lifecycle) ||
	    copy.reserved ||
	    (!copy.loader_instance_nonce.low &&
	     !copy.loader_instance_nonce.high)) {
		scrub_bytes(&copy, sizeof(copy));
		return CB_ERR;
	}
	if (!__atomic_compare_exchange_n(&instance->state, &expected,
		SMM_INVOCATION_LOADER_INSTANCE_PROVISIONING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		scrub_bytes(&copy, sizeof(copy));
		return CB_ERR;
	}
	if (memcmp((const uint8_t *)instance + sizeof(instance->state),
		(const uint8_t *)&empty + sizeof(empty.state),
		sizeof(empty) - sizeof(empty.state))) {
		smm_invocation_loader_instance_scrub(instance);
		scrub_bytes(&copy, sizeof(copy));
		return CB_ERR;
	}

	instance->revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION;
	instance->size = sizeof(*instance);
	instance->lifecycle = copy.lifecycle;
	instance->loader_instance_nonce = copy.loader_instance_nonce;
	instance->reserved = 0;
	LOADER_INSTANCE_TEST_HOOK(1);
	if (__atomic_load_n(&instance->state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_LOADER_INSTANCE_PROVISIONING ||
	    instance->revision != SMM_INVOCATION_LOADER_INSTANCE_REVISION ||
	    instance->size != sizeof(*instance) ||
	    instance->lifecycle != copy.lifecycle ||
	    instance->loader_instance_nonce.low != copy.loader_instance_nonce.low ||
	    instance->loader_instance_nonce.high != copy.loader_instance_nonce.high ||
	    instance->reserved ||
	    memcmp(&copy, seed, sizeof(copy))) {
		smm_invocation_loader_instance_scrub(instance);
		scrub_bytes(&copy, sizeof(copy));
		return CB_ERR;
	}
	LOADER_INSTANCE_TEST_HOOK(2);
	__atomic_store_n(&instance->state, SMM_INVOCATION_LOADER_INSTANCE_READY,
		__ATOMIC_RELEASE);
	scrub_bytes(&copy, sizeof(copy));
	return CB_SUCCESS;
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_loader_instance *snapshot)
{
	struct smm_invocation_loader_instance copy;

	if (!object_valid(instance, sizeof(*instance), _Alignof(*instance)) ||
	    !object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)) ||
	    objects_overlap(instance, sizeof(*instance), snapshot, sizeof(*snapshot)))
		return CB_ERR_ARG;
	memset(snapshot, 0, sizeof(*snapshot));
	if (__atomic_load_n(&instance->state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_LOADER_INSTANCE_READY)
		return CB_ERR;
	memcpy(&copy, instance, sizeof(copy));
	LOADER_INSTANCE_TEST_HOOK(3);
	if (__atomic_load_n(&instance->state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_LOADER_INSTANCE_READY ||
	    memcmp(&copy, instance, sizeof(copy)) ||
	    copy.state != SMM_INVOCATION_LOADER_INSTANCE_READY ||
	    copy.revision != SMM_INVOCATION_LOADER_INSTANCE_REVISION ||
	    copy.size != sizeof(copy) || !lifecycle_valid(copy.lifecycle) ||
	    (!copy.loader_instance_nonce.low &&
	     !copy.loader_instance_nonce.high) || copy.reserved) {
		scrub_bytes(&copy, sizeof(copy));
		return CB_ERR;
	}
	memcpy(snapshot, &copy, sizeof(*snapshot));
	scrub_bytes(&copy, sizeof(copy));
	return CB_SUCCESS;
}
