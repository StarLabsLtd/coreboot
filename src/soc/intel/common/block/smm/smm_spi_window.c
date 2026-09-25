/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/intel/msr.h>
#include <cpu/x86/msr.h>
#include <delay.h>
#include <device/mmio.h>
#include <intelblocks/fast_spi.h>
#include <intelblocks/smm_spi_window.h>
#include <security/lockdown/lockdown.h>
#include <string.h>

#include "../fast_spi/fast_spi_def.h"

#define SYNC_SMI_SETTLE_USEC 50
#define SYNC_SMI_CLEAR_TRIES 8
#define INSMM_STS_ADDRESS 0xfed30880
#define INSMM_STS_WRITE_ENABLE 1U
#define WINDOW_COOKIE 0x57494e44U

enum window_state {
	WINDOW_IDLE = 0,
	WINDOW_BEGINNING,
	WINDOW_ACTIVE,
	WINDOW_ENDING,
	WINDOW_RESTORING,
	WINDOW_POISONED,
};

static struct {
	struct intel_smm_spi_window *token;
	const void *context;
	uintptr_t generation;
	uint32_t owner;
	uint32_t state;
	uint32_t policy_enabled;
	uint32_t initial_wpd;
	uint32_t opened;
} active_window;

static bool owner_recognized(enum intel_smm_spi_window_owner owner)
{
	return owner == INTEL_SMM_SPI_WINDOW_LEGACY_SMMSTORE ||
		owner == INTEL_SMM_SPI_WINDOW_AUTHVAR ||
		owner == INTEL_SMM_SPI_WINDOW_OPTION_STORE;
}

static bool set_insmm_sts(bool enable)
{
	msr_t msr = { .lo = read32p(INSMM_STS_ADDRESS), .hi = 0 };

	if (enable)
		msr.lo |= INSMM_STS_WRITE_ENABLE;
	else
		msr.lo &= ~INSMM_STS_WRITE_ENABLE;
	wrmsr(MSR_SPCL_CHIPSET_USAGE, msr);
	return !!(read32p(INSMM_STS_ADDRESS) & INSMM_STS_WRITE_ENABLE) == enable;
}

static bool drain_sync_smi(void)
{
	int i;

	for (i = 0; i < SYNC_SMI_CLEAR_TRIES; i++) {
		if (!fast_spi_clear_sync_smi_status())
			return true;
		udelay(SYNC_SMI_SETTLE_USEC);
	}
	return false;
}

static bool protected_idle(uint16_t control)
{
	const uint32_t locked = SPI_BIOS_CONTROL_EISS |
		SPI_BIOS_CONTROL_LOCK_ENABLE | SPI_BIOS_CONTROL_BILD;

	return (control & (locked | SPI_BIOS_CONTROL_WPD)) == locked;
}

static bool protected_active(uint16_t control)
{
	const uint32_t required = SPI_BIOS_CONTROL_EISS |
		SPI_BIOS_CONTROL_LOCK_ENABLE | SPI_BIOS_CONTROL_BILD |
		SPI_BIOS_CONTROL_WPD;

	return (control & required) == required &&
		(read32p(INSMM_STS_ADDRESS) & INSMM_STS_WRITE_ENABLE);
}

static bool write_active(uint16_t control)
{
	return (control & SPI_BIOS_CONTROL_WPD) &&
		(read32p(INSMM_STS_ADDRESS) & INSMM_STS_WRITE_ENABLE);
}

static void token_build(struct intel_smm_spi_window *window,
	enum intel_smm_spi_window_owner owner, const void *context)
{
	window->private_data[0] = WINDOW_COOKIE;
	window->private_data[1] = active_window.generation;
	window->private_data[2] = (uintptr_t)context;
	window->private_data[3] = (uintptr_t)owner ^ active_window.generation ^
		WINDOW_COOKIE;
}

