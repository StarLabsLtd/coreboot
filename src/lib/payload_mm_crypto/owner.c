/* SPDX-License-Identifier: GPL-2.0-only */

#include <mbedtls/platform_util.h>
#include <commonlib/helpers.h>

#include "payload_mm_crypto_platform.h"
#include "crypto.h"

static struct payload_mm_crypto_owner *active_owner;
static uint8_t abort_token;

#ifdef DAVE_TPM_SMM_CRYPTO
/* Sorted live spans let short-lived MPI temporaries reuse gaps in the arena. */
static struct {
	size_t offset;
	size_t size;
} live[PAYLOAD_MM_CRYPTO_MAX_ALLOCATIONS];
static bool reuse_freed;
#endif

#define ABORT_GUARD ((struct payload_mm_crypto_owner *)(void *)&abort_token)

#ifdef PAYLOAD_MM_AUTH_TEST
#endif

static bool allocation_request_valid(
	struct payload_mm_crypto_owner *owner, size_t count, size_t size,
	size_t *bytes)
{
	size_t aligned;

	if (owner == NULL || count == 0U || size == 0U ||
	    count > SIZE_MAX / size)
		return false;

	*bytes = count * size;
	if (*bytes > PAYLOAD_MM_CRYPTO_MAX_ALLOCATION_SIZE ||
	    owner->allocation_count >= PAYLOAD_MM_CRYPTO_MAX_ALLOCATIONS ||
	    *bytes > SIZE_MAX - 15U)
		return false;
	aligned = (*bytes + 15U) & ~(size_t)15U;
#ifdef DAVE_TPM_SMM_CRYPTO
	if (reuse_freed)
		return aligned <= sizeof(owner->arena);
#endif
	return owner->arena_used <= sizeof(owner->arena) &&
		aligned <= sizeof(owner->arena) - owner->arena_used;
}

int payload_mm_crypto_snprintf(char *buffer, size_t size,
	const char *format, ...)
{
	(void)buffer;
	(void)size;
	(void)format;
	return -1;
}

void mbedtls_platform_zeroize(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size != 0U) {
		*bytes++ = 0U;
		size--;
	}
}

static void reset_owner(struct payload_mm_crypto_owner *owner)
{
#ifdef DAVE_TPM_SMM_CRYPTO
	mbedtls_platform_zeroize(live, sizeof(live));
	reuse_freed = false;
#endif
	mbedtls_platform_zeroize(owner->arena, sizeof(owner->arena));
	owner->arena_used = 0U;
	owner->allocation_count = 0U;
	owner->allocation_failed = false;
	owner->busy = false;
}

