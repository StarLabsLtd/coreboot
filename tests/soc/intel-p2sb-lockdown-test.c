/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <device/pci_def.h>
#include <device/pci_ids.h>
#include <device/pci_type.h>
#include <intelblocks/p2sb.h>
#include <soc/p2sb.h>
#include <string.h>
#include <tests/test.h>

static uint32_t epmask[P2SB_EP_MASK_MAX_REG];
static uint8_t endpoint_control;
static bool device_available;
static int ignored_mask;
static bool ignore_lock;

#define PCH_P2SB_EPMASK(mask_number) (PCH_P2SB_EPMASK0 + ((mask_number) * 4))

uint8_t pci_s_read_config8(pci_devfn_t dev, uint16_t reg)
{
	if (reg == PCH_P2SB_E0 + 2)
		return endpoint_control;
	return 0;
}

uint16_t pci_s_read_config16(pci_devfn_t dev, uint16_t reg)
{
	if (reg == PCI_VENDOR_ID)
		return device_available ? PCI_VID_INTEL : 0xffff;
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
	if (reg == PCH_P2SB_E0 + 2 && !ignore_lock)
		endpoint_control = value;
}

void pci_s_write_config16(pci_devfn_t dev, uint16_t reg, uint16_t value)
{
}

void pci_s_write_config32(pci_devfn_t dev, uint16_t reg, uint32_t value)
{
	int index = (reg - PCH_P2SB_EPMASK(0)) / sizeof(uint32_t);

	if (index != ignored_mask)
		epmask[index] = value;
}

void p2sb_soc_get_sb_mask(uint32_t *mask, size_t count)
{
	assert_true(count >= 2);
	mask[0] = BIT(3);
	mask[1] = BIT(7);
}

static int setup(void **state)
{
	memset(epmask, 0, sizeof(epmask));
	endpoint_control = 0;
	device_available = true;
	ignored_mask = -1;
	ignore_lock = false;
	return 0;
}

static void successful_lockdown(void **state)
{
	assert_int_equal(p2sb_disable_sideband_access(), 0);
	assert_true(epmask[0] & BIT(3));
	assert_true(epmask[1] & BIT(7));
	assert_true(endpoint_control & P2SB_E0_MASKLOCK);
}

static void ignored_endpoint_mask_write_fails(void **state)
{
	ignored_mask = 1;
	assert_int_equal(p2sb_disable_sideband_access(), -1);
}

static void ignored_mask_lock_write_fails(void **state)
{
	ignore_lock = true;
	assert_int_equal(p2sb_disable_sideband_access(), -1);
}

static void unavailable_device_fails(void **state)
{
	device_available = false;
	assert_int_equal(p2sb_disable_sideband_access(), -1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(successful_lockdown, setup),
		cmocka_unit_test_setup(ignored_endpoint_mask_write_fails, setup),
		cmocka_unit_test_setup(ignored_mask_lock_write_fails, setup),
		cmocka_unit_test_setup(unavailable_device_fails, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
