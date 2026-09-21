/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/capsule_broker.h>
#include <string.h>

static bool ready = true;
static bool busy;
static bool recurse;
static unsigned int dispatches;

void mock_assert(const int value, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!value)
		__builtin_trap();
}

bool capsule_broker_endpoint_ready(struct lb_capsule_broker_endpoint *endpoint)
{
	if (!ready)
		return false;
	*endpoint = (struct lb_capsule_broker_endpoint) {
		.transport = LB_CAPSULE_ENDPOINT_TRANSPORT_APM_IO8,
		.trigger_width = 1,
		.trigger_address = CAPSULE_BROKER_APM_PORT,
		.trigger_value = CAPSULE_BROKER_APM_COMMAND,
	};
	return true;
}

enum cb_err capsule_broker_transport_dispatch(void)
{
	if (busy)
		return CB_ERR;
	busy = true;
	dispatches++;
	if (recurse)
		assert(capsule_broker_smi_dispatch(CAPSULE_BROKER_APM_PORT,
			CAPSULE_BROKER_APM_COMMAND) == CB_ERR);
	busy = false;
	return CB_SUCCESS;
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (!strcmp(argv[1], "port")) {
		assert(capsule_broker_smi_dispatch(CAPSULE_BROKER_APM_PORT + 1,
			CAPSULE_BROKER_APM_COMMAND) == CB_ERR);
	} else if (!strcmp(argv[1], "value")) {
		assert(capsule_broker_smi_dispatch(CAPSULE_BROKER_APM_PORT,
			CAPSULE_BROKER_APM_COMMAND - 1) == CB_ERR);
	} else if (!strcmp(argv[1], "not-ready")) {
		ready = false;
		assert(capsule_broker_smi_dispatch(CAPSULE_BROKER_APM_PORT,
			CAPSULE_BROKER_APM_COMMAND) == CB_ERR);
	} else {
		recurse = !strcmp(argv[1], "reentry");
		assert(capsule_broker_smi_dispatch(CAPSULE_BROKER_APM_PORT,
			CAPSULE_BROKER_APM_COMMAND) == CB_SUCCESS);
	}
	assert(dispatches == (!strcmp(argv[1], "happy") ||
		!strcmp(argv[1], "reentry")));
	return 0;
}
