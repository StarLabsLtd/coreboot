/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_linear.h>
#include <boot/payload_mm_authvar_mor_probe.h>
#include <commonlib/helpers.h>
#include <limits.h>
#include <string.h>
#include <timestamp.h>

#if !ENV_TEST
#include <bootstate.h>
#include <console/console.h>
#include <halt.h>
#endif

#if !ENV_RAMSTAGE && !ENV_TEST
#error "MOR linear coordinator is ramstage-only"
#endif

enum lifecycle_phase {
	LIFECYCLE_EMPTY,
	LIFECYCLE_CLAIMING,
	LIFECYCLE_CLASSIFYING,
	LIFECYCLE_PROBING,
	LIFECYCLE_CLOSING,
	LIFECYCLE_RESERVING,
	LIFECYCLE_RESERVED,
	LIFECYCLE_RESOLVING,
	LIFECYCLE_CLEARING,
	LIFECYCLE_COMMITTING,
	LIFECYCLE_COMPLETE,
	LIFECYCLE_CLOSED,
	LIFECYCLE_FAILED,
};

static struct {
	uintptr_t owner;
	uint64_t generation;
	uint8_t phase;
} lifecycle;

static void lifecycle_poison(void)
{
	__atomic_store_n(&lifecycle.phase, LIFECYCLE_FAILED, __ATOMIC_RELEASE);
}

static bool lifecycle_claim(
	struct payload_mm_authvar_mor_linear_state *state)
{
	uint8_t expected = LIFECYCLE_EMPTY;

	if (!__atomic_compare_exchange_n(&lifecycle.phase, &expected,
		LIFECYCLE_CLAIMING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		lifecycle_poison();
		return false;
	}
	__atomic_store_n(&lifecycle.owner, (uintptr_t)state, __ATOMIC_RELEASE);
	expected = LIFECYCLE_CLAIMING;
	if (!__atomic_compare_exchange_n(&lifecycle.phase, &expected,
		LIFECYCLE_CLASSIFYING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE)) {
		lifecycle_poison();
		return false;
	}
	return true;
}

static bool lifecycle_advance(
	const struct payload_mm_authvar_mor_linear_state *state,
	enum lifecycle_phase expected_phase, enum lifecycle_phase next_phase)
{
	uint8_t expected = expected_phase;

	if (__atomic_load_n(&lifecycle.owner, __ATOMIC_ACQUIRE) !=
	    (uintptr_t)state ||
	    !__atomic_compare_exchange_n(&lifecycle.phase, &expected, next_phase,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		lifecycle_poison();
		return false;
	}
	return true;
}

static bool lifecycle_owned(const struct payload_mm_authvar_mor_linear_state *state,
	enum lifecycle_phase phase)
{
	return __atomic_load_n(&lifecycle.owner, __ATOMIC_ACQUIRE) ==
		(uintptr_t)state &&
		__atomic_load_n(&lifecycle.phase, __ATOMIC_ACQUIRE) == phase;
}

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return !combined;
}

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	const uintptr_t left_base = (uintptr_t)left;
	const uintptr_t right_base = (uintptr_t)right;

	if (!object_valid(left, left_size, 1) ||
	    !object_valid(right, right_size, 1))
		return true;
	if (left_base <= right_base)
		return right_base - left_base < left_size;
	return left_base - right_base < right_size;
}

static void scrub_work(struct payload_mm_authvar_mor_linear_state *state)
{
	const uint32_t phase = state->phase;
	const uint32_t failure = state->failure;

	memset(state, 0, sizeof(*state));
	state->revision = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_REVISION;
	state->size = sizeof(*state);
	state->phase = phase;
	state->failure = failure;
}

static enum payload_mm_authvar_mor_linear_result fail(
	struct payload_mm_authvar_mor_linear_state *state,
	enum payload_mm_authvar_mor_linear_failure failure)
{
	lifecycle_poison();
	state->phase = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILED;
	state->failure = failure;
	scrub_work(state);
	return PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT;
}

static bool ops_valid(const struct payload_mm_authvar_mor_linear_ops *ops,
	const struct payload_mm_authvar_mor_linear_state *state)
{
	return object_valid(ops, sizeof(*ops), _Alignof(*ops)) && ops->context &&
		ops->context_size &&
		object_valid(ops->context, ops->context_size, 1) &&
		!ranges_overlap(ops, sizeof(*ops), state, sizeof(*state)) &&
		!ranges_overlap(ops->context, ops->context_size, state,
			sizeof(*state)) &&
		ops->classify_guard && ops->reservations_register &&
		ops->resolve_binding && ops->private_complete && ops->private_close;
}

