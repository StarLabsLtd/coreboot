/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/pre_os_lifecycle.h>
#include <string.h>

#if ENV_SMM && !ENV_TEST
#error "The pre-OS TPM lifecycle must not be built in SMM"
#endif

#define CONTROL_POISON 0x80000000U
#define CONTROL_MAGIC 0x25000000U
#define CONTROL_MAGIC_MASK 0x7f000000U
#define CONTROL_REVISION (TPM_PRE_OS_LIFECYCLE_REVISION << 8)

_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0),
	"TPM lifecycle control must be lock-free");

struct lifecycle_snapshot {
	uint32_t revision;
	uint32_t context_size;
	uint64_t generation;
	struct tpm_pre_os_backend backend;
};

static bool bytes_zero(const void *data, size_t size)
{
	const uint8_t *bytes = data;
	uint8_t value = 0;

	for (size_t i = 0; i < size; i++)
		value |= bytes[i];
	return value == 0;
}

static uint32_t control_word(enum tpm_pre_os_state state)
{
	uint8_t value = state;

	return CONTROL_MAGIC | ((uint32_t)(uint8_t)~value << 16) |
		CONTROL_REVISION | value;
}

static enum tpm_pre_os_state decode_control(uint32_t control)
{
	uint32_t base = control & ~CONTROL_POISON;
	uint8_t state;

	if (!control)
		return TPM_PRE_OS_UNBOUND;
	state = base;
	if ((base & CONTROL_MAGIC_MASK) != CONTROL_MAGIC ||
	    (base & 0x0000ff00U) != CONTROL_REVISION ||
	    (uint8_t)(base >> 16) != (uint8_t)~state ||
	    state > TPM_PRE_OS_FAILED)
		return TPM_PRE_OS_FAILED;
	if (state == TPM_PRE_OS_HANDED_OFF)
		return state;
	if (control & CONTROL_POISON)
		return TPM_PRE_OS_FAILED;
	return state;
}

static enum tpm_pre_os_state load_state(
	const struct tpm_pre_os_lifecycle *lifecycle)
{
	if (!lifecycle)
		return TPM_PRE_OS_FAILED;
	return decode_control(__atomic_load_n(&lifecycle->control,
		__ATOMIC_ACQUIRE));
}

static bool transition(struct tpm_pre_os_lifecycle *lifecycle,
	enum tpm_pre_os_state old_state, enum tpm_pre_os_state new_state)
{
	uint32_t expected = old_state == TPM_PRE_OS_UNBOUND ? 0 :
		control_word(old_state);

	return __atomic_compare_exchange_n(&lifecycle->control, &expected,
		control_word(new_state), false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE);
}

static void request_failure(struct tpm_pre_os_lifecycle *lifecycle)
{
	if (lifecycle)
		__atomic_fetch_or(&lifecycle->control, CONTROL_POISON,
			__ATOMIC_ACQ_REL);
}

/* Call only after the holder has returned from its provider callback. */
static void seal_failed(struct tpm_pre_os_lifecycle *lifecycle)
{
	__atomic_store_n(&lifecycle->control,
		control_word(TPM_PRE_OS_FAILED) | CONTROL_POISON,
		__ATOMIC_RELEASE);
	memset(&lifecycle->backend, 0, sizeof(lifecycle->backend));
	memset(lifecycle->backend_context, 0,
		sizeof(lifecycle->backend_context));
	lifecycle->context_size = 0;
}

static bool claim(struct tpm_pre_os_lifecycle *lifecycle,
	enum tpm_pre_os_state old_state, enum tpm_pre_os_state new_state)
{
	uint32_t expected;

