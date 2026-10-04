/* SPDX-License-Identifier: GPL-2.0-only */

#include <timer.h>
#include <console/console.h>
#include <amdblocks/smn.h>
#include <amdblocks/smu.h>
#include <soc/smu.h>
#include <thread.h>
#include <types.h>

/* returns SMU_MESG_RESP_OK, SMU_MESG_RESP_TIMEOUT or a negative number */
static int32_t smu_poll_response(bool print_command_duration, unsigned int timeout_us)
{
	struct stopwatch sw;
	int32_t result;

	stopwatch_init_usecs_expire(&sw, timeout_us);

	while (1) {
		result = smn_read32(SMN_SMU_MESG_RESP);
		if (result)
			break;

		if (stopwatch_expired(&sw)) {
			printk(BIOS_ERR, "SMU timeout: resp=%08x msgid=%08x\n",
			       smn_read32(SMN_SMU_MESG_RESP),
			       smn_read32(SMN_SMU_MESG_ID));
			for (size_t i = 0; i < SMU_NUM_ARGS; i++)
				printk(BIOS_ERR, "SMU arg%zu=%08x\n", i,
				       smn_read32(SMN_SMU_MESG_ARG(i)));
			return SMU_MESG_RESP_TIMEOUT;
		}
		thread_yield();
	}

	if (print_command_duration)
		printk(BIOS_SPEW, "SMU command consumed %lld usecs\n",
		       stopwatch_duration_usecs(&sw));
	return result;
}

/*
 * Send a message and bi-directional payload to the SMU. SMU response, if any, is returned via
 * *arg.
 */
static int32_t send_smu_message_raw_timeout(enum smu_message_id message_id,
					  struct smu_payload *arg, unsigned int timeout_us)
{
	size_t i;
	int32_t response;

	/* Wait until the SMU can process a new request; an old failed response is harmless. */
	response = smu_poll_response(false, timeout_us);
	if (response == SMU_MESG_RESP_TIMEOUT || response == -1)
		return response;

	/* Clear response register */
	smn_write32(SMN_SMU_MESG_RESP, 0);
	if (smn_read32(SMN_SMU_MESG_RESP) != 0)
		return -1;

	/* Populate arguments */
	for (i = 0; i < SMU_NUM_ARGS; i++)
		smn_write32(SMN_SMU_MESG_ARG(i), arg->msg[i]);

	/* Send message to SMU */
	smn_write32(SMN_SMU_MESG_ID, message_id);

	/* Wait until the SMU has processed the message */
	response = smu_poll_response(true, timeout_us);
	if (response == SMU_MESG_RESP_TIMEOUT || response == -1)
		return response;

	/* Copy returned values, even when the response isn't SMU_MESG_RESP_OK */
	for (i = 0; i < SMU_NUM_ARGS; i++)
		arg->msg[i] = smn_read32(SMN_SMU_MESG_ARG(i));

	return response;
}

int32_t send_smu_message_raw(enum smu_message_id message_id, struct smu_payload *arg)
{
	return send_smu_message_raw_timeout(message_id, arg, 10 * USECS_PER_SEC);
}

enum cb_err send_smu_message_timeout(enum smu_message_id message_id,
				    struct smu_payload *arg, unsigned int timeout_us)
{
	struct smu_payload response = *arg;

	if (send_smu_message_raw_timeout(message_id, &response, timeout_us) != SMU_MESG_RESP_OK)
		return CB_ERR;

	*arg = response;
	return CB_SUCCESS;
}

enum cb_err send_smu_message(enum smu_message_id message_id, struct smu_payload *arg)
{
	return send_smu_message_timeout(message_id, arg, 10 * USECS_PER_SEC);
}
