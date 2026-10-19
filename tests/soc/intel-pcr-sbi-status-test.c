/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pci_def.h>
#include <device/pci_ids.h>
#include <intelblocks/pcr.h>
#include <tests/test.h>

#define P2SB_CR_SBI_DATA                 0xd4
#define P2SB_CR_SBI_STATUS               0xd8
#define P2SB_CR_SBI_STATUS_BUSY          1
#define P2SB_CR_SBI_STATUS_NOT_SUPPORTED (1 << 1)

static uint16_t sbi_status;
static bool command_started;

uint8_t pci_s_read_config8(pci_devfn_t dev, uint16_t reg)
{
	return 0;
}

uint16_t pci_s_read_config16(pci_devfn_t dev, uint16_t reg)
{
	if (reg == PCI_VENDOR_ID)
		return PCI_VID_INTEL;
	if (reg == P2SB_CR_SBI_STATUS)
		return command_started ? 0 : sbi_status;
	return 0;
}

uint32_t pci_s_read_config32(pci_devfn_t dev, uint16_t reg)
{
	return reg == P2SB_CR_SBI_DATA ? 0x12345678 : 0;
}

void pci_s_write_config8(pci_devfn_t dev, uint16_t reg, uint8_t value)
{
}

void pci_s_write_config16(pci_devfn_t dev, uint16_t reg, uint16_t value)
{
	if (reg == P2SB_CR_SBI_STATUS && (value & P2SB_CR_SBI_STATUS_BUSY))
		command_started = true;
}

void pci_s_write_config32(pci_devfn_t dev, uint16_t reg, uint32_t value)
{
}

static void completed_status_determines_result(void **state)
{
	struct pcr_sbi_msg msg = {
		.pid = 1,
		.offset = 0,
		.opcode = PCR_READ,
		.fast_byte_enable = 0xf,
	};
	uint32_t data = 0;
	uint8_t response = 0xff;

	sbi_status = P2SB_CR_SBI_STATUS_NOT_SUPPORTED;
	command_started = false;
	assert_int_equal(pcr_execute_sideband_msg(0, &msg, &data, &response), 0);
	assert_int_equal(response, 0);
	assert_int_equal(data, 0x12345678);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(completed_status_determines_result),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
