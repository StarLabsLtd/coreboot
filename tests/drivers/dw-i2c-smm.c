/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <device/pci.h>
#include <device/pci_def.h>
#include <drivers/i2c/designware/dw_i2c.h>
#include <intelblocks/lpss.h>
#include <tests/test.h>

static union pci_bank config_space;
u8 *const pci_mmconf = (u8 *)&config_space;
static enum lpss_pwr_state power_state;
static int bus_devfn;

void dw_i2c_smm_reset_for_test(void);

int dw_i2c_soc_bus_to_devfn(unsigned int bus)
{
	return bus_devfn;
}

bool smm_region_overlaps_handler(const struct region *r)
{
	return false;
}

enum lpss_pwr_state lpss_get_power_state(pci_devfn_t devfn)
{
	return power_state;
}

static int setup(void **state)
{
	memset(&config_space, 0, sizeof(config_space));
	bus_devfn = PCI_DEVFN(0, 0);
	power_state = STATE_D3;
	dw_i2c_smm_reset_for_test();
	return 0;
}

static void failed_initialization_is_explicit_and_not_retried(void **state)
{
	config_space.reg32[PCI_BASE_ADDRESS_0 / sizeof(uint32_t)] =
		PCI_BASE_ADDRESS_SPACE_IO;
	assert_int_equal(dw_i2c_smm_init(0), CB_ERR);

	config_space.reg32[PCI_BASE_ADDRESS_0 / sizeof(uint32_t)] = 0xfe000000;
	assert_int_equal(dw_i2c_smm_init(0), CB_ERR);
	assert_false(dw_i2c_smm_bus_is_runtime_suspended(0));
}

static void changed_bar_and_disabled_decoding_are_rejected(void **state)
{
	config_space.reg32[PCI_BASE_ADDRESS_0 / sizeof(uint32_t)] = 0xfe000000;
	config_space.reg16[PCI_COMMAND / sizeof(uint16_t)] = PCI_COMMAND_MEMORY;
	assert_int_equal(dw_i2c_smm_init(0), CB_SUCCESS);
	assert_true(dw_i2c_smm_bus_is_runtime_suspended(0));

	config_space.reg32[PCI_BASE_ADDRESS_0 / sizeof(uint32_t)] = 0xfe001000;
	assert_false(dw_i2c_smm_bus_is_runtime_suspended(0));

	config_space.reg32[PCI_BASE_ADDRESS_0 / sizeof(uint32_t)] = 0xfe000000;
	config_space.reg16[PCI_COMMAND / sizeof(uint16_t)] = 0;
	assert_false(dw_i2c_smm_bus_is_runtime_suspended(0));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(failed_initialization_is_explicit_and_not_retried, setup),
		cmocka_unit_test_setup(changed_bar_and_disabled_decoding_are_rejected, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
