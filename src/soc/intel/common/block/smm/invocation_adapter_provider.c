/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/intel/smm_invocation_adapter.h>
#include <cpu/intel/smm_invocation_adapter_provider.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <intelblocks/smm_save_state_ops.h>
#include <string.h>

#include "invocation_adapter_internal.h"

#if defined(__TEST__)
void intel_smm_invocation_adapter_provider_test_hook(uint32_t point);
const uint32_t *intel_smm_invocation_adapter_provider_test_revision_table(
	const struct smm_save_state_ops *ops);
#define PROVIDER_TEST_HOOK(point) \
	intel_smm_invocation_adapter_provider_test_hook(point)
#define PROVIDER_REVISION_TABLE(ops) \
	intel_smm_invocation_adapter_provider_test_revision_table(ops)
#else
#define PROVIDER_TEST_HOOK(point) do { } while (0)
#define PROVIDER_REVISION_TABLE(ops) ((ops)->revision_table)
#endif

enum provider_state {
	PROVIDER_EMPTY,
	PROVIDER_PROVISIONING,
	PROVIDER_READY,
	PROVIDER_ARMING,
	PROVIDER_ARMED,
	PROVIDER_RETIRING,
	PROVIDER_POISONED,
};

struct adapter_provider {
	uint32_t state;
	uint64_t last_generation;
	uint64_t active_generation;
	struct intel_smm_invocation_adapter adapter;
	struct smm_invocation_save_state_ops ops;
};

struct runtime_borrower {
	const struct smm_invocation_runtime_view *view;
};

static struct adapter_provider provider;

