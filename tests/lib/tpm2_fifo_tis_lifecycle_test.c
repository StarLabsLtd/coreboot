/* SPDX-License-Identifier: GPL-2.0-only */

#include <drivers/pc80/tpm/tpm.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

#define ACCESS_VALID	(1 << 7)
#define ACCESS_ACTIVE	(1 << 5)
#define ACCESS_SEIZED	(1 << 4)
#define STATUS_VALID	(1 << 7)
#define STATUS_READY	(1 << 6)
#define STATUS_DATA	(1 << 4)
#define STATUS_EXPECT	(1 << 3)

static u8 access_values[8];
static u8 status_values[8];
static size_t access_count;
static size_t status_count;
static size_t access_index;
static size_t status_index;
static unsigned int ready_calls;
static unsigned int write_calls;
static unsigned int wait_calls;
static tpm_result_t ready_result;
static tpm_result_t wait_result;

void udelay(unsigned int usecs)
{
	(void)usecs;
}

struct device;
struct device_operations;
struct pnp_info;

void pnp_enable_devices(struct device *dev, struct device_operations *ops,
	unsigned int functions, struct pnp_info *info)
{
	(void)dev;
	(void)ops;
	(void)functions;
	(void)info;
}

static u8 next_value(const u8 *values, size_t count, size_t *index)
{
	CHECK(count);
	if (*index < count)
		return values[(*index)++];
	return values[count - 1];
}

static u8 read_access(int locality)
{
	CHECK(locality == 0);
	return next_value(access_values, access_count, &access_index);
}

static u8 read_status(int locality)
{
	CHECK(locality == 0);
	return next_value(status_values, status_count, &status_index);
}

static void write_access(u8 value, int locality)
{
	CHECK(value == ACCESS_ACTIVE);
	CHECK(locality == 0);
	write_calls++;
}

static tpm_result_t command_ready(u8 locality)
{
	CHECK(locality == 0);
	ready_calls++;
	return ready_result;
}

static tpm_result_t wait_access(int locality, u8 mask, u8 expected)
{
	CHECK(locality == 0);
	CHECK(mask == ACCESS_ACTIVE);
	CHECK(expected == 0);
	wait_calls++;
	return wait_result;
}

static const struct pc80_tis_fifo_test_backend backend = {
	.read_access = read_access,
	.read_status = read_status,
	.write_access = write_access,
	.command_ready = command_ready,
	.wait_access = wait_access,
};

static void reset(void)
{
	memset(access_values, 0, sizeof(access_values));
	memset(status_values, 0, sizeof(status_values));
	access_count = 0;
	status_count = 0;
	access_index = 0;
	status_index = 0;
	ready_calls = 0;
	write_calls = 0;
	wait_calls = 0;
	ready_result = TPM_SUCCESS;
	wait_result = TPM_SUCCESS;
	pc80_tis_fifo_set_test_backend(&backend);
}

static void quiesce_idle(void)
{
	reset();
	access_values[0] = ACCESS_VALID | ACCESS_ACTIVE;
	access_count = 1;
	status_values[0] = STATUS_VALID | STATUS_READY;
	status_count = 1;
	CHECK(pc80_tis_fifo_quiesce() == CB_SUCCESS);
	CHECK(ready_calls == 0);
}

static void quiesce_busy(u8 busy_status)
{
	reset();
	access_values[0] = ACCESS_VALID | ACCESS_ACTIVE;
	access_count = 1;
	status_values[0] = STATUS_VALID | busy_status;
	status_values[1] = STATUS_VALID | STATUS_READY;
	status_count = 2;
	CHECK(pc80_tis_fifo_quiesce() == CB_SUCCESS);
	CHECK(ready_calls == 1);
}

static void quiesce_lost(u8 lost_access)
{
	reset();
	access_values[0] = ACCESS_VALID | ACCESS_ACTIVE;
	access_values[1] = lost_access;
	access_count = 2;
	status_values[0] = STATUS_VALID | STATUS_DATA;
	status_count = 1;
	CHECK(pc80_tis_fifo_quiesce() == CB_ERR);
	CHECK(ready_calls == 0);
}

