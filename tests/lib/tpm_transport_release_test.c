/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/transport_release.h>
#include <string.h>

#define CRB_LOC_STATE 0x00U
#define CRB_LOC_CTRL 0x08U
#define CRB_LOC_STS 0x0cU
#define CRB_CTRL_REQ 0x40U
#define CRB_CTRL_STS 0x44U
#define CRB_CTRL_START 0x4cU

#define CRB_LOC_STATE_ASSIGNED (1U << 1)
#define CRB_LOC_STATE_ACTIVE_SHIFT 2
#define CRB_LOC_STATE_VALID (1U << 7)
#define CRB_LOC_STS_BEEN_SEIZED (1U << 1)
#define CRB_LOC_STS_GRANTED (1U << 0)
#define CRB_CTRL_REQ_GO_IDLE (1U << 1)
#define CRB_CTRL_REQ_COMMAND_READY (1U << 0)
#define CRB_CTRL_STS_ERROR (1U << 0)
#define CRB_CTRL_STS_IDLE (1U << 1)
#define CRB_CTRL_START_START (1U << 0)

#define FIFO_ACCESS 0x00U
#define FIFO_STS 0x18U
#define FIFO_ACCESS_VALID (1U << 7)
#define FIFO_ACCESS_ACTIVE_LOCALITY (1U << 5)
#define FIFO_ACCESS_BEEN_SEIZED (1U << 4)
#define FIFO_STS_VALID (1U << 7)
#define FIFO_STS_COMMAND_READY (1U << 6)
#define FIFO_STS_DATA_AVAILABLE (1U << 4)
#define FIFO_STS_EXPECT (1U << 3)

#define NEVER 0xffffffffU
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

struct write_record {
	uint32_t offset;
	uint32_t value;
	uint8_t width;
};

struct mock_transport {
	uint32_t locality_state;
	uint32_t locality_status;
	uint32_t crb_request;
	uint32_t crb_status;
	uint32_t crb_start;
	uint8_t fifo_access;
	uint8_t fifo_status;
	uint32_t command_ready_ticks;
	uint32_t command_ticks;
	uint32_t go_idle_ticks;
	uint32_t release_ticks;
	bool command_ready_leaves_idle;
	bool go_idle_leaves_ready;
	unsigned int fifo_ready_write;
	unsigned int fifo_ready_writes;
	bool release_requested;
	unsigned int operations;
	unsigned int fail_operation;
	unsigned int seize_operation;
	unsigned int fifo_loss_operation;
	unsigned int loss_delay;
	unsigned int delays;
	struct write_record writes[8];
	unsigned int write_count;
	struct tpm2_transport_release *mutate;
};

static bool operation_fails(struct mock_transport *mock)
{
	mock->operations++;
	if (mock->operations == mock->seize_operation)
		mock->locality_status |= CRB_LOC_STS_BEEN_SEIZED;
	if (mock->operations == mock->fifo_loss_operation)
		mock->fifo_access &= ~FIFO_ACCESS_ACTIVE_LOCALITY;
	return mock->fail_operation &&
		mock->operations == mock->fail_operation;
}

static void record_write(struct mock_transport *mock, uint32_t offset,
	uint32_t value, uint8_t width)
{
	CHECK(mock->write_count < ARRAY_SIZE(mock->writes));
	if (mock->write_count < ARRAY_SIZE(mock->writes)) {
		mock->writes[mock->write_count] = (struct write_record) {
			.offset = offset,
			.value = value,
			.width = width,
		};
	}
	mock->write_count++;
}