void *payload_mm_crypto_calloc(size_t count, size_t size)
{
	struct payload_mm_crypto_owner *owner = __atomic_load_n(&active_owner,
		__ATOMIC_ACQUIRE);
	struct payload_mm_crypto_owner *expected = owner;
	size_t aligned;
	size_t bytes;
	void *allocation;

	if (owner == NULL || owner == ABORT_GUARD ||
	    !__atomic_compare_exchange_n(&active_owner, &expected, ABORT_GUARD,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return NULL;
	if (!allocation_request_valid(owner, count, size, &bytes)) {
		owner->allocation_failed = true;
		__atomic_store_n(&active_owner, owner, __ATOMIC_RELEASE);
		return NULL;
	}

#ifdef PAYLOAD_MM_AUTH_TEST
#endif
	aligned = (bytes + 15U) & ~(size_t)15U;
#ifdef DAVE_TPM_SMM_CRYPTO
	if (reuse_freed) {
		size_t offset = 0, index;

		for (index = 0; index < owner->allocation_count; index++) {
			if (aligned <= live[index].offset - offset)
				break;
			offset = live[index].offset + live[index].size;
		}
		if (aligned > sizeof(owner->arena) - offset) {
			owner->allocation_failed = true;
			__atomic_store_n(&active_owner, owner, __ATOMIC_RELEASE);
			return NULL;
		}
		memmove(live + index + 1, live + index,
			(owner->allocation_count - index) * sizeof(*live));
		live[index].offset = offset;
		live[index].size = aligned;
		owner->allocation_count++;
		owner->arena_used = MAX(owner->arena_used, offset + aligned);
		allocation = owner->arena + offset;
	} else
#endif
	{
		allocation = owner->arena + owner->arena_used;
		owner->arena_used += aligned;
		owner->allocation_count++;
	}
	memset(allocation, 0, aligned);
	__atomic_store_n(&active_owner, owner, __ATOMIC_RELEASE);
	return allocation;
}

void payload_mm_crypto_free(void *allocation)
{
#ifdef DAVE_TPM_SMM_CRYPTO
	struct payload_mm_crypto_owner *owner, *expected;
	size_t index;

	if (!allocation)
		return;
	owner = __atomic_load_n(&active_owner, __ATOMIC_ACQUIRE);
	expected = owner;
	if (!owner || owner == ABORT_GUARD ||
	    !__atomic_compare_exchange_n(&active_owner, &expected, ABORT_GUARD,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return;
	if (!reuse_freed)
		goto out;
	for (index = 0; index < owner->allocation_count; index++) {
		if (allocation == owner->arena + live[index].offset)
			break;
	}
	if (index == owner->allocation_count) {
		owner->allocation_failed = true;
		goto out;
	}
	mbedtls_platform_zeroize(allocation, live[index].size);
	owner->allocation_count--;
	memmove(live + index, live + index + 1,
		(owner->allocation_count - index) * sizeof(*live));
	mbedtls_platform_zeroize(live + owner->allocation_count, sizeof(*live));
	owner->arena_used = owner->allocation_count ?
		live[owner->allocation_count - 1].offset + live[owner->allocation_count - 1].size : 0;
out:
	__atomic_store_n(&active_owner, owner, __ATOMIC_RELEASE);
#else
	/* The complete protected arena is wiped when verification ends. */
	(void)allocation;
#endif
}

static enum payload_mm_verify_status begin(struct payload_mm_crypto_owner *owner,
	bool reusable)
{
	struct payload_mm_crypto_owner *expected = NULL;

	if (owner == NULL)
		return PAYLOAD_MM_VERIFY_INVALID;
	if (!__atomic_compare_exchange_n(&active_owner, &expected, ABORT_GUARD,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return PAYLOAD_MM_VERIFY_BUSY;
#ifdef PAYLOAD_MM_AUTH_TEST
#endif
	if (owner->busy) {
		__atomic_store_n(&active_owner, NULL, __ATOMIC_RELEASE);
		return PAYLOAD_MM_VERIFY_BUSY;
	}

	owner->busy = true;
	owner->allocation_count = 0U;
	owner->arena_used = 0U;
	owner->allocation_failed = false;
	mbedtls_platform_zeroize(owner->arena, sizeof(owner->arena));
#ifdef DAVE_TPM_SMM_CRYPTO
	mbedtls_platform_zeroize(live, sizeof(live));
	reuse_freed = reusable;
#else
	(void)reusable;
#endif
	__atomic_store_n(&active_owner, owner, __ATOMIC_RELEASE);
	return PAYLOAD_MM_VERIFY_OK;
}

enum payload_mm_verify_status payload_mm_crypto_begin(struct payload_mm_crypto_owner *owner)
{
	return begin(owner, false);
}

#ifdef DAVE_TPM_SMM_CRYPTO
enum payload_mm_verify_status payload_mm_crypto_begin_reusable(
	struct payload_mm_crypto_owner *owner)
{
	return begin(owner, true);
}
#endif

enum payload_mm_verify_status payload_mm_crypto_end(
	struct payload_mm_crypto_owner *owner,
	enum payload_mm_verify_status status)
{
	struct payload_mm_crypto_owner *expected = owner;

	if (owner == NULL || !__atomic_compare_exchange_n(&active_owner,
		&expected, ABORT_GUARD, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return PAYLOAD_MM_VERIFY_INTERNAL;
	if (owner->allocation_failed)
		status = PAYLOAD_MM_VERIFY_NO_MEMORY;
#ifdef DAVE_TPM_SMM_CRYPTO
	if (reuse_freed && owner->allocation_count)
		status = PAYLOAD_MM_VERIFY_INTERNAL;
	mbedtls_platform_zeroize(live, sizeof(live));
	reuse_freed = false;
#endif
	mbedtls_platform_zeroize(owner->arena, sizeof(owner->arena));
	owner->arena_used = 0U;
	owner->allocation_failed = false;
	owner->busy = false;
	__atomic_store_n(&active_owner, NULL, __ATOMIC_RELEASE);
	return status;
}

bool payload_mm_crypto_abort(struct payload_mm_crypto_owner *owner)
{
	struct payload_mm_crypto_owner *expected;

	if (owner == NULL)
		return false;
	expected = __atomic_load_n(&active_owner, __ATOMIC_ACQUIRE);
#ifdef PAYLOAD_MM_AUTH_TEST
#endif
	if (expected != NULL && expected != owner)
		return false;
	if (!__atomic_compare_exchange_n(&active_owner, &expected, ABORT_GUARD,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return false;
	reset_owner(owner);
	__atomic_store_n(&active_owner, NULL, __ATOMIC_RELEASE);
	return true;
}

bool payload_mm_crypto_idle(void)
{
	return __atomic_load_n(&active_owner, __ATOMIC_ACQUIRE) == NULL;
}

bool payload_mm_crypto_owner_is_clean(
	const struct payload_mm_crypto_owner *owner)
{
	struct payload_mm_crypto_owner *expected = NULL;
	uint8_t combined = 0U;
	bool clean;

	if (owner == NULL || !__atomic_compare_exchange_n(&active_owner,
		&expected, ABORT_GUARD, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return false;
	for (size_t index = 0U; index < sizeof(owner->arena); index++)
		combined |= owner->arena[index];
	clean = !owner->arena_used && !owner->busy &&
		!owner->allocation_failed && combined == 0U;
	__atomic_store_n(&active_owner, NULL, __ATOMIC_RELEASE);
	return clean;
}

bool payload_mm_crypto_abort_active(void)
{
	struct payload_mm_crypto_owner *owner = __atomic_load_n(&active_owner,
		__ATOMIC_ACQUIRE);

	if (owner == NULL || owner == ABORT_GUARD ||
	    !__atomic_compare_exchange_n(&active_owner, &owner, ABORT_GUARD,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return false;
	reset_owner(owner);
	__atomic_store_n(&active_owner, NULL, __ATOMIC_RELEASE);
	return true;
}
