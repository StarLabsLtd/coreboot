/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/smn.h>
#include <amdblocks/smu.h>
#include <console/console.h>
#include <device/mmio.h>
#include <device/pci_ops.h>
#include <power_budget.h>
#include <soc/pci_devs.h>
#include <timer.h>

static enum { LIMITS_UNSET, LIMITS_READY, LIMITS_FAILED } limits_state;
static uint32_t native_limits[3];

/* PMF-SMU registers, shared with the Linux amd/pmf interface. */
#define PMF_MESSAGE 0xa18
#define PMF_RESPONSE 0xa78
#define PMF_ARGUMENT 0xa58

static uint32_t pmf_wait(const uint8_t *registers)
{
	struct stopwatch timeout;
	stopwatch_init_usecs_expire(&timeout, 100000);
	uint32_t response;
	do {
		response = read32(registers + PMF_RESPONSE);
		if (response)
			return response;
	} while (!stopwatch_expired(&timeout));
	return 0;
}

static bool pmf_get_limit(uint8_t *registers, uint8_t command, uint32_t *limit)
{
	uint32_t response = pmf_wait(registers);
	if (!response || response == UINT32_MAX)
		return false;

	/* PMF uses one argument, unlike the six-argument BIOS SMU mailbox. */
	write32(registers + PMF_RESPONSE, 0);
	write32(registers + PMF_ARGUMENT, 0);
	write32(registers + PMF_MESSAGE, command);
	if (pmf_wait(registers) != SMU_MESG_RESP_OK)
		return false;
	*limit = read32(registers + PMF_ARGUMENT);
	return *limit >= 20000 && *limit <= 200000;
}

/* Capture once before the payload; never request a metrics-table DMA transfer. */
static bool capture_native_limits(void)
{
	static const uint8_t commands[] = { 0x0b, 0x0f, 0x0d };
	if (limits_state != LIMITS_UNSET)
		return false;
	limits_state = LIMITS_FAILED;

	uint32_t low = smn_read32(0x13b102e8);
	uint32_t high = smn_read32(0x13b102ec);
	uint64_t base = ((uint64_t)(high & 0xffff) << 32) | (low & 0xfff00000);
	if (!base || low == UINT32_MAX || high == UINT32_MAX ||
	    base > UINTPTR_MAX - 0x10fff)
		return false;
	uint8_t *registers = (void *)(uintptr_t)(base + 0x10000);
	for (size_t i = 0; i < ARRAY_SIZE(commands); i++)
		if (!pmf_get_limit(registers, commands[i], &native_limits[i]))
			return false;

	limits_state = LIMITS_READY;
	printk(BIOS_NOTICE, "PHX native STAPM/fast/slow limits: %u/%u/%u mW\n",
	       native_limits[0], native_limits[1], native_limits[2]);
	return true;
}

bool phx_power_budget(uint8_t available_watts)
{
	const uint32_t saved_index = pci_read_config32(SOC_GNB_DEV, 0xb8);
	static const enum smu_message_id commands[] = {
		SMC_MSG_SET_SUSTAINED_POWER_LIMIT,
		SMC_MSG_SET_FAST_PPT_LIMIT,
		SMC_MSG_SET_SLOW_PPT_LIMIT,
	};
	bool success = false;
	if (available_watts == 0xff) {
		success = capture_native_limits();
		goto out;
	}
	if (limits_state != LIMITS_READY)
		goto out;

	/* Match Merlin's existing reservation for the rest of the board. */
	uint32_t budget = MAX(20, available_watts > 25 ? available_watts - 25 : 20) * 1000;
	for (size_t i = 0; i < ARRAY_SIZE(commands); i++) {
		struct smu_payload args = { .msg = { MIN(budget, native_limits[i]) } };
		if (send_smu_message_timeout(commands[i], &args, 100000) != CB_SUCCESS)
			goto out;
	}
	success = true;
	printk(BIOS_NOTICE, "PHX power budget: available=%uW CPU budget=%umW applied\n",
	       available_watts, budget);
out:
	pci_write_config32(SOC_GNB_DEV, 0xb8, saved_index);
	return success;
}