static enum cb_err mock_read32(void *context, uint32_t offset,
	uint32_t *value)
{
	struct mock_transport *mock = context;

	if (operation_fails(mock))
		return CB_ERR;
	switch (offset) {
	case CRB_LOC_STATE:
		*value = mock->locality_state;
		break;
	case CRB_LOC_STS:
		*value = mock->locality_status;
		break;
	case CRB_CTRL_REQ:
		*value = mock->crb_request;
		break;
	case CRB_CTRL_STS:
		*value = mock->crb_status;
		break;
	case CRB_CTRL_START:
		*value = mock->crb_start;
		break;
	default:
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static enum cb_err mock_write32(void *context, uint32_t offset,
	uint32_t value)
{
	struct mock_transport *mock = context;

	if (operation_fails(mock))
		return CB_ERR;
	record_write(mock, offset, value, 32);
	if (offset == CRB_CTRL_REQ && value == CRB_CTRL_REQ_GO_IDLE) {
		mock->crb_request = value;
		if (!mock->go_idle_ticks) {
			mock->crb_request = 0;
			if (!mock->go_idle_leaves_ready)
				mock->crb_status = CRB_CTRL_STS_IDLE;
		}
		return CB_SUCCESS;
	}
	if (offset == CRB_LOC_CTRL && value == (1U << 1)) {
		mock->release_requested = true;
		if (!mock->release_ticks) {
			mock->locality_status &= ~CRB_LOC_STS_GRANTED;
			mock->locality_state &= ~CRB_LOC_STATE_ASSIGNED;
		}
		return CB_SUCCESS;
	}
	return CB_ERR;
}

static enum cb_err mock_read8(void *context, uint32_t offset,
	uint8_t *value)
{
	struct mock_transport *mock = context;

	if (operation_fails(mock))
		return CB_ERR;
	switch (offset) {
	case FIFO_ACCESS:
		*value = mock->fifo_access;
		break;
	case FIFO_STS:
		*value = mock->fifo_status;
		break;
	default:
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static enum cb_err mock_write8(void *context, uint32_t offset,
	uint8_t value)
{
	struct mock_transport *mock = context;

	if (operation_fails(mock))
		return CB_ERR;
	record_write(mock, offset, value, 8);
	if (offset == FIFO_STS && value == FIFO_STS_COMMAND_READY) {
		mock->fifo_ready_writes++;
		if (mock->fifo_ready_write &&
		    mock->fifo_ready_writes >= mock->fifo_ready_write)
			mock->fifo_status = FIFO_STS_VALID |
				FIFO_STS_COMMAND_READY;
		return CB_SUCCESS;
	}
	if (offset == FIFO_ACCESS &&
	    value == FIFO_ACCESS_ACTIVE_LOCALITY) {
		mock->release_requested = true;
		if (!mock->release_ticks)
			mock->fifo_access &= ~FIFO_ACCESS_ACTIVE_LOCALITY;
		return CB_SUCCESS;
	}
	return CB_ERR;
}

static void mock_delay(void *context, uint32_t delay_us)
{
	struct mock_transport *mock = context;

	CHECK(delay_us == 1);
	mock->delays++;
	if (mock->loss_delay && mock->delays == mock->loss_delay) {
		mock->locality_status |= CRB_LOC_STS_BEEN_SEIZED;
		mock->fifo_access &= ~FIFO_ACCESS_ACTIVE_LOCALITY;
	}
	if (mock->crb_request & CRB_CTRL_REQ_COMMAND_READY &&
	    mock->command_ready_ticks != NEVER &&
	    mock->command_ready_ticks &&
	    --mock->command_ready_ticks == 0) {
		mock->crb_request = 0;
		if (!mock->command_ready_leaves_idle)
			mock->crb_status = 0;
	}
	if (mock->crb_start && mock->command_ticks != NEVER &&
	    mock->command_ticks && --mock->command_ticks == 0)
		mock->crb_start = 0;
	if (mock->crb_request & CRB_CTRL_REQ_GO_IDLE &&
	    mock->go_idle_ticks != NEVER && mock->go_idle_ticks &&
	    --mock->go_idle_ticks == 0) {
		mock->crb_request = 0;
		if (!mock->go_idle_leaves_ready)
			mock->crb_status = CRB_CTRL_STS_IDLE;
	}
	if (mock->release_requested && mock->release_ticks != NEVER &&
	    mock->release_ticks &&
	    --mock->release_ticks == 0) {
		mock->locality_status &= ~CRB_LOC_STS_GRANTED;
		mock->locality_state &= ~CRB_LOC_STATE_ASSIGNED;
		mock->fifo_access &= ~FIFO_ACCESS_ACTIVE_LOCALITY;
	}
	if (mock->mutate) {
		mock->mutate->interface = (enum tpm2_transport_interface)99;
		mock->mutate->poll_attempts = 0;
		mock->mutate = NULL;
	}
}

static struct mock_transport initial_mock(unsigned int locality)
{
	return (struct mock_transport) {
		.locality_state = CRB_LOC_STATE_VALID |
			CRB_LOC_STATE_ASSIGNED |
			(locality << CRB_LOC_STATE_ACTIVE_SHIFT),
		.locality_status = CRB_LOC_STS_GRANTED,
		.crb_status = CRB_CTRL_STS_IDLE,
		.fifo_access = FIFO_ACCESS_VALID |
			FIFO_ACCESS_ACTIVE_LOCALITY,
		.fifo_status = FIFO_STS_VALID | FIFO_STS_COMMAND_READY,
		.command_ready_ticks = NEVER,
		.command_ticks = NEVER,
		.go_idle_ticks = NEVER,
		.release_ticks = NEVER,
	};
}

static struct tpm2_transport_release transport(
	struct mock_transport *mock, enum tpm2_transport_interface interface,
	unsigned int locality)
{
	return (struct tpm2_transport_release) {
		.interface = interface,
		.locality = locality,
		.poll_attempts = 3,
		.poll_delay_us = 1,
		.io = {
			.read8 = mock_read8,
			.write8 = mock_write8,
			.read32 = mock_read32,
			.write32 = mock_write32,
			.delay_us = mock_delay,
			.context = mock,
		},
	};
}

static void test_invalid_arguments(void)
{
	struct mock_transport mock = initial_mock(0);
	struct tpm2_transport_release current = transport(&mock,
		TPM2_TRANSPORT_CRB, 0);

	CHECK(tpm2_transport_quiesce(NULL) == CB_ERR_ARG);
	current.locality = 5;
	CHECK(tpm2_transport_quiesce(&current) == CB_ERR_ARG);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	current.poll_attempts = 0;
	CHECK(tpm2_transport_quiesce(&current) == CB_ERR_ARG);
	current.poll_attempts = TPM2_TRANSPORT_MAX_POLL_ATTEMPTS + 1;
	CHECK(tpm2_transport_quiesce(&current) == CB_ERR_ARG);
	current.poll_attempts = 11;
	current.poll_delay_us = 1000000;
	CHECK(tpm2_transport_quiesce(&current) == CB_ERR_ARG);
	current = transport(&mock, TPM2_TRANSPORT_FIFO, 0);
	current.poll_delay_us = 0;
	CHECK(tpm2_transport_quiesce(&current) == CB_ERR_ARG);
	current.poll_delay_us = TPM2_TRANSPORT_MAX_POLL_DELAY_US + 1;
	CHECK(tpm2_transport_quiesce(&current) == CB_ERR_ARG);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	current.io.delay_us = NULL;
	CHECK(tpm2_transport_quiesce(&current) == CB_ERR_ARG);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	current.io.read32 = NULL;
	CHECK(tpm2_transport_quiesce(&current) == CB_ERR_ARG);
	current = transport(&mock, TPM2_TRANSPORT_FIFO, 0);
	current.io.write8 = NULL;
	CHECK(tpm2_transport_quiesce(&current) == CB_ERR_ARG);
	current = transport(&mock, (enum tpm2_transport_interface)99, 0);
	CHECK(tpm2_transport_quiesce(&current) == CB_ERR_ARG);
	CHECK(mock.write_count == 0);
}

static bool wrote_offset(const struct mock_transport *mock, uint32_t offset)
{
	for (unsigned int i = 0; i < mock->write_count; i++) {
		if (mock->writes[i].offset == offset)
			return true;
	}
	return false;
}

static struct mock_transport active_crb(void)
{
	struct mock_transport mock = initial_mock(0);

	mock.crb_status = 0;
	mock.crb_start = CRB_CTRL_START_START;
	mock.command_ticks = 1;
	mock.go_idle_ticks = 1;
	mock.release_ticks = 1;
	return mock;
}

static struct mock_transport active_fifo(void)
{
	struct mock_transport mock = initial_mock(0);

	mock.fifo_status = FIFO_STS_VALID | FIFO_STS_DATA_AVAILABLE |
		FIFO_STS_EXPECT;
	mock.fifo_ready_write = 2;
	mock.release_ticks = 1;
	return mock;
}

static void test_crb_state_machine(void)
{
	struct mock_transport mock = active_crb();
	struct tpm2_transport_release current = transport(&mock,
		TPM2_TRANSPORT_CRB, 0);

	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_SUCCESS);
	CHECK(mock.write_count == 2);
	CHECK(mock.writes[0].offset == CRB_CTRL_REQ);
	CHECK(mock.writes[0].value == CRB_CTRL_REQ_GO_IDLE);
	CHECK(mock.writes[1].offset == CRB_LOC_CTRL);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.crb_request = CRB_CTRL_REQ_COMMAND_READY;
	mock.command_ready_ticks = 1;
	mock.go_idle_ticks = 1;
	mock.release_ticks = 0;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_SUCCESS);
	CHECK(mock.write_count == 2);
	CHECK(mock.writes[0].offset == CRB_CTRL_REQ);
	CHECK(mock.writes[0].value == CRB_CTRL_REQ_GO_IDLE);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.crb_status = 0;
	mock.crb_request = CRB_CTRL_REQ_GO_IDLE;
	mock.go_idle_ticks = 1;
	mock.release_ticks = 0;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_SUCCESS);
	CHECK(mock.write_count == 1);
	CHECK(mock.writes[0].offset == CRB_LOC_CTRL);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.release_ticks = 1;
	mock.mutate = &current;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_SUCCESS);
	CHECK(mock.write_count == 1);
	CHECK(mock.writes[0].offset == CRB_LOC_CTRL);
	CHECK(current.poll_attempts == 0);
}