static void quiesce_ready_failure(bool bad_postcondition)
{
	reset();
	access_values[0] = ACCESS_VALID | ACCESS_ACTIVE;
	access_count = 1;
	status_values[0] = STATUS_VALID | STATUS_EXPECT;
	status_values[1] = STATUS_VALID |
		(bad_postcondition ? STATUS_DATA : STATUS_READY);
	status_count = 2;
	if (!bad_postcondition)
		ready_result = TPM_CB_TIMEOUT;
	CHECK(pc80_tis_fifo_quiesce() == CB_ERR);
	CHECK(ready_calls == 1);
}

static void release_test(const char *scenario)
{
	reset();
	access_values[0] = ACCESS_VALID | ACCESS_ACTIVE;
	access_values[1] = ACCESS_VALID | ACCESS_ACTIVE;
	access_values[2] = ACCESS_VALID;
	access_count = 3;
	status_values[0] = STATUS_VALID | STATUS_READY;
	status_count = 1;
	if (!strcmp(scenario, "release-wait-timeout"))
		wait_result = TPM_CB_TIMEOUT;
	else if (!strcmp(scenario, "release-still-active"))
		access_values[2] |= ACCESS_ACTIVE;
	else if (!strcmp(scenario, "release-invalid"))
		access_values[2] = 0;
	else if (!strcmp(scenario, "release-seized"))
		access_values[2] |= ACCESS_SEIZED;

	if (!strcmp(scenario, "release-success"))
		CHECK(pc80_tis_fifo_release_locality() == CB_SUCCESS);
	else
		CHECK(pc80_tis_fifo_release_locality() == CB_ERR);
	CHECK(write_calls == 1);
	CHECK(wait_calls == 1);
}

static void release_lost(u8 lost_access)
{
	reset();
	access_values[0] = ACCESS_VALID | ACCESS_ACTIVE;
	access_values[1] = lost_access;
	access_count = 2;
	status_values[0] = STATUS_VALID | STATUS_READY;
	status_count = 1;
	CHECK(pc80_tis_fifo_release_locality() == CB_ERR);
	CHECK(write_calls == 0);
	CHECK(wait_calls == 0);
}

static void invalid_status(void)
{
	reset();
	access_values[0] = ACCESS_VALID | ACCESS_ACTIVE;
	access_count = 1;
	status_values[0] = STATUS_READY;
	status_count = 1;
	CHECK(pc80_tis_fifo_quiesce() == CB_ERR);
	CHECK(ready_calls == 0);
	CHECK(write_calls == 0);
	CHECK(wait_calls == 0);
}

static void transmit_lost(u8 lost_access)
{
	reset();
	access_values[0] = ACCESS_VALID | ACCESS_ACTIVE;
	access_values[1] = lost_access;
	access_count = 2;
	status_values[0] = STATUS_VALID | STATUS_READY;
	status_count = 1;
	CHECK(pc80_tis_fifo_validate_idle() == CB_ERR);
	CHECK(ready_calls == 0);
	CHECK(write_calls == 0);
	CHECK(wait_calls == 0);
}

int main(int argc, char **argv)
{
	CHECK(argc == 2);
	if (!strcmp(argv[1], "idle"))
		quiesce_idle();
	else if (!strcmp(argv[1], "data-available"))
		quiesce_busy(STATUS_DATA);
	else if (!strcmp(argv[1], "expect"))
		quiesce_busy(STATUS_EXPECT);
	else if (!strcmp(argv[1], "lost-locality"))
		quiesce_lost(ACCESS_VALID);
	else if (!strcmp(argv[1], "seized-locality"))
		quiesce_lost(ACCESS_VALID | ACCESS_ACTIVE | ACCESS_SEIZED);
	else if (!strcmp(argv[1], "ready-timeout"))
		quiesce_ready_failure(false);
	else if (!strcmp(argv[1], "ready-bad-postcondition"))
		quiesce_ready_failure(true);
	else if (!strcmp(argv[1], "invalid-status"))
		invalid_status();
	else if (!strcmp(argv[1], "transmit-lost-locality"))
		transmit_lost(ACCESS_VALID);
	else if (!strcmp(argv[1], "transmit-seized-locality"))
		transmit_lost(ACCESS_VALID | ACCESS_ACTIVE | ACCESS_SEIZED);
	else if (!strcmp(argv[1], "release-lost-locality"))
		release_lost(ACCESS_VALID);
	else if (!strcmp(argv[1], "release-seized-locality"))
		release_lost(ACCESS_VALID | ACCESS_ACTIVE | ACCESS_SEIZED);
	else
		release_test(argv[1]);
	return 0;
}
