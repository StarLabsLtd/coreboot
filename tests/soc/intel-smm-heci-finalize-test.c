/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <cpu/x86/save_state.h>
#include <cpu/x86/smm.h>
#include <device/pci_def.h>
#include <device/pci_ids.h>
#include <intelblocks/p2sb.h>
#include <intelblocks/pcr.h>
#include <intelblocks/smihandler.h>
#include <soc/p2sb.h>
#include <stdbool.h>
#include <string.h>
#include <sys/wait.h>
#include <tests/test.h>
#include <unistd.h>

#define PCH_P2SB_EPMASK(mask_number) (PCH_P2SB_EPMASK0 + ((mask_number) * 4))

enum failure_mode {
	NO_FAILURE,
	MASK_WRITE_FAILURE,
	MASK_LOCK_FAILURE,
	P2SB_UNAVAILABLE,
};

static enum failure_mode failure_mode;
static uint32_t epmask[P2SB_EP_MASK_MAX_REG];
static uint8_t endpoint_control;
static uint32_t caller_result;
static unsigned int hide_calls;
static unsigned int sbi_calls;

uint8_t pci_s_read_config8(pci_devfn_t dev, uint16_t reg)
{
	return reg == PCH_P2SB_E0 + 2 ? endpoint_control : 0;
}

uint16_t pci_s_read_config16(pci_devfn_t dev, uint16_t reg)
{
	if (reg == PCI_VENDOR_ID)
		return failure_mode == P2SB_UNAVAILABLE ? 0xffff : PCI_VID_INTEL;
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
	if (reg == PCH_P2SB_E0 + 2 && failure_mode != MASK_LOCK_FAILURE)
		endpoint_control = value;
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
	hide_calls++;
	return true;
}

int pcr_execute_sideband_msg(pci_devfn_t dev, struct pcr_sbi_msg *msg, uint32_t *data,
			     uint8_t *response)
{
	sbi_calls++;
	*response = 0;
	return 0;
}

bool soc_disable_heci1_at_pre_boot(void)
{
	return true;
}

int mainboard_smi_finalize(void)
{
	return 0;
}

bool enable_smm_bios_protection(void)
{
	return false;
}

bool opal_s3_smi_apmc(uint8_t command)
{
	return true;
}

uint8_t apm_get_apmc(void)
{
	return APM_CNT_FINALIZE;
}

void pmc_disable_pm1_control(uint32_t mask)
{
}

void pmc_enable_pm1_control(uint32_t mask)
{
}

int mainboard_smi_apmc(uint8_t command)
{
	return 0;
}

void post_code(uint8_t value)
{
}

static int apmc_node(uint8_t command)
{
	return 0;
}

static int set_reg(enum cpu_reg reg, int node, void *value, uint8_t length)
{
	memcpy(&caller_result, value, sizeof(caller_result));
	return 0;
}

static const struct smm_save_state_ops save_state_ops = {
	.apmc_node = apmc_node,
	.set_reg = set_reg,
};

static int run_scenario(enum failure_mode mode)
{
	failure_mode = mode;
	memset(epmask, 0, sizeof(epmask));
	endpoint_control = 0;
	caller_result = 0x55;
	hide_calls = 0;
	sbi_calls = 0;

	smihandler_southbridge_apmc(&save_state_ops);
	if (caller_result != (mode == NO_FAILURE ? 0 : UINT32_MAX))
		return 1;
	if (sbi_calls != 1 || hide_calls != 1)
		return 2;

	caller_result = 0x55;
	smihandler_southbridge_apmc(&save_state_ops);
	if (caller_result != (mode == NO_FAILURE ? 0 : UINT32_MAX))
		return 3;
	if (sbi_calls != 1 || hide_calls != 1)
		return 4;
	return 0;
}

static void scenario_reaches_finalization_caller(void **state)
{
	for (enum failure_mode mode = NO_FAILURE; mode <= P2SB_UNAVAILABLE; mode++) {
		pid_t child = fork();
		int status;

		assert_true(child >= 0);
		if (!child)
			_exit(run_scenario(mode));
		assert_int_equal(waitpid(child, &status, 0), child);
		assert_true(WIFEXITED(status));
		assert_int_equal(WEXITSTATUS(status), 0);
	}
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(scenario_reaches_finalization_caller),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