static void test_crb_timeouts(void)
{
	struct mock_transport mock = initial_mock(0);
	struct tpm2_transport_release current = transport(&mock,
		TPM2_TRANSPORT_CRB, 0);

	mock.crb_status = 0;
	mock.crb_start = CRB_CTRL_START_START;
	mock.command_ticks = NEVER;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 0);
	CHECK(mock.delays == 2);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.crb_request = CRB_CTRL_REQ_COMMAND_READY;
	mock.command_ready_ticks = NEVER;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 0);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.crb_request = CRB_CTRL_REQ_COMMAND_READY;
	mock.command_ready_ticks = 1;
	mock.command_ready_leaves_idle = true;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 0);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.crb_status = 0;
	mock.crb_request = CRB_CTRL_REQ_GO_IDLE;
	mock.go_idle_ticks = NEVER;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 0);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.crb_status = 0;
	mock.crb_request = CRB_CTRL_REQ_GO_IDLE;
	mock.go_idle_ticks = 1;
	mock.go_idle_leaves_ready = true;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 0);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.crb_status = 0;
	mock.go_idle_ticks = NEVER;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 1);
	CHECK(mock.writes[0].offset == CRB_CTRL_REQ);
	CHECK(!wrote_offset(&mock, CRB_LOC_CTRL));

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.release_ticks = NEVER;
	CHECK(tpm2_transport_release_locality(&current) == CB_ERR);
	CHECK(mock.write_count == 1);
	CHECK(mock.writes[0].offset == CRB_LOC_CTRL);
}