	if (!lifecycle)
		return false;
	expected = old_state == TPM_PRE_OS_UNBOUND ? 0 :
		control_word(old_state);
	if (__atomic_compare_exchange_n(&lifecycle->control, &expected,
		control_word(new_state), false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return true;
	request_failure(lifecycle);
	return false;
}

static bool range_end(uintptr_t base, size_t size, uintptr_t *end)
{
	if (!size || size > (uintptr_t)-1 - base)
		return false;
	*end = base + size;
	return true;
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_end;
	uintptr_t right_end;
	uintptr_t left_base = (uintptr_t)left;
	uintptr_t right_base = (uintptr_t)right;

	if (!range_end(left_base, left_size, &left_end) ||
	    !range_end(right_base, right_size, &right_end))
		return true;
	return left_base < right_end && right_base < left_end;
}

static void clear_token(struct tpm_pre_os_token *token)
{
	if (token)
		memset(token, 0, sizeof(*token));
}

static void clear_response(uint8_t *response, size_t capacity,
	size_t *response_size)
{
	if (response && capacity)
		memset(response, 0, capacity);
	if (response_size)
		*response_size = 0;
}

static bool metadata_valid(const struct tpm_pre_os_lifecycle *lifecycle)
{
	return lifecycle->revision == TPM_PRE_OS_LIFECYCLE_REVISION &&
		lifecycle->context_size <= TPM_PRE_OS_BACKEND_CONTEXT_SIZE &&
		lifecycle->backend.begin && lifecycle->backend.transmit &&
		lifecycle->backend.quiesce && lifecycle->backend.release;
}

static struct lifecycle_snapshot snapshot(
	const struct tpm_pre_os_lifecycle *lifecycle)
{
	return (struct lifecycle_snapshot) {
		.revision = lifecycle->revision,
		.context_size = lifecycle->context_size,
		.generation = lifecycle->generation,
		.backend = lifecycle->backend,
	};
}

static bool snapshot_valid(const struct tpm_pre_os_lifecycle *lifecycle,
	const struct lifecycle_snapshot *saved)
{
	return metadata_valid(lifecycle) &&
		lifecycle->revision == saved->revision &&
		lifecycle->context_size == saved->context_size &&
		lifecycle->generation == saved->generation &&
		!memcmp(&lifecycle->backend, &saved->backend,
			sizeof(saved->backend));
}

static bool token_valid(const struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token)
{
	return token && token->lifecycle == (uintptr_t)lifecycle &&
		token->generation && token->generation == lifecycle->generation;
}

static bool unbound_metadata_zero(
	const struct tpm_pre_os_lifecycle *lifecycle)
{
	return !lifecycle->revision && !lifecycle->context_size &&
		!lifecycle->generation &&
		bytes_zero(&lifecycle->backend, sizeof(lifecycle->backend)) &&
		bytes_zero(lifecycle->backend_context,
			sizeof(lifecycle->backend_context));
}

static bool terminal_clean(const struct tpm_pre_os_lifecycle *lifecycle)
{
	return lifecycle->revision == TPM_PRE_OS_LIFECYCLE_REVISION &&
		!lifecycle->context_size &&
		bytes_zero(&lifecycle->backend, sizeof(lifecycle->backend)) &&
		bytes_zero(lifecycle->backend_context,
			sizeof(lifecycle->backend_context));
}

enum cb_err tpm_pre_os_lifecycle_install(
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_backend *backend,
	const void *backend_context, size_t backend_context_size)
{
	struct lifecycle_snapshot saved;
	enum cb_err result;

	if (!lifecycle)
		return CB_ERR_ARG;
	if (!claim(lifecycle, TPM_PRE_OS_UNBOUND, TPM_PRE_OS_BUSY))
		return CB_ERR;
	if (!unbound_metadata_zero(lifecycle) || !backend ||
	    backend_context_size > TPM_PRE_OS_BACKEND_CONTEXT_SIZE ||
	    (!!backend_context != !!backend_context_size) ||
	    ranges_overlap(backend, sizeof(*backend), lifecycle,
		sizeof(*lifecycle)) ||
	    (backend_context_size && ranges_overlap(backend_context,
		backend_context_size, lifecycle, sizeof(*lifecycle)))) {
		seal_failed(lifecycle);
		return CB_ERR_ARG;
	}
	lifecycle->revision = TPM_PRE_OS_LIFECYCLE_REVISION;
	lifecycle->backend = *backend;
	lifecycle->context_size = backend_context_size;
	if (backend_context_size)
		memcpy(lifecycle->backend_context, backend_context,
			backend_context_size);
	if (!metadata_valid(lifecycle)) {
		seal_failed(lifecycle);
		return CB_ERR_ARG;
	}
	saved = snapshot(lifecycle);
	result = saved.backend.begin(lifecycle->backend_context);
	if (result != CB_SUCCESS || !snapshot_valid(lifecycle, &saved) ||
	    !transition(lifecycle, TPM_PRE_OS_BUSY, TPM_PRE_OS_AVAILABLE)) {
		seal_failed(lifecycle);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

enum cb_err tpm_pre_os_lifecycle_acquire(
	struct tpm_pre_os_lifecycle *lifecycle,
	struct tpm_pre_os_token *token)
{
	if (!token) {
		request_failure(lifecycle);
		return CB_ERR_ARG;
	}
	if (!lifecycle) {
		clear_token(token);
		return CB_ERR_ARG;
	}
	if (ranges_overlap(token, sizeof(*token), lifecycle,
		sizeof(*lifecycle))) {
		request_failure(lifecycle);
		return CB_ERR_ARG;
	}
	clear_token(token);
	if (!claim(lifecycle, TPM_PRE_OS_AVAILABLE, TPM_PRE_OS_BUSY))
		return CB_ERR;
	if (!metadata_valid(lifecycle) || lifecycle->generation == UINT64_MAX) {
		seal_failed(lifecycle);
		return CB_ERR;
	}
	lifecycle->generation++;
	token->lifecycle = (uintptr_t)lifecycle;
	token->generation = lifecycle->generation;
	if (!transition(lifecycle, TPM_PRE_OS_BUSY, TPM_PRE_OS_OWNED)) {
		clear_token(token);
		seal_failed(lifecycle);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

enum cb_err tpm_pre_os_lifecycle_transmit(
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token,
	const uint8_t *request, size_t request_size,
	uint8_t *response, size_t *response_size)
{
	struct lifecycle_snapshot saved;
	size_t capacity;
	enum cb_err result;
	bool response_alias;

	if (!response_size) {
		request_failure(lifecycle);
		return CB_ERR_ARG;
	}
	if (!lifecycle) {
		*response_size = 0;
		return CB_ERR_ARG;
	}
	if (!token || ranges_overlap(token, sizeof(*token), lifecycle,
		sizeof(*lifecycle)) ||
	    ranges_overlap(response_size, sizeof(*response_size), lifecycle,
		sizeof(*lifecycle)) ||
	    ranges_overlap(response_size, sizeof(*response_size), token,
		sizeof(*token))) {
		request_failure(lifecycle);
		return CB_ERR_ARG;
	}
	capacity = *response_size;
	response_alias = response && capacity &&
		(ranges_overlap(response, capacity, lifecycle,
			sizeof(*lifecycle)) ||
		 ranges_overlap(response, capacity, token, sizeof(*token)));
	if (!request || !request_size || !response || !capacity ||
	    ranges_overlap(request, request_size, lifecycle,
		sizeof(*lifecycle)) ||
	    ranges_overlap(request, request_size, token, sizeof(*token)) ||
	    response_alias) {
		if (response_alias)
			*response_size = 0;
		else
			clear_response(response, capacity, response_size);
		request_failure(lifecycle);
		return CB_ERR_ARG;
	}
	if (!claim(lifecycle, TPM_PRE_OS_OWNED, TPM_PRE_OS_BUSY)) {
		clear_response(response, capacity, response_size);
		request_failure(lifecycle);
		return CB_ERR;
	}
	if (!metadata_valid(lifecycle) || !token_valid(lifecycle, token)) {
		clear_response(response, capacity, response_size);
		seal_failed(lifecycle);
		return CB_ERR;
	}
	saved = snapshot(lifecycle);
	result = saved.backend.transmit(lifecycle->backend_context,
		request, request_size, response, response_size);
	if (result != CB_SUCCESS || !snapshot_valid(lifecycle, &saved) ||
	    !token_valid(lifecycle, token) || !*response_size ||
	    *response_size > capacity ||
	    !transition(lifecycle, TPM_PRE_OS_BUSY, TPM_PRE_OS_OWNED)) {
		clear_response(response, capacity, response_size);
		seal_failed(lifecycle);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

enum cb_err tpm_pre_os_lifecycle_end(
	struct tpm_pre_os_lifecycle *lifecycle,
	struct tpm_pre_os_token *token)
{
	if (!lifecycle)
		return CB_ERR_ARG;
	if (!token || ranges_overlap(token, sizeof(*token), lifecycle,
		sizeof(*lifecycle))) {
		request_failure(lifecycle);
		return CB_ERR_ARG;
	}
	if (!claim(lifecycle, TPM_PRE_OS_OWNED, TPM_PRE_OS_BUSY)) {
		clear_token(token);
		request_failure(lifecycle);
		return CB_ERR;
	}
	if (!metadata_valid(lifecycle) || !token_valid(lifecycle, token)) {
		clear_token(token);
		seal_failed(lifecycle);
		return CB_ERR;
	}
	clear_token(token);
	if (!transition(lifecycle, TPM_PRE_OS_BUSY, TPM_PRE_OS_AVAILABLE)) {
		seal_failed(lifecycle);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

enum cb_err tpm_pre_os_lifecycle_fail(
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token)
{
	if (!lifecycle)
		return CB_ERR_ARG;
	if (!token || ranges_overlap(token, sizeof(*token), lifecycle,
		sizeof(*lifecycle))) {
		request_failure(lifecycle);
		return CB_ERR_ARG;
	}
	if (!claim(lifecycle, TPM_PRE_OS_OWNED, TPM_PRE_OS_BUSY))
		return CB_ERR;
	if (!metadata_valid(lifecycle) || !token_valid(lifecycle, token)) {
		seal_failed(lifecycle);
		return CB_ERR;
	}
	seal_failed(lifecycle);
	return CB_SUCCESS;
}

enum cb_err tpm_pre_os_lifecycle_handoff(
	struct tpm_pre_os_lifecycle *lifecycle)
{
	struct lifecycle_snapshot saved;
	enum cb_err result;

	if (!claim(lifecycle, TPM_PRE_OS_AVAILABLE,
		TPM_PRE_OS_HANDING_OFF))
		return CB_ERR;
	if (!metadata_valid(lifecycle)) {
		seal_failed(lifecycle);
		return CB_ERR;
	}
	saved = snapshot(lifecycle);
	result = saved.backend.quiesce(lifecycle->backend_context);
	if (result != CB_SUCCESS || !snapshot_valid(lifecycle, &saved) ||
	    load_state(lifecycle) != TPM_PRE_OS_HANDING_OFF) {
		seal_failed(lifecycle);
		return CB_ERR;
	}
	result = saved.backend.release(lifecycle->backend_context);
	if (result != CB_SUCCESS || !snapshot_valid(lifecycle, &saved) ||
	    load_state(lifecycle) != TPM_PRE_OS_HANDING_OFF) {
		seal_failed(lifecycle);
		return CB_ERR;
	}
	memset(&lifecycle->backend, 0, sizeof(lifecycle->backend));
	memset(lifecycle->backend_context, 0,
		sizeof(lifecycle->backend_context));
	lifecycle->context_size = 0;
	if (!transition(lifecycle, TPM_PRE_OS_HANDING_OFF,
		TPM_PRE_OS_HANDED_OFF)) {
		seal_failed(lifecycle);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

enum tpm_pre_os_state tpm_pre_os_lifecycle_state(
	const struct tpm_pre_os_lifecycle *lifecycle)
{
	return load_state(lifecycle);
}

bool tpm_pre_os_lifecycle_os_access_allowed(
	const struct tpm_pre_os_lifecycle *lifecycle)
{
	return lifecycle && load_state(lifecycle) == TPM_PRE_OS_HANDED_OFF &&
		terminal_clean(lifecycle);
}
