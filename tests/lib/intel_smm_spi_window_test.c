/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <intelblocks/smm_spi_window.h>

#include "../../src/soc/intel/common/block/smm/smm_spi_window.c"

#define LOCKED_CONTROL (SPI_BIOS_CONTROL_EISS | SPI_BIOS_CONTROL_LOCK_ENABLE | \
	SPI_BIOS_CONTROL_BILD)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

enum trace_operation {
	TRACE_INSMM_ON = 1,
	TRACE_DRAIN,
	TRACE_WPD_ON,
	TRACE_WPD_OFF,
	TRACE_INSMM_OFF,
	TRACE_OPERATION,
};

static uint32_t bios_control;
static uint32_t insmm_status;
static bool policy_enabled;
static bool wpd_on_stuck;
static bool wpd_off_stuck;
static bool insmm_stuck;
static bool insmm_clear_stuck;
static int sync_smi_count;
static enum trace_operation trace[64];
static size_t trace_count;

static void record(enum trace_operation operation)
{
	assert(trace_count < ARRAY_SIZE(trace));
	trace[trace_count++] = operation;
}

uint32_t test_read32p(uintptr_t address)
{
	assert(address == INSMM_STS_ADDRESS);
	return insmm_status;
}

void test_wrmsr(uint32_t index, msr_t value)
{
	assert(index == MSR_SPCL_CHIPSET_USAGE);
	if (value.lo & INSMM_STS_WRITE_ENABLE) {
		record(TRACE_INSMM_ON);
		if (!insmm_stuck)
			insmm_status |= INSMM_STS_WRITE_ENABLE;
	} else {
		record(TRACE_INSMM_OFF);
		if (!insmm_clear_stuck)
			insmm_status &= ~(uint32_t)INSMM_STS_WRITE_ENABLE;
	}
}

void test_udelay(unsigned int usec)
{
	assert(usec == SYNC_SMI_SETTLE_USEC);
}

bool fast_spi_clear_sync_smi_status(void)
{
	record(TRACE_DRAIN);
	if (sync_smi_count > 0) {
		sync_smi_count--;
		return true;
	}
	return false;
}

uint16_t fast_spi_bios_control(void)
{
	return (uint16_t)bios_control;
}

void fast_spi_disable_wp(void)
{
	record(TRACE_WPD_ON);
	if (!wpd_on_stuck)
		bios_control |= SPI_BIOS_CONTROL_WPD;
}

void fast_spi_enable_wp(void)
{
	record(TRACE_WPD_OFF);
	if (!wpd_off_stuck)
		bios_control &= ~(uint32_t)SPI_BIOS_CONTROL_WPD;
}

bool enable_smm_bios_protection(void)
{
	return policy_enabled;
}

static void initialize(void)
{
	memset(&active_window, 0, sizeof(active_window));
	bios_control = LOCKED_CONTROL;
	insmm_status = 0;
	policy_enabled = true;
	wpd_on_stuck = false;
	wpd_off_stuck = false;
	insmm_stuck = false;
	insmm_clear_stuck = false;
	sync_smi_count = 0;
	memset(trace, 0, sizeof(trace));
	trace_count = 0;
}

static void assert_closed(void)
{
	assert(!(bios_control & SPI_BIOS_CONTROL_WPD));
	assert(!(insmm_status & INSMM_STS_WRITE_ENABLE));
}