static void test_crb_impossible_states(void)
{
	struct mock_transport mock;
	struct tpm2_transport_release current;

	for (unsigned int state = 0; state < 5; state++) {
		mock = initial_mock(0);
		current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
		switch (state) {
		case 0:
			mock.crb_request = CRB_CTRL_REQ_COMMAND_READY |
				CRB_CTRL_REQ_GO_IDLE;
			break;
		case 1:
			mock.crb_status |= CRB_CTRL_STS_ERROR;
			break;
		case 2:
			mock.crb_start = CRB_CTRL_START_START;
			break;
		case 3:
			mock.crb_status = 0;
			mock.crb_start = CRB_CTRL_START_START;
			mock.crb_request = CRB_CTRL_REQ_COMMAND_READY;
			break;
		default:
			mock.crb_request = 4;
			break;
		}
		CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
		CHECK(mock.write_count == 0);
	}
}

static void test_crb_ownership_loss(void)
{
	struct mock_transport mock = initial_mock(0);
	struct tpm2_transport_release current = transport(&mock,
		TPM2_TRANSPORT_CRB, 0);

	mock.locality_status |= CRB_LOC_STS_BEEN_SEIZED;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 0);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.crb_request = CRB_CTRL_REQ_COMMAND_READY;
	mock.command_ready_ticks = 2;
	mock.loss_delay = 1;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 0);

	mock = active_crb();
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.command_ticks = 2;
	mock.loss_delay = 1;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 0);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.crb_status = 0;
	mock.crb_request = CRB_CTRL_REQ_GO_IDLE;
	mock.go_idle_ticks = 2;
	mock.loss_delay = 1;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 0);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.crb_status = 0;
	mock.go_idle_ticks = 2;
	mock.loss_delay = 1;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 1);
	CHECK(!wrote_offset(&mock, CRB_LOC_CTRL));

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
	mock.seize_operation = 6;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 0);
}

