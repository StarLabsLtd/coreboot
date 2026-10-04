/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/smu.h>
#include <amdblocks/smn.h>
#include <console/console.h>
#define main coreboot_main
#include <thread.h>
#undef main
#include <timer.h>
#include <tests/test.h>

static u32 response, command_response;
static u32 arguments[SMU_NUM_ARGS];
static unsigned int writes;
static uint64_t elapsed;
static bool clear_stuck;

static u32 mailbox_read(u32 address)
{
	if (address == SMN_SMU_MESG_RESP)
		return response;
	if (address == SMN_SMU_MESG_ID)
		return SMC_MSG_USB4_SLEEP;
	assert_true(address >= SMN_SMU_MESG_ARG(0) &&
		    address <= SMN_SMU_MESG_ARG(SMU_NUM_ARGS - 1));
	return arguments[(address - SMN_SMU_MESG_ARG(0)) / sizeof(u32)];
}

static void mailbox_write(u32 address, u32 value)
{
	writes++;
	if (address == SMN_SMU_MESG_RESP) {
		if (!clear_stuck)
			response = value;
	} else if (address == SMN_SMU_MESG_ID) {
		response = command_response;
		for (size_t i = 0; i < SMU_NUM_ARGS; i++)
			arguments[i]++;
	} else {
		assert_true(address >= SMN_SMU_MESG_ARG(0) &&
			    address <= SMN_SMU_MESG_ARG(SMU_NUM_ARGS - 1));
		arguments[(address - SMN_SMU_MESG_ARG(0)) / sizeof(u32)] = value;
	}
}

void timer_monotonic_get(struct mono_time *time)
{
	time->microseconds = elapsed++;
}

static int mailbox_printk(int level, const char *format, ...)
{
	return 0;
}

#define smn_read32  mailbox_read
#define smn_write32 mailbox_write
#define printk      mailbox_printk
#include "../../src/soc/amd/common/block/smu/smu.c"

static int setup(void **state)
{
	response = command_response = 1;
	writes = elapsed = 0;
	clear_stuck = false;
	return 0;
}

static void successful_exchange(void **state)
{
	struct smu_payload payload = {
		.msg = {1, 2, 3, 4, 5, 6}
        };

	/* An old failed command does not prevent a new request. */
	response = 0xfe;
	assert_int_equal(send_smu_message_timeout(SMC_MSG_SET_TOOLS_DRAM_ADDR, &payload, 100),
			 CB_SUCCESS);
	assert_int_equal(writes, SMU_NUM_ARGS + 2);
	for (size_t i = 0; i < SMU_NUM_ARGS; i++)
		assert_int_equal(payload.msg[i], i + 2);
}

static void not_ready(void **state)
{
	struct smu_payload payload = {0};

	response = 0;
	assert_int_equal(send_smu_message_timeout(SMC_MSG_USB4_SLEEP, &payload, 100), CB_ERR);
	assert_int_equal(writes, 0);
	assert_true(elapsed >= 100 && elapsed <= 110);
}

static void inaccessible_mailbox(void **state)
{
	struct smu_payload payload = {0};

	response = UINT32_MAX;
	assert_int_equal(send_smu_message_timeout(SMC_MSG_USB4_SLEEP, &payload, 100), CB_ERR);
	assert_int_equal(writes, 0);
}

static void clear_failure(void **state)
{
	struct smu_payload payload = {0};

	clear_stuck = true;
	assert_int_equal(send_smu_message_timeout(SMC_MSG_USB4_SLEEP, &payload, 100), CB_ERR);
	assert_int_equal(writes, 1);
}

static void command_failure(void **state)
{
	struct smu_payload payload = {.msg = {7}};

	command_response = 0xfe;
	assert_int_equal(send_smu_message_timeout(SMC_MSG_USB4_SLEEP, &payload, 100), CB_ERR);
	assert_int_equal(payload.msg[0], 7);
}

static void raw_command_failure(void **state)
{
	struct smu_payload payload = {.msg = {7}};

	command_response = 0xfe;
	assert_int_equal(send_smu_message_raw(SMC_MSG_USB4_SLEEP, &payload), 0xfe);
	assert_int_equal(payload.msg[0], 8);
}

static void command_timeout(void **state)
{
	struct smu_payload payload = {0};

	command_response = 0;
	assert_int_equal(send_smu_message_timeout(SMC_MSG_USB4_SLEEP, &payload, 100), CB_ERR);
	assert_true(elapsed >= 100 && elapsed <= 115);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(successful_exchange, setup),
		cmocka_unit_test_setup(not_ready, setup),
		cmocka_unit_test_setup(inaccessible_mailbox, setup),
		cmocka_unit_test_setup(clear_failure, setup),
		cmocka_unit_test_setup(command_failure, setup),
		cmocka_unit_test_setup(raw_command_failure, setup),
		cmocka_unit_test_setup(command_timeout, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