static bool range_valid(const void *base, size_t size)
{
	return base && size && (uintptr_t)base <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (!range_valid(first, first_size) || !range_valid(second, second_size))
		return true;
	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

static enum cb_err runtime_span(const void *context, uint32_t cpu,
	struct smm_save_state_span *span)
{
	const struct runtime_borrower *borrower = context;

	return smm_invocation_runtime_save_state_span(borrower->view, cpu, span);
}

static enum cb_err canonical_revision(uint32_t *revision)
{
	const struct smm_save_state_ops *native_ops = get_smm_save_state_ops();
	const uint32_t *revision_table;

	if (native_ops != &em64t100_smm_ops && native_ops != &em64t101_smm_ops)
		return CB_ERR;
	revision_table = PROVIDER_REVISION_TABLE(native_ops);
	if (!revision_table || revision_table[1] != SMM_REV_INVALID ||
	    !intel_smm_invocation_adapter_revision_supported(revision_table[0]))
		return CB_ERR;
	*revision = revision_table[0];
	return CB_SUCCESS;
}

static void poison(void)
{
	memset((uint8_t *)&provider + sizeof(provider.state), 0,
		sizeof(provider) - sizeof(provider.state));
	__atomic_store_n(&provider.state, PROVIDER_POISONED, __ATOMIC_RELEASE);
}

static enum smm_invocation_try_result observe_busy_or_error(uint32_t state)
{
	if (state == PROVIDER_PROVISIONING || state == PROVIDER_ARMING ||
	    state == PROVIDER_ARMED || state == PROVIDER_RETIRING)
		return SMM_INVOCATION_TRY_RETRY;
	return SMM_INVOCATION_TRY_ERROR;
}

enum smm_invocation_try_result
intel_smm_invocation_adapter_provider_provision(
	const struct smm_invocation_save_state_ops **ops)
{
	const struct smm_invocation_runtime_view *view = NULL;
	struct runtime_borrower borrower;
	struct smm_invocation_save_state_ops sealed_ops;
	enum cb_err result;
	uint32_t expected_revision;
	uint32_t active_cpus = 0;
	uint32_t empty = PROVIDER_EMPTY;
	uint32_t state;

	if (!ops || (uintptr_t)ops % _Alignof(*ops) ||
	    canonical_revision(&expected_revision) != CB_SUCCESS ||
	    ranges_overlap(ops, sizeof(*ops), &provider, sizeof(provider)))
		return SMM_INVOCATION_TRY_ERROR;
	state = __atomic_load_n(&provider.state, __ATOMIC_ACQUIRE);
	if (state == PROVIDER_READY) {
		uint32_t ready = PROVIDER_READY;

		if (!__atomic_compare_exchange_n(&provider.state, &ready,
			PROVIDER_PROVISIONING, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			return observe_busy_or_error(ready);
		PROVIDER_TEST_HOOK(4);
		if (intel_smm_invocation_adapter_bind(&provider.adapter,
			&sealed_ops) != CB_SUCCESS || memcmp(&sealed_ops,
				&provider.ops, sizeof(sealed_ops))) {
			poison();
			return SMM_INVOCATION_TRY_ERROR;
		}
		if (provider.adapter.expected_revision != expected_revision ||
		    !intel_smm_invocation_adapter_range_disjoint(&provider.adapter,
			ops, sizeof(*ops))) {
			__atomic_store_n(&provider.state, PROVIDER_READY,
				__ATOMIC_RELEASE);
			return SMM_INVOCATION_TRY_ERROR;
		}
		*ops = &provider.ops;
		PROVIDER_TEST_HOOK(5);
		__atomic_store_n(&provider.state, PROVIDER_READY, __ATOMIC_RELEASE);
		return SMM_INVOCATION_TRY_SUCCESS;
	}
	if (state != PROVIDER_EMPTY)
		return observe_busy_or_error(state);
	if (!__atomic_compare_exchange_n(&provider.state, &empty,
		PROVIDER_PROVISIONING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return observe_busy_or_error(empty);
	PROVIDER_TEST_HOOK(1);
	if (smm_invocation_runtime_view_get(&view) != CB_SUCCESS ||
	    smm_invocation_runtime_cpu_count(view, &active_cpus) != CB_SUCCESS) {
		poison();
		return SMM_INVOCATION_TRY_ERROR;
	}
	borrower.view = view;
	result = intel_smm_invocation_adapter_init_spans(&provider.adapter,
		active_cpus, runtime_span, &borrower, sizeof(borrower),
		expected_revision);
	memset(&borrower, 0, sizeof(borrower));
	if (result != CB_SUCCESS ||
	    !intel_smm_invocation_adapter_range_disjoint(&provider.adapter, ops,
		sizeof(*ops)) ||
	    intel_smm_invocation_adapter_bind(&provider.adapter, &provider.ops) !=
		CB_SUCCESS ||
	    intel_smm_invocation_adapter_end(&provider.adapter) != CB_SUCCESS) {
		poison();
		return SMM_INVOCATION_TRY_ERROR;
	}
	*ops = &provider.ops;
	PROVIDER_TEST_HOOK(5);
	__atomic_store_n(&provider.state, PROVIDER_READY, __ATOMIC_RELEASE);
	return SMM_INVOCATION_TRY_SUCCESS;
}

enum smm_invocation_try_result
intel_smm_invocation_adapter_provider_arm(uint64_t *generation)
{
	struct smm_invocation_save_state_ops sealed_ops;
	uint32_t ready = PROVIDER_READY;
	uint64_t next;

	if (!generation || (uintptr_t)generation % _Alignof(*generation) ||
	    ranges_overlap(generation, sizeof(*generation), &provider,
		sizeof(provider)))
		return SMM_INVOCATION_TRY_ERROR;
	if (!__atomic_compare_exchange_n(&provider.state, &ready, PROVIDER_ARMING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return observe_busy_or_error(ready);
	PROVIDER_TEST_HOOK(2);
	if (intel_smm_invocation_adapter_bind(&provider.adapter, &sealed_ops) !=
		CB_SUCCESS || memcmp(&sealed_ops, &provider.ops,
			sizeof(sealed_ops))) {
		poison();
		return SMM_INVOCATION_TRY_ERROR;
	}
	if (!intel_smm_invocation_adapter_range_disjoint(&provider.adapter,
		generation, sizeof(*generation))) {
		__atomic_store_n(&provider.state, PROVIDER_READY, __ATOMIC_RELEASE);
		return SMM_INVOCATION_TRY_ERROR;
	}
	if (provider.last_generation == UINT64_MAX ||
	    intel_smm_invocation_adapter_begin(&provider.adapter) != CB_SUCCESS) {
		poison();
		return SMM_INVOCATION_TRY_ERROR;
	}
	next = provider.last_generation + 1U;
	if (!next) {
		poison();
		return SMM_INVOCATION_TRY_ERROR;
	}
	provider.last_generation = next;
	provider.active_generation = next;
	*generation = next;
	__atomic_store_n(&provider.state, PROVIDER_ARMED, __ATOMIC_RELEASE);
	return SMM_INVOCATION_TRY_SUCCESS;
}

enum smm_invocation_try_result
intel_smm_invocation_adapter_provider_retire(uint64_t generation)
{
	uint32_t armed = PROVIDER_ARMED;

	if (!__atomic_compare_exchange_n(&provider.state, &armed,
		PROVIDER_RETIRING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return observe_busy_or_error(armed);
	PROVIDER_TEST_HOOK(3);
	if (!generation || provider.active_generation != generation) {
		poison();
		return SMM_INVOCATION_TRY_ERROR;
	}
	if (intel_smm_invocation_adapter_end(&provider.adapter) != CB_SUCCESS) {
		poison();
		return SMM_INVOCATION_TRY_ERROR;
	}
	provider.active_generation = 0;
	__atomic_store_n(&provider.state, PROVIDER_READY, __ATOMIC_RELEASE);
	return SMM_INVOCATION_TRY_SUCCESS;
}