static bool token_valid(const struct intel_smm_spi_window *window,
	enum intel_smm_spi_window_owner owner, const void *context)
{
	return window && active_window.token == window &&
		active_window.context == context && active_window.owner == owner &&
		window->private_data[0] == WINDOW_COOKIE &&
		window->private_data[1] == active_window.generation &&
		window->private_data[2] == (uintptr_t)context &&
		window->private_data[3] == ((uintptr_t)owner ^
			active_window.generation ^ WINDOW_COOKIE);
}

int intel_smm_spi_window_restore_ro(void)
{
	const bool policy_enabled = enable_smm_bios_protection();
	uint32_t expected = WINDOW_IDLE;
	bool ok;

	if (!__atomic_compare_exchange_n(&active_window.state, &expected,
		WINDOW_RESTORING, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
		return -1;
	ok = drain_sync_smi();
	fast_spi_enable_wp();
	if (policy_enabled)
		ok &= protected_idle(fast_spi_bios_control());
	else
		ok &= !(fast_spi_bios_control() & SPI_BIOS_CONTROL_WPD);
	ok &= set_insmm_sts(false);
	__atomic_store_n(&active_window.state,
		ok ? WINDOW_IDLE : WINDOW_POISONED, __ATOMIC_RELEASE);
	return ok ? 0 : -1;
}

bool intel_smm_spi_writes_restricted(void)
{
	return enable_smm_bios_protection() &&
		__atomic_load_n(&active_window.state, __ATOMIC_ACQUIRE) ==
		WINDOW_IDLE && protected_idle(fast_spi_bios_control()) &&
		!(read32p(INSMM_STS_ADDRESS) & INSMM_STS_WRITE_ENABLE);
}

static bool close_active_window(void)
{
	const bool live_policy = enable_smm_bios_protection();
	const bool force_ro = active_window.policy_enabled || live_policy ||
		!active_window.initial_wpd;
	bool ok = true;
	uint16_t control;

	ok &= drain_sync_smi();
	if (force_ro)
		fast_spi_enable_wp();
	control = fast_spi_bios_control();
	if (force_ro)
		ok &= !(control & SPI_BIOS_CONTROL_WPD);
	else
		ok &= !!(control & SPI_BIOS_CONTROL_WPD);
	if (active_window.policy_enabled)
		ok &= (control & (SPI_BIOS_CONTROL_EISS |
			SPI_BIOS_CONTROL_LOCK_ENABLE | SPI_BIOS_CONTROL_BILD)) ==
			(SPI_BIOS_CONTROL_EISS | SPI_BIOS_CONTROL_LOCK_ENABLE |
			 SPI_BIOS_CONTROL_BILD);
	ok &= set_insmm_sts(false);
	active_window.opened = 0;
	return ok;
}

int intel_smm_spi_window_begin(struct intel_smm_spi_window *window,
	enum intel_smm_spi_window_owner owner, const void *context)
{
	uint32_t expected = WINDOW_IDLE;
	bool policy_enabled;

	if (!window || !context || !owner_recognized(owner) ||
		memcmp(window, &(const struct intel_smm_spi_window){ 0 },
			sizeof(*window)) ||
		!__atomic_compare_exchange_n(&active_window.state, &expected,
			WINDOW_BEGINNING, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
		return -1;
	active_window.token = window;
	active_window.context = context;
	active_window.owner = owner;
	active_window.generation++;
	if (!active_window.generation)
		active_window.generation++;
	token_build(window, owner, context);
	active_window.initial_wpd = !!(fast_spi_bios_control() &
		SPI_BIOS_CONTROL_WPD);
	policy_enabled = enable_smm_bios_protection();
	active_window.policy_enabled = policy_enabled;
	if (owner == INTEL_SMM_SPI_WINDOW_AUTHVAR && !policy_enabled)
		goto fail;
	if (policy_enabled) {
		if (active_window.initial_wpd) {
			active_window.initial_wpd = 0;
			goto fail;
		}
		if (!protected_idle(fast_spi_bios_control()) ||
		    !set_insmm_sts(true) || !drain_sync_smi())
			goto fail;
		fast_spi_disable_wp();
		active_window.opened = 1;
		if (!protected_active(fast_spi_bios_control()))
			goto fail;
	} else if (!active_window.initial_wpd) {
		if (!set_insmm_sts(true) || !drain_sync_smi())
			goto fail;
		fast_spi_disable_wp();
		active_window.opened = 1;
		if (!write_active(fast_spi_bios_control()))
			goto fail;
	} else if (!(fast_spi_bios_control() & SPI_BIOS_CONTROL_WPD) ||
		   (read32p(INSMM_STS_ADDRESS) & INSMM_STS_WRITE_ENABLE)) {
		goto fail;
	}
	__atomic_store_n(&active_window.state, WINDOW_ACTIVE, __ATOMIC_RELEASE);
	return 0;

fail:
	if (active_window.token == window)
		(void)close_active_window();
	memset(window, 0, sizeof(*window));
	active_window.token = NULL;
	active_window.context = NULL;
	active_window.owner = 0;
	__atomic_store_n(&active_window.state, WINDOW_POISONED, __ATOMIC_RELEASE);
	return -1;
}

int intel_smm_spi_window_prove(const struct intel_smm_spi_window *window,
	enum intel_smm_spi_window_owner owner, const void *context)
{
	const bool policy_enabled = enable_smm_bios_protection();

	if (__atomic_load_n(&active_window.state, __ATOMIC_ACQUIRE) !=
		WINDOW_ACTIVE || !token_valid(window, owner, context) ||
		policy_enabled != !!active_window.policy_enabled ||
		(owner == INTEL_SMM_SPI_WINDOW_AUTHVAR && !policy_enabled) ||
		(policy_enabled && !protected_active(fast_spi_bios_control())) ||
		(!policy_enabled && active_window.opened &&
		 !write_active(fast_spi_bios_control())) ||
		(!policy_enabled && !active_window.opened &&
		 (!(fast_spi_bios_control() & SPI_BIOS_CONTROL_WPD) ||
		  (read32p(INSMM_STS_ADDRESS) & INSMM_STS_WRITE_ENABLE)))) {
		__atomic_store_n(&active_window.state, WINDOW_POISONED,
			__ATOMIC_RELEASE);
		return -1;
	}
	return 0;
}

int intel_smm_spi_window_end(struct intel_smm_spi_window *window,
	enum intel_smm_spi_window_owner owner, const void *context)
{
	const bool owned = window && active_window.token == window;
	bool valid;
	int result;

	if (!owned)
		return -1;
	valid = token_valid(window, owner, context) &&
		__atomic_load_n(&active_window.state, __ATOMIC_ACQUIRE) ==
		WINDOW_ACTIVE;
	if (valid && enable_smm_bios_protection() !=
	    !!active_window.policy_enabled &&
	    owner != INTEL_SMM_SPI_WINDOW_OPTION_STORE)
		valid = false;
	__atomic_store_n(&active_window.state, WINDOW_ENDING, __ATOMIC_RELEASE);
	result = close_active_window() ? 0 : -1;
	memset(window, 0, sizeof(*window));
	active_window.token = NULL;
	active_window.context = NULL;
	active_window.owner = 0;
	active_window.policy_enabled = 0;
	active_window.initial_wpd = 0;
	active_window.opened = 0;
	__atomic_store_n(&active_window.state,
		valid && !result ? WINDOW_IDLE : WINDOW_POISONED, __ATOMIC_RELEASE);
	return valid && !result ? 0 : -1;
}

int intel_smm_spi_window_run(enum intel_smm_spi_window_owner owner,
	const void *owner_context, intel_smm_spi_window_operation operation,
	void *operation_context, int failure_result)
{
	struct intel_smm_spi_window window = { 0 };
	int result = failure_result;

	if (!operation || intel_smm_spi_window_begin(&window, owner,
		owner_context))
		return failure_result;
	if (!intel_smm_spi_window_prove(&window, owner, owner_context))
		result = operation(operation_context);
	if (intel_smm_spi_window_end(&window, owner, owner_context))
		result = failure_result;
	return result;
}