static bool boot_valid(const struct payload_mm_authvar_mor_linear_boot *boot)
{
	if (boot->revision != PAYLOAD_MM_AUTHVAR_MOR_LINEAR_REVISION ||
	    boot->size != sizeof(*boot) || boot->reserved)
		return false;
	if (boot->kind == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_COLD_BOOT)
		return boot->generation != 0;
	return boot->kind == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_S3_RESUME &&
		!boot->generation;
}

static bool state_unchanged(
	const struct payload_mm_authvar_mor_linear_state *state,
	const struct payload_mm_authvar_mor_linear_ops *ops, uint64_t generation,
	const struct payload_mm_authvar_mor_entry *entry, uint32_t phase)
{
	return state->revision == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_REVISION &&
		state->size == sizeof(*state) && state->phase == phase &&
		state->failure == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_NONE &&
		state->generation == generation && !state->reserved &&
		!memcmp(&state->entry, entry, sizeof(*entry)) &&
		!memcmp(&state->ops, ops, sizeof(*ops));
}

static enum payload_mm_authvar_mor_linear_result close_authority(
	struct payload_mm_authvar_mor_linear_state *state,
	enum lifecycle_phase expected_phase)
{
	struct payload_mm_authvar_mor_linear_ops ops = state->ops;
	struct payload_mm_authvar_mor_linear_state frozen;
	bool unchanged;

	if (!lifecycle_advance(state, expected_phase, LIFECYCLE_CLOSING))
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PHASE);
	memcpy(&frozen, state, sizeof(frozen));
	const enum cb_err status = ops.private_close(ops.context);
	unchanged = !memcmp(state, &frozen, sizeof(frozen)) &&
		lifecycle_owned(state, LIFECYCLE_CLOSING);
	memset(&frozen, 0, sizeof(frozen));
	if (status != CB_SUCCESS || !unchanged)
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLOSE);
	if (!lifecycle_advance(state, LIFECYCLE_CLOSING, LIFECYCLE_CLOSED))
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLOSE);
	state->phase = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_CLOSED;
	scrub_work(state);
	return PAYLOAD_MM_AUTHVAR_MOR_LINEAR_CONTINUE;
}

