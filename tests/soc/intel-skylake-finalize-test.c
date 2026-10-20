/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/console/post_codes.h>
#include <cpu/x86/smm.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <device/pci_ids.h>
#include <intelblocks/p2sb.h>
#include <setjmp.h>
#include <soc/p2sb.h>
#include <soc/pci_devs.h>
#include <soc/pmc.h>
#include <stdarg.h>
#include <tests/test.h>

#include "../../src/soc/intel/skylake/chip.h"
#include "p2sb-pci-mock.h"

#define PCH_P2SB_EPMASK(mask_number) (PCH_P2SB_EPMASK0 + ((mask_number) * 4))

enum failure_mode {
	NO_FAILURE,
	MASK_WRITE_FAILURE,
	MASK_LOCK_FAILURE,
};

enum event {
	EVENT_TCO_LOCK,
	EVENT_ME_STATUS,
	EVENT_THERMAL,
	EVENT_HECI_DISABLE,
	EVENT_ENDPOINT_LOCK,
	EVENT_P2SB_HIDE,
	EVENT_PM_CLEAR,
	EVENT_MEMORY_LOCK,
	EVENT_SMM_FINALIZE,
	EVENT_DIE,
	EVENT_OS_BOOT,
};

static enum failure_mode failure_mode;
static enum event events[32];
static size_t event_count;
static bool disable_heci;
static uint32_t epmask[P2SB_EP_MASK_MAX_REG];
static uint8_t endpoint_control;
static unsigned int failure_logs;
static unsigned int continuation_logs;
static jmp_buf die_target;
static struct soc_intel_skylake_config config;
static uint8_t pci_config_space[CONFIG_ECAM_MMCONF_LENGTH];
uint8_t *const pci_mmconf = pci_config_space;
static struct bus pmc_bus;
static struct device pmc_device;

void skylake_soc_finalize_for_test(void);

static void record_event(enum event event)
{
	assert_true(event_count < ARRAY_SIZE(events));
	events[event_count++] = event;
}

uint8_t pci_s_read_config8(pci_devfn_t dev, uint16_t reg)
{
	if (reg == PCH_P2SB_E0 + 2)
		return endpoint_control;
	return 0;
}

uint16_t pci_s_read_config16(pci_devfn_t dev, uint16_t reg)
{
	if (reg == PCI_VENDOR_ID)
		return PCI_VID_INTEL;
	return 0;
}

uint32_t pci_s_read_config32(pci_devfn_t dev, uint16_t reg)
{
	if (reg >= PCH_P2SB_EPMASK(0) && reg <= PCH_P2SB_EPMASK(P2SB_EP_MASK_MAX_REG - 1))
		return epmask[(reg - PCH_P2SB_EPMASK(0)) / sizeof(uint32_t)];
	return 0;
}

void pci_s_write_config8(pci_devfn_t dev, uint16_t reg, uint8_t value)
{
	if (reg == PCH_P2SB_E0 + 2) {
		record_event(EVENT_ENDPOINT_LOCK);
		if (failure_mode != MASK_LOCK_FAILURE)
			endpoint_control = value;
	}
}

void pci_s_write_config16(pci_devfn_t dev, uint16_t reg, uint16_t value)
{
}

void pci_s_write_config32(pci_devfn_t dev, uint16_t reg, uint32_t value)
{
	int index = (reg - PCH_P2SB_EPMASK(0)) / sizeof(uint32_t);

	if (failure_mode != MASK_WRITE_FAILURE || index != 1)
		epmask[index] = value;
}

struct device *pcidev_path_on_root_debug(pci_devfn_t devfn, const char *func)
{
	return &pmc_device;
}

void devtree_die(void)
{
	fail_msg("invalid test device");
	longjmp(die_target, 1);
}

unsigned int get_uint_option(const char *name, unsigned int fallback)
{
	return disable_heci;
}

void tco_lockdown(void)
{
	record_event(EVENT_TCO_LOCK);
}

void intel_me_status(void)
{
	record_event(EVENT_ME_STATUS);
}

void pch_thermal_configuration(void)
{
	record_event(EVENT_THERMAL);
}

void pcr_or32(uint8_t pid, uint16_t offset, uint32_t ordata)
{
	record_event(EVENT_HECI_DISABLE);
}

void p2sb_soc_get_sb_mask(uint32_t *mask, size_t count)
{
	mask[0] = BIT(3);
	mask[1] = BIT(7);
}

bool p2sb_dev_unhide(pci_devfn_t dev)
{
	return true;
}

bool p2sb_dev_hide(pci_devfn_t dev)
{
	record_event(EVENT_P2SB_HIDE);
	return true;
}

void pmc_clear_pmcon_sts(void)
{
	record_event(EVENT_PM_CLEAR);
}

void cpu_lt_lock_memory(void)
{
	record_event(EVENT_MEMORY_LOCK);
}

int printk(int msg_level, const char *fmt, ...)
{
	if (!strcmp(fmt, "Chipset finalization failed.\n"))
		failure_logs++;
	else if (!strcmp(fmt, "Continuing because fail-closed policy is disabled.\n"))
		continuation_logs++;
	return 0;
}