static void test_crb_mock_does_not_complete_command(void)
{
	struct mock_transport mock = initial_mock(0);

	mock.crb_status = 0;
	mock.crb_start = CRB_CTRL_START_START;
	mock.crb_request = CRB_CTRL_REQ_GO_IDLE;
	mock.go_idle_ticks = 1;
	mock_delay(&mock, 1);
	CHECK(mock.crb_start == CRB_CTRL_START_START);
}

static void run_crb_io_failure_matrix(struct mock_transport template)
{
	struct mock_transport baseline = template;
	struct tpm2_transport_release current = transport(&baseline,
		TPM2_TRANSPORT_CRB, 0);
	unsigned int operations;

	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_SUCCESS);
	operations = baseline.operations;
	for (unsigned int operation = 1; operation <= operations; operation++) {
		struct mock_transport mock = template;

		current = transport(&mock, TPM2_TRANSPORT_CRB, 0);
		mock.fail_operation = operation;
		CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	}
}

static void test_crb_io_failure_matrices(void)
{
	struct mock_transport mock = active_crb();

	run_crb_io_failure_matrix(mock);

	mock = initial_mock(0);
	mock.release_ticks = 1;
	run_crb_io_failure_matrix(mock);

	mock = initial_mock(0);
	mock.crb_request = CRB_CTRL_REQ_COMMAND_READY;
	mock.command_ready_ticks = 1;
	mock.go_idle_ticks = 1;
	mock.release_ticks = 1;
	run_crb_io_failure_matrix(mock);

	mock = initial_mock(0);
	mock.crb_status = 0;
	mock.crb_request = CRB_CTRL_REQ_GO_IDLE;
	mock.go_idle_ticks = 1;
	mock.release_ticks = 1;
	run_crb_io_failure_matrix(mock);
}

static void test_fifo_success(void)
{
	struct mock_transport mock = active_fifo();
	struct tpm2_transport_release current = transport(&mock,
		TPM2_TRANSPORT_FIFO, 0);

	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_SUCCESS);
	CHECK(mock.write_count == 3);
	CHECK(mock.writes[0].offset == FIFO_STS);
	CHECK(mock.writes[1].offset == FIFO_STS);
	CHECK(mock.writes[2].offset == FIFO_ACCESS);
	CHECK(!(mock.fifo_access & FIFO_ACCESS_ACTIVE_LOCALITY));
}