enum payload_mm_authvar_mor_linear_result
payload_mm_authvar_mor_linear_before_bootmem(
	struct payload_mm_authvar_mor_linear_state *state,
	const struct payload_mm_authvar_mor_linear_ops *ops)
{
	struct payload_mm_authvar_mor_linear_ops frozen_ops;
	struct payload_mm_authvar_mor_linear_state frozen_state;
	struct payload_mm_authvar_mor_linear_boot boot = { 0 };
	struct payload_mm_authvar_mor_entry entry = { 0 };

	if (!object_valid(state, sizeof(*state), _Alignof(*state))) {
		lifecycle_poison();
		return PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT;
	}
	/* Own the state before reading it; rejected entrants must not touch it. */
	if (!lifecycle_claim(state))
		return PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT;
	if (!bytes_zero(state, sizeof(*state)) || !ops_valid(ops, state)) {
		memset(state, 0, sizeof(*state));
		state->revision = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_REVISION;
		state->size = sizeof(*state);
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PROVIDER);
	}
	memcpy(&frozen_ops, ops, sizeof(frozen_ops));
	*state = (struct payload_mm_authvar_mor_linear_state) {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_REVISION,
		.size = sizeof(*state),
		.ops = frozen_ops,
	};
	/* A competing entry may have poisoned the claim before this boundary. */
	if (!lifecycle_owned(state, LIFECYCLE_CLASSIFYING))
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PHASE);
	timestamp_add_now(TS_MOR_DISCOVERY_START);
	memcpy(&frozen_state, state, sizeof(frozen_state));
	const enum cb_err classify_status =
		frozen_ops.classify_guard(frozen_ops.context, &boot);
	const bool classify_unchanged =
		!memcmp(state, &frozen_state, sizeof(frozen_state)) &&
		lifecycle_owned(state, LIFECYCLE_CLASSIFYING);
	memset(&frozen_state, 0, sizeof(frozen_state));
	if (classify_status != CB_SUCCESS || !classify_unchanged ||
	    memcmp(ops, &frozen_ops, sizeof(frozen_ops)) || !boot_valid(&boot))
		return fail(state,
			PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLASSIFICATION);
	state->generation = boot.generation;
	if (boot.kind == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_S3_RESUME) {
		timestamp_add_now(TS_MOR_DISCOVERY_END);
		return close_authority(state, LIFECYCLE_CLASSIFYING);
	}
	__atomic_store_n(&lifecycle.generation, boot.generation, __ATOMIC_RELEASE);
	if (!lifecycle_advance(state, LIFECYCLE_CLASSIFYING,
		LIFECYCLE_PROBING))
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PHASE);
	if (payload_mm_authvar_mor_probe_entry(&entry) != CB_SUCCESS ||
	    !lifecycle_owned(state, LIFECYCLE_PROBING) ||
	    __atomic_load_n(&lifecycle.generation, __ATOMIC_ACQUIRE) !=
		boot.generation ||
	    memcmp(ops, &frozen_ops, sizeof(frozen_ops)) || entry.present > 1U ||
	    entry.reserved)
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PROBE);
	state->entry = entry;
	if (!entry.present || !(entry.value & 1U)) {
		timestamp_add_now(TS_MOR_DISCOVERY_END);
		return close_authority(state, LIFECYCLE_PROBING);
	}
	if (!lifecycle_advance(state, LIFECYCLE_PROBING,
		LIFECYCLE_RESERVING))
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PHASE);
	memcpy(&frozen_state, state, sizeof(frozen_state));
	const enum cb_err reservation_status =
		frozen_ops.reservations_register(frozen_ops.context);
	const bool reservation_unchanged =
		!memcmp(state, &frozen_state, sizeof(frozen_state)) &&
		lifecycle_owned(state, LIFECYCLE_RESERVING) &&
		__atomic_load_n(&lifecycle.generation, __ATOMIC_ACQUIRE) ==
			boot.generation;
	memset(&frozen_state, 0, sizeof(frozen_state));
	if (reservation_status != CB_SUCCESS || !reservation_unchanged ||
	    memcmp(ops, &frozen_ops, sizeof(frozen_ops)))
		return fail(state,
			PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_RESERVATION);
	state->phase = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_RESERVED;
	if (!lifecycle_advance(state, LIFECYCLE_RESERVING, LIFECYCLE_RESERVED))
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_RESERVATION);
	timestamp_add_now(TS_MOR_DISCOVERY_END);
	return PAYLOAD_MM_AUTHVAR_MOR_LINEAR_WAIT_FOR_BOOTMEM;
}

enum payload_mm_authvar_mor_linear_result
payload_mm_authvar_mor_linear_after_bootmem(
	struct payload_mm_authvar_mor_linear_state *state)
{
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_entry entry;
	struct payload_mm_authvar_mor_linear_state completed;
	uint64_t generation;
	bool commit_unchanged;
	enum cb_err commit_status;