int vprintk(int msg_level, const char *fmt, va_list args)
{
	return 0;
}

int console_log_level(int msg_level)
{
	return 0;
}

enum cb_err apm_control(uint8_t cmd)
{
	assert_int_equal(cmd, APM_CNT_FINALIZE);
	record_event(EVENT_SMM_FINALIZE);
	return CB_SUCCESS;
}

void post_code(uint8_t value)
{
	assert_int_equal(value, POSTCODE_OS_BOOT);
	record_event(EVENT_OS_BOOT);
}

void die(const char *fmt, ...)
{
	record_event(EVENT_DIE);
	longjmp(die_target, 1);
}

static int event_index(enum event event)
{
	for (size_t i = 0; i < event_count; i++)
		if (events[i] == event)
			return i;
	return -1;
}

static void reset_scenario(enum failure_mode mode, bool request_disable)
{
	failure_mode = mode;
	disable_heci = request_disable;
	event_count = 0;
	memset(epmask, 0, sizeof(epmask));
	endpoint_control = 0;
	failure_logs = 0;
	continuation_logs = 0;
	memset(&config, 0, sizeof(config));
	memset(&pmc_bus, 0, sizeof(pmc_bus));
	memset(&pmc_device, 0, sizeof(pmc_device));
	pmc_device.chip_info = &config;
	pmc_device.upstream = &pmc_bus;
}

static void run_finalize(bool expect_die)
{
	if (expect_die) {
		if (!setjmp(die_target)) {
			skylake_soc_finalize_for_test();
			fail_msg("fail-closed finalization returned");
		}
	} else {
		skylake_soc_finalize_for_test();
	}
}

static void successful_lockdown_reaches_os_boot(void **state)
{
	reset_scenario(NO_FAILURE, true);
	run_finalize(false);
	assert_true(event_index(EVENT_HECI_DISABLE) >= 0);
	assert_true(event_index(EVENT_ENDPOINT_LOCK) > event_index(EVENT_HECI_DISABLE));
	assert_true(event_index(EVENT_P2SB_HIDE) > event_index(EVENT_ENDPOINT_LOCK));
	assert_true(event_index(EVENT_MEMORY_LOCK) > event_index(EVENT_PM_CLEAR));
	assert_true(event_index(EVENT_SMM_FINALIZE) > event_index(EVENT_PM_CLEAR));
	assert_true(event_index(EVENT_SMM_FINALIZE) > event_index(EVENT_MEMORY_LOCK));
	assert_true(event_index(EVENT_OS_BOOT) > event_index(EVENT_SMM_FINALIZE));
	assert_int_equal(failure_logs, 0);
	assert_int_equal(continuation_logs, 0);
}

static void endpoint_failures_reach_policy_boundary(void **state)
{
	for (enum failure_mode mode = MASK_WRITE_FAILURE; mode <= MASK_LOCK_FAILURE; mode++) {
		reset_scenario(mode, true);
		run_finalize(CONFIG(SECURITY_FAIL_CLOSED));

		assert_true(event_index(EVENT_P2SB_HIDE) > event_index(EVENT_ENDPOINT_LOCK));
		assert_true(event_index(EVENT_PM_CLEAR) > event_index(EVENT_P2SB_HIDE));
		assert_true(event_index(EVENT_MEMORY_LOCK) > event_index(EVENT_PM_CLEAR));
		assert_true(event_index(EVENT_SMM_FINALIZE) > event_index(EVENT_PM_CLEAR));
		assert_int_equal(failure_logs, 1);
		if (CONFIG(SECURITY_FAIL_CLOSED)) {
			assert_true(event_index(EVENT_DIE) > event_index(EVENT_SMM_FINALIZE));
			assert_int_equal(event_index(EVENT_OS_BOOT), -1);
			assert_int_equal(continuation_logs, 0);
		} else {
			assert_int_equal(event_index(EVENT_DIE), -1);
			assert_true(event_index(EVENT_OS_BOOT) >
				    event_index(EVENT_SMM_FINALIZE));
			assert_int_equal(continuation_logs, 1);
		}
	}
}

static void no_disable_request_skips_endpoint_lockdown(void **state)
{
	reset_scenario(NO_FAILURE, false);
	run_finalize(false);
	assert_int_equal(event_index(EVENT_HECI_DISABLE), -1);
	assert_int_equal(event_index(EVENT_ENDPOINT_LOCK), -1);
	assert_true(event_index(EVENT_P2SB_HIDE) >= 0);
	assert_true(event_index(EVENT_SMM_FINALIZE) > event_index(EVENT_P2SB_HIDE));
	assert_true(event_index(EVENT_OS_BOOT) > event_index(EVENT_SMM_FINALIZE));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(successful_lockdown_reaches_os_boot),
		cmocka_unit_test(endpoint_failures_reach_policy_boundary),
		cmocka_unit_test(no_disable_request_skips_endpoint_lockdown),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