static void test_fifo_already_idle(void)
{
	struct mock_transport mock = initial_mock(0);
	struct tpm2_transport_release current = transport(&mock,
		TPM2_TRANSPORT_FIFO, 0);

	mock.release_ticks = 0;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_SUCCESS);
	CHECK(mock.write_count == 1);
	CHECK(mock.writes[0].offset == FIFO_ACCESS);
}

static void test_fifo_fail_closed(void)
{
	struct mock_transport mock = initial_mock(0);
	struct tpm2_transport_release current = transport(&mock,
		TPM2_TRANSPORT_FIFO, 0);

	mock.fifo_status = FIFO_STS_VALID | FIFO_STS_DATA_AVAILABLE;
	mock.fifo_ready_write = 0;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 2);
	CHECK(mock.delays == 2);
	CHECK(mock.fifo_access & FIFO_ACCESS_ACTIVE_LOCALITY);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_FIFO, 0);
	mock.fifo_access |= FIFO_ACCESS_BEEN_SEIZED;
	CHECK(tpm2_transport_quiesce(&current) == CB_ERR);
	CHECK(mock.write_count == 0);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_FIFO, 0);
	mock.fifo_status = FIFO_STS_VALID | FIFO_STS_DATA_AVAILABLE;
	CHECK(tpm2_transport_release_locality(&current) == CB_ERR);
	CHECK(mock.write_count == 0);

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_FIFO, 0);
	mock.release_ticks = NEVER;
	CHECK(tpm2_transport_release_locality(&current) == CB_ERR);
	CHECK(mock.write_count == 1);
	CHECK(mock.delays == 2);
}

static void test_fifo_ownership_loss(void)
{
	struct mock_transport mock = initial_mock(0);
	struct tpm2_transport_release current = transport(&mock,
		TPM2_TRANSPORT_FIFO, 0);

	mock.fifo_access |= FIFO_ACCESS_BEEN_SEIZED;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 0);

	mock = active_fifo();
	current = transport(&mock, TPM2_TRANSPORT_FIFO, 0);
	mock.fifo_ready_write = 0;
	mock.loss_delay = 1;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(!wrote_offset(&mock, FIFO_ACCESS));

	mock = initial_mock(0);
	current = transport(&mock, TPM2_TRANSPORT_FIFO, 0);
	mock.fifo_loss_operation = 3;
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	CHECK(mock.write_count == 0);
}

static void test_fifo_io_failure_matrices(void)
{
	struct mock_transport baseline = active_fifo();
	struct tpm2_transport_release current = transport(&baseline,
		TPM2_TRANSPORT_FIFO, 0);
	unsigned int active_operations;
	unsigned int idle_operations;

	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_SUCCESS);
	active_operations = baseline.operations;
	for (unsigned int operation = 1; operation <= active_operations;
	     operation++) {
		struct mock_transport mock = active_fifo();

		current = transport(&mock, TPM2_TRANSPORT_FIFO, 0);
		mock.fail_operation = operation;
		CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	}

	baseline = initial_mock(0);
	baseline.release_ticks = 1;
	current = transport(&baseline, TPM2_TRANSPORT_FIFO, 0);
	CHECK(tpm2_transport_quiesce_and_release(&current) == CB_SUCCESS);
	idle_operations = baseline.operations;
	for (unsigned int operation = 1; operation <= idle_operations;
	     operation++) {
		struct mock_transport mock = initial_mock(0);

		mock.release_ticks = 1;
		mock.fail_operation = operation;
		current = transport(&mock, TPM2_TRANSPORT_FIFO, 0);
		CHECK(tpm2_transport_quiesce_and_release(&current) == CB_ERR);
	}
}

int main(void)
{
	test_invalid_arguments();
	test_crb_state_machine();
	test_crb_timeouts();
	test_crb_impossible_states();
	test_crb_ownership_loss();
	test_crb_mock_does_not_complete_command();
	test_crb_io_failure_matrices();
	test_fifo_success();
	test_fifo_already_idle();
	test_fifo_fail_closed();
	test_fifo_ownership_loss();
	test_fifo_io_failure_matrices();

	return 0;
}