	if (!object_valid(state, sizeof(*state), _Alignof(*state))) {
		lifecycle_poison();
		return PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT;
	}
	/* A rejected entrant may alias the owner's state: never write it. */
	if (!lifecycle_owned(state, LIFECYCLE_RESERVED)) {
		lifecycle_poison();
		return PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT;
	}
	if (state->phase != PAYLOAD_MM_AUTHVAR_MOR_LINEAR_RESERVED ||
	    state->failure != PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_NONE ||
	    state->revision != PAYLOAD_MM_AUTHVAR_MOR_LINEAR_REVISION ||
	    state->size != sizeof(*state) || state->reserved ||
	    __atomic_load_n(&lifecycle.generation, __ATOMIC_ACQUIRE) !=
		state->generation || !state->generation ||
	    state->entry.present != 1U || !(state->entry.value & 1U) ||
	    state->entry.reserved || !bytes_zero(&state->plan, sizeof(state->plan)) ||
	    !bytes_zero(&state->executor, sizeof(state->executor)) ||
	    !bytes_zero(&state->transcript, sizeof(state->transcript)) ||
	    !bytes_zero(&state->grant, sizeof(state->grant)))
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PHASE);
	ops = state->ops;
	entry = state->entry;
	generation = state->generation;
	if (!lifecycle_advance(state, LIFECYCLE_RESERVED, LIFECYCLE_RESOLVING))
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PHASE);
	if (ops.resolve_binding(ops.context, generation, &state->plan,
		&state->executor) != CB_SUCCESS ||
	    !lifecycle_owned(state, LIFECYCLE_RESOLVING) ||
	    !state_unchanged(state, &ops, generation, &entry,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_RESERVED) ||
	    payload_mm_authvar_mor_clear_plan_validate(&state->plan) != CB_SUCCESS ||
	    !state->executor.window_bytes || !state->executor.dma_snapshot ||
	    !state->executor.map_window ||
	    !state->executor.cache_writeback_invalidate ||
	    !state->executor.fence || !state->executor.unmap_window ||
	    !state->executor.inventory_validate)
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_BINDING);
	if (!lifecycle_advance(state, LIFECYCLE_RESOLVING,
		LIFECYCLE_CLEARING))
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_BINDING);
	timestamp_add_now(TS_MOR_CLEAR_START);
	if (payload_mm_authvar_mor_clear_execute(&state->plan, &state->entry,
		generation, &state->executor, &state->transcript,
		&state->grant) != CB_SUCCESS ||
	    !lifecycle_owned(state, LIFECYCLE_CLEARING) ||
	    !state_unchanged(state, &ops, generation, &entry,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_RESERVED))
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLEAR);
	timestamp_add_now(TS_MOR_CLEAR_END);
	if (!lifecycle_advance(state, LIFECYCLE_CLEARING,
		LIFECYCLE_COMMITTING))
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLEAR);
	timestamp_add_now(TS_MOR_COMMIT_START);
	memcpy(&completed, state, sizeof(completed));
	commit_status = ops.private_complete(ops.context, &state->grant);
	commit_unchanged = !memcmp(state, &completed, sizeof(completed)) &&
		lifecycle_owned(state, LIFECYCLE_COMMITTING);
	memset(&completed, 0, sizeof(completed));
	if (commit_status != CB_SUCCESS || !commit_unchanged)
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_COMMIT);
	timestamp_add_now(TS_MOR_COMMIT_END);
	if (!lifecycle_advance(state, LIFECYCLE_COMMITTING,
		LIFECYCLE_COMPLETE))
		return fail(state, PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_COMMIT);
	state->phase = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_COMPLETE;
	scrub_work(state);
	return PAYLOAD_MM_AUTHVAR_MOR_LINEAR_CONTINUE;
}

const char *payload_mm_authvar_mor_linear_failure_name(
	enum payload_mm_authvar_mor_linear_failure failure)
{
	static const char *const names[] = {
		[PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_NONE] = "none",
		[PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PROVIDER] = "provider",
		[PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLASSIFICATION] = "classification/guard",
		[PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PROBE] = "Control probe",
		[PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLOSE] = "private close",
		[PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_RESERVATION] = "reservation registration",
		[PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PHASE] = "phase/state validation",
		[PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_BINDING] = "reservation resolution/binding",
		[PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLEAR] = "memory clear/readback",
		[PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_COMMIT] = "private completion commit",
	};

	if ((size_t)failure >= ARRAY_SIZE(names) || !names[failure])
		return "unknown";
	return names[failure];
}

__weak bool platform_payload_mm_authvar_mor_linear_ops(
	struct payload_mm_authvar_mor_linear_ops *ops)
{
	if (ops)
		memset(ops, 0, sizeof(*ops));
	return false;
}

#if ENV_TEST
void payload_mm_authvar_mor_linear_reset_test(void)
{
	memset(&lifecycle, 0, sizeof(lifecycle));
}
#else
static struct payload_mm_authvar_mor_linear_state boot_state;

static void halt_failure(void)
{
	printk(BIOS_EMERG, "MOR: fatal linear boot failure: %s\n",
		payload_mm_authvar_mor_linear_failure_name(boot_state.failure));
	die("MOR linear boot cannot continue\n");
}

static void mor_before_bootmem(void *unused)
{
	struct payload_mm_authvar_mor_linear_ops ops = { 0 };

	if (!platform_payload_mm_authvar_mor_linear_ops(&ops)) {
		boot_state.failure = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PROVIDER;
		halt_failure();
	}
	if (payload_mm_authvar_mor_linear_before_bootmem(&boot_state, &ops) ==
	    PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT)
		halt_failure();
}

static void mor_after_bootmem(void *unused)
{
	if (boot_state.phase != PAYLOAD_MM_AUTHVAR_MOR_LINEAR_RESERVED)
		return;
	if (payload_mm_authvar_mor_linear_after_bootmem(&boot_state) ==
	    PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT)
		halt_failure();
}

BOOT_STATE_INIT_ENTRY(BS_OS_RESUME_CHECK, BS_ON_ENTRY, mor_before_bootmem, NULL);
BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_EXIT, mor_after_bootmem, NULL);
#endif