static void test_success(void)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	assert(!intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert(intel_smm_spi_window_prove(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context) == 0);
	assert(trace_count >= 3 && trace[0] == TRACE_INSMM_ON &&
		trace[1] == TRACE_DRAIN && trace[2] == TRACE_WPD_ON);
	assert(!intel_smm_spi_window_end(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert(trace_count >= 6 && trace[trace_count - 3] == TRACE_DRAIN &&
		trace[trace_count - 2] == TRACE_WPD_OFF &&
		trace[trace_count - 1] == TRACE_INSMM_OFF);
	assert_closed();
	assert(!memcmp(&window, &(const struct intel_smm_spi_window){ 0 },
		sizeof(window)));
}

static void test_idle_proof(void)
{
	assert(intel_smm_spi_writes_restricted());
	bios_control |= SPI_BIOS_CONTROL_WPD;
	assert(!intel_smm_spi_writes_restricted());
	bios_control = LOCKED_CONTROL;
	insmm_status = INSMM_STS_WRITE_ENABLE;
	assert(!intel_smm_spi_writes_restricted());
}

static void test_reentry(void)
{
	struct intel_smm_spi_window first = { 0 }, second = { 0 };
	uint32_t context = 0;

	assert(!intel_smm_spi_window_begin(&first,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert(intel_smm_spi_window_begin(&second,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert(!intel_smm_spi_window_end(&first,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert_closed();
}

static void test_mutation(bool mutate_token)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0, other = 0;

	assert(!intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	if (mutate_token)
		window.private_data[1]++;
	assert(intel_smm_spi_window_prove(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR,
		mutate_token ? (const void *)&context : (const void *)&other));
	assert(intel_smm_spi_window_end(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert_closed();
}

static void test_policy_mutation(bool initial_policy)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	policy_enabled = initial_policy;
	if (!initial_policy)
		bios_control |= SPI_BIOS_CONTROL_WPD;
	assert(!intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_LEGACY_SMMSTORE, &context));
	policy_enabled = !initial_policy;
	assert(intel_smm_spi_window_prove(&window,
		INTEL_SMM_SPI_WINDOW_LEGACY_SMMSTORE, &context));
	assert(intel_smm_spi_window_end(&window,
		INTEL_SMM_SPI_WINDOW_LEGACY_SMMSTORE, &context));
	assert_closed();
}

static void test_option_policy_transition(bool initial_policy)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	policy_enabled = initial_policy;
	if (!initial_policy)
		bios_control |= SPI_BIOS_CONTROL_WPD;
	assert(!intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_OPTION_STORE, &context));
	assert(!intel_smm_spi_window_prove(&window,
		INTEL_SMM_SPI_WINDOW_OPTION_STORE, &context));
	policy_enabled = !initial_policy;
	assert(!intel_smm_spi_window_end(&window,
		INTEL_SMM_SPI_WINDOW_OPTION_STORE, &context));
	assert_closed();
}

static void test_active_restore(void)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	assert(!intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert(intel_smm_spi_window_restore_ro());
	assert(intel_smm_spi_window_prove(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context) == 0);
	assert(!intel_smm_spi_window_end(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert_closed();
}

static void test_restore_interleaving(bool beginning)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	if (beginning) {
		active_window.state = WINDOW_BEGINNING;
		assert(intel_smm_spi_window_restore_ro());
		assert(trace_count == 0 && active_window.state == WINDOW_BEGINNING);
		active_window.state = WINDOW_IDLE;
	} else {
		active_window.state = WINDOW_RESTORING;
		assert(intel_smm_spi_window_begin(&window,
			INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
		assert(trace_count == 0 && active_window.state == WINDOW_RESTORING);
	}
}

static void test_restore_failure_poison(void)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	bios_control |= SPI_BIOS_CONTROL_WPD;
	wpd_off_stuck = true;
	assert(intel_smm_spi_window_restore_ro());
	assert(active_window.state == WINDOW_POISONED);
	assert(intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
}

static int run_operation(void *context)
{
	const int result = *(const int *)context;

	record(TRACE_OPERATION);
	return result;
}

static void test_consumer_run(bool option_owner, bool operation_failure)
{
	uint32_t context = 0;
	int operation_result = operation_failure ? 7 : 3;
	const enum intel_smm_spi_window_owner owner = option_owner ?
		INTEL_SMM_SPI_WINDOW_OPTION_STORE :
		INTEL_SMM_SPI_WINDOW_LEGACY_SMMSTORE;

	assert(intel_smm_spi_window_run(owner, &context, run_operation,
		&operation_result, -1) == operation_result);
	assert(trace_count == 7 && trace[0] == TRACE_INSMM_ON &&
		trace[1] == TRACE_DRAIN && trace[2] == TRACE_WPD_ON &&
		trace[3] == TRACE_OPERATION && trace[4] == TRACE_DRAIN &&
		trace[5] == TRACE_WPD_OFF && trace[6] == TRACE_INSMM_OFF);
	assert_closed();
}

static void test_policy_off(void)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	policy_enabled = false;
	assert(intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert_closed();

}

static void test_legacy_policy_off(void)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	policy_enabled = false;
	assert(!intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_LEGACY_SMMSTORE, &context));
	assert((bios_control & SPI_BIOS_CONTROL_WPD) &&
		(insmm_status & INSMM_STS_WRITE_ENABLE));
	assert(!intel_smm_spi_window_prove(&window,
		INTEL_SMM_SPI_WINDOW_LEGACY_SMMSTORE, &context));
	assert(!intel_smm_spi_window_end(&window,
		INTEL_SMM_SPI_WINDOW_LEGACY_SMMSTORE, &context));
	assert_closed();
}

static void test_legacy_policy_off_writable(void)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	policy_enabled = false;
	bios_control |= SPI_BIOS_CONTROL_WPD;
	assert(!intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_LEGACY_SMMSTORE, &context));
	assert((bios_control & SPI_BIOS_CONTROL_WPD) &&
		!(insmm_status & INSMM_STS_WRITE_ENABLE));
	assert(!intel_smm_spi_window_prove(&window,
		INTEL_SMM_SPI_WINDOW_LEGACY_SMMSTORE, &context));
	assert(!intel_smm_spi_window_end(&window,
		INTEL_SMM_SPI_WINDOW_LEGACY_SMMSTORE, &context));
	assert((bios_control & SPI_BIOS_CONTROL_WPD) &&
		!(insmm_status & INSMM_STS_WRITE_ENABLE));
	assert(trace_count == 2 && trace[0] == TRACE_DRAIN &&
		trace[1] == TRACE_INSMM_OFF);
}

static void test_active_hardware_mutation(bool drop_wpd)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	assert(!intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	if (drop_wpd)
		bios_control &= ~(uint32_t)SPI_BIOS_CONTROL_WPD;
	else
		bios_control &= ~(uint32_t)SPI_BIOS_CONTROL_LOCK_ENABLE;
	assert(intel_smm_spi_window_prove(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert(intel_smm_spi_window_end(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert_closed();
}

static void test_wrong_ownership(bool copied)
{
	struct intel_smm_spi_window window = { 0 }, other = { 0 };
	uint32_t context = 0;

	assert(!intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	if (copied) {
		other = window;
		assert(intel_smm_spi_window_prove(&other,
			INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	} else {
		assert(intel_smm_spi_window_prove(&window,
			INTEL_SMM_SPI_WINDOW_LEGACY_SMMSTORE, &context));
	}
	assert(intel_smm_spi_window_end(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert_closed();
}

static void test_invalid_input(void)
{
	struct intel_smm_spi_window window = { .private_data = { 1 } };
	uint32_t context = 0;

	assert(intel_smm_spi_window_begin(NULL,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert(intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	memset(&window, 0, sizeof(window));
	assert(intel_smm_spi_window_begin(&window,
		(enum intel_smm_spi_window_owner)0, &context));
	assert(intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, NULL));
	assert_closed();
}

static void test_bad_lock(void)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	bios_control &= ~(uint32_t)SPI_BIOS_CONTROL_EISS;
	assert(intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert_closed();
}

static void test_open_readback(void)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	wpd_on_stuck = true;
	assert(intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert_closed();
}

static void test_close_readback(void)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	assert(!intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	wpd_off_stuck = true;
	assert(intel_smm_spi_window_end(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert(bios_control & SPI_BIOS_CONTROL_WPD);
	assert(!(insmm_status & INSMM_STS_WRITE_ENABLE));
}

static void test_sync_stuck(void)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	sync_smi_count = SYNC_SMI_CLEAR_TRIES + 1;
	assert(intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert_closed();
}

static void test_open_insmm_stuck(void)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	insmm_stuck = true;
	assert(intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert_closed();
}

static void test_close_failure(bool drain, bool insmm)
{
	struct intel_smm_spi_window window = { 0 };
	uint32_t context = 0;

	assert(!intel_smm_spi_window_begin(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	if (drain)
		sync_smi_count = SYNC_SMI_CLEAR_TRIES + 1;
	if (insmm)
		insmm_clear_stuck = true;
	assert(intel_smm_spi_window_end(&window,
		INTEL_SMM_SPI_WINDOW_AUTHVAR, &context));
	assert(!(bios_control & SPI_BIOS_CONTROL_WPD));
	assert(!!(insmm_status & INSMM_STS_WRITE_ENABLE) == insmm);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	initialize();
	if (!strcmp(argv[1], "success"))
		test_success();
	else if (!strcmp(argv[1], "idle-proof"))
		test_idle_proof();
	else if (!strcmp(argv[1], "reentry"))
		test_reentry();
	else if (!strcmp(argv[1], "token-mutation"))
		test_mutation(true);
	else if (!strcmp(argv[1], "context-mutation"))
		test_mutation(false);
	else if (!strcmp(argv[1], "policy-true-to-false"))
		test_policy_mutation(true);
	else if (!strcmp(argv[1], "policy-false-to-true"))
		test_policy_mutation(false);
	else if (!strcmp(argv[1], "option-false-to-true"))
		test_option_policy_transition(false);
	else if (!strcmp(argv[1], "option-true-to-false"))
		test_option_policy_transition(true);
	else if (!strcmp(argv[1], "active-restore"))
		test_active_restore();
	else if (!strcmp(argv[1], "beginning-restore"))
		test_restore_interleaving(true);
	else if (!strcmp(argv[1], "restoring-begin"))
		test_restore_interleaving(false);
	else if (!strcmp(argv[1], "restore-failure-poison"))
		test_restore_failure_poison();
	else if (!strcmp(argv[1], "legacy-run-success"))
		test_consumer_run(false, false);
	else if (!strcmp(argv[1], "legacy-run-failure"))
		test_consumer_run(false, true);
	else if (!strcmp(argv[1], "option-run-success"))
		test_consumer_run(true, false);
	else if (!strcmp(argv[1], "option-run-failure"))
		test_consumer_run(true, true);
	else if (!strcmp(argv[1], "policy-off"))
		test_policy_off();
	else if (!strcmp(argv[1], "legacy-policy-off"))
		test_legacy_policy_off();
	else if (!strcmp(argv[1], "legacy-policy-off-writable"))
		test_legacy_policy_off_writable();
	else if (!strcmp(argv[1], "drop-wpd"))
		test_active_hardware_mutation(true);
	else if (!strcmp(argv[1], "drop-lock"))
		test_active_hardware_mutation(false);
	else if (!strcmp(argv[1], "wrong-owner"))
		test_wrong_ownership(false);
	else if (!strcmp(argv[1], "copied-token"))
		test_wrong_ownership(true);
	else if (!strcmp(argv[1], "invalid-input"))
		test_invalid_input();
	else if (!strcmp(argv[1], "bad-lock"))
		test_bad_lock();
	else if (!strcmp(argv[1], "open-readback"))
		test_open_readback();
	else if (!strcmp(argv[1], "close-readback"))
		test_close_readback();
	else if (!strcmp(argv[1], "sync-stuck"))
		test_sync_stuck();
	else if (!strcmp(argv[1], "open-insmm-stuck"))
		test_open_insmm_stuck();
	else if (!strcmp(argv[1], "close-drain-stuck"))
		test_close_failure(true, false);
	else if (!strcmp(argv[1], "close-insmm-stuck"))
		test_close_failure(false, true);
	else
		abort();
	return 0;
}
