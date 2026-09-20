/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/transport_release.h>
#include <security/tpm/tis.h>

#if ENV_SMM && !ENV_TEST
#error "TPM transport release must not be built in SMM"
#endif

#define TPM_LOCALITY_COUNT 5U

#define CRB_LOC_STATE 0x00U
#define CRB_LOC_CTRL 0x08U
#define CRB_LOC_STS 0x0cU
#define CRB_CTRL_REQ 0x40U
#define CRB_CTRL_STS 0x44U
#define CRB_CTRL_START 0x4cU

#define CRB_LOC_STATE_ASSIGNED (1U << 1)
#define CRB_LOC_STATE_ACTIVE_SHIFT 2
#define CRB_LOC_STATE_ACTIVE_MASK (7U << CRB_LOC_STATE_ACTIVE_SHIFT)
#define CRB_LOC_STATE_VALID (1U << 7)
#define CRB_LOC_CTRL_RELINQUISH (1U << 1)
#define CRB_LOC_STS_BEEN_SEIZED (1U << 1)
#define CRB_LOC_STS_GRANTED (1U << 0)
#define CRB_CTRL_REQ_GO_IDLE (1U << 1)
#define CRB_CTRL_REQ_COMMAND_READY (1U << 0)
#define CRB_CTRL_STS_ERROR (1U << 0)
#define CRB_CTRL_STS_IDLE (1U << 1)
#define CRB_CTRL_START_START (1U << 0)

#define FIFO_ACCESS 0x00U
#define FIFO_STS 0x18U
typedef enum cb_err (*poll_condition)(
	const struct tpm2_transport_release *transport, bool *complete);

struct crb_state {
	uint32_t request;
	bool idle;
	bool started;
};

static bool transport_valid(const struct tpm2_transport_release *transport)
{
	if (!transport || transport->locality >= TPM_LOCALITY_COUNT ||
	    !transport->poll_attempts ||
	    transport->poll_attempts > TPM2_TRANSPORT_MAX_POLL_ATTEMPTS ||
	    !transport->poll_delay_us ||
	    transport->poll_delay_us > TPM2_TRANSPORT_MAX_POLL_DELAY_US ||
	    (uint64_t)transport->poll_attempts *
		transport->poll_delay_us > TPM2_TRANSPORT_MAX_POLL_TIME_US ||
	    !transport->io.delay_us)
		return false;

	switch (transport->interface) {
	case TPM2_TRANSPORT_CRB:
		return transport->io.read32 && transport->io.write32;
	case TPM2_TRANSPORT_FIFO:
		return transport->io.read8 && transport->io.write8;
	default:
		return false;
	}
}

static enum cb_err poll(const struct tpm2_transport_release *transport,
	poll_condition condition)
{
	for (uint32_t attempt = 0; attempt < transport->poll_attempts;
	     attempt++) {
		bool complete;

		if (condition(transport, &complete) != CB_SUCCESS)
			return CB_ERR;
		if (complete)
			return CB_SUCCESS;
		if (attempt + 1 < transport->poll_attempts)
			transport->io.delay_us(transport->io.context,
				transport->poll_delay_us);
	}

	return CB_ERR;
}

static enum cb_err crb_owned(
	const struct tpm2_transport_release *transport)
{
	uint32_t locality_state;
	uint32_t locality_status;
	uint32_t active_locality;

	if (transport->io.read32(transport->io.context, CRB_LOC_STATE,
		&locality_state) != CB_SUCCESS ||
	    transport->io.read32(transport->io.context, CRB_LOC_STS,
		&locality_status) != CB_SUCCESS)
		return CB_ERR;

	active_locality = (locality_state & CRB_LOC_STATE_ACTIVE_MASK) >>
		CRB_LOC_STATE_ACTIVE_SHIFT;
	if ((locality_state & (CRB_LOC_STATE_VALID |
		CRB_LOC_STATE_ASSIGNED)) !=
		(CRB_LOC_STATE_VALID | CRB_LOC_STATE_ASSIGNED) ||
	    active_locality != transport->locality ||
	    !(locality_status & CRB_LOC_STS_GRANTED) ||
	    (locality_status & CRB_LOC_STS_BEEN_SEIZED))
		return CB_ERR;

	return CB_SUCCESS;
}

static enum cb_err crb_read_state(
	const struct tpm2_transport_release *transport, struct crb_state *state)
{
	uint32_t request;
	uint32_t status;
	uint32_t start;

	if (crb_owned(transport) != CB_SUCCESS ||
	    transport->io.read32(transport->io.context, CRB_CTRL_START,
		&start) != CB_SUCCESS ||
	    transport->io.read32(transport->io.context, CRB_CTRL_REQ,
		&request) != CB_SUCCESS ||
	    transport->io.read32(transport->io.context, CRB_CTRL_STS,
		&status) != CB_SUCCESS)
		return CB_ERR;
	if ((status & CRB_CTRL_STS_ERROR) ||
	    (request & ~(CRB_CTRL_REQ_COMMAND_READY |
		CRB_CTRL_REQ_GO_IDLE)) ||
	    (request & CRB_CTRL_REQ_COMMAND_READY &&
	     request & CRB_CTRL_REQ_GO_IDLE) ||
	    (start & CRB_CTRL_START_START &&
	     (request || status & CRB_CTRL_STS_IDLE)))
		return CB_ERR;

	state->request = request;
	state->idle = status & CRB_CTRL_STS_IDLE;
	state->started = start & CRB_CTRL_START_START;
	return CB_SUCCESS;
}

static enum cb_err crb_command_ready_complete(
	const struct tpm2_transport_release *transport, bool *complete)
{
	struct crb_state state;

	if (crb_read_state(transport, &state) != CB_SUCCESS ||
	    state.started || state.request & CRB_CTRL_REQ_GO_IDLE)
		return CB_ERR;
	*complete = !(state.request & CRB_CTRL_REQ_COMMAND_READY) &&
		!state.idle;
	return CB_SUCCESS;
}

static enum cb_err crb_command_complete(
	const struct tpm2_transport_release *transport, bool *complete)
{
	struct crb_state state;

	if (crb_read_state(transport, &state) != CB_SUCCESS || state.request)
		return CB_ERR;
	*complete = !state.started;
	return CB_SUCCESS;
}

static enum cb_err crb_go_idle_complete(
	const struct tpm2_transport_release *transport, bool *complete)
{
	struct crb_state state;

	if (crb_read_state(transport, &state) != CB_SUCCESS ||
	    state.started || state.request & CRB_CTRL_REQ_COMMAND_READY)
		return CB_ERR;
	*complete = !(state.request & CRB_CTRL_REQ_GO_IDLE) && state.idle;
	return CB_SUCCESS;
}

static enum cb_err crb_released(
	const struct tpm2_transport_release *transport, bool *released)
{
	uint32_t locality_state;
	uint32_t locality_status;
	uint32_t active_locality;

	if (transport->io.read32(transport->io.context, CRB_LOC_STATE,
		&locality_state) != CB_SUCCESS ||
	    transport->io.read32(transport->io.context, CRB_LOC_STS,
		&locality_status) != CB_SUCCESS)
		return CB_ERR;
	if (!(locality_state & CRB_LOC_STATE_VALID))
		return CB_ERR;

	active_locality = (locality_state & CRB_LOC_STATE_ACTIVE_MASK) >>
		CRB_LOC_STATE_ACTIVE_SHIFT;
	*released = !(locality_status & CRB_LOC_STS_GRANTED) &&
		(!(locality_state & CRB_LOC_STATE_ASSIGNED) ||
		 active_locality != transport->locality);
	return CB_SUCCESS;
}

static enum cb_err crb_quiesce(
	const struct tpm2_transport_release *transport)
{
	struct crb_state state;

	if (crb_read_state(transport, &state) != CB_SUCCESS)
		return CB_ERR;
	if (state.request & CRB_CTRL_REQ_GO_IDLE)
		return poll(transport, crb_go_idle_complete);
	if (state.request & CRB_CTRL_REQ_COMMAND_READY) {
		if (poll(transport, crb_command_ready_complete) != CB_SUCCESS ||
		    crb_read_state(transport, &state) != CB_SUCCESS)
			return CB_ERR;
	}
	if (state.started) {
		if (poll(transport, crb_command_complete) != CB_SUCCESS ||
		    crb_read_state(transport, &state) != CB_SUCCESS)
			return CB_ERR;
	}
	if (state.idle && !state.request && !state.started)
		return CB_SUCCESS;
	if (state.request || state.started)
		return CB_ERR;
	if (transport->io.write32(transport->io.context, CRB_CTRL_REQ,
		CRB_CTRL_REQ_GO_IDLE) != CB_SUCCESS)
		return CB_ERR;

	return poll(transport, crb_go_idle_complete);
}

static enum cb_err crb_release(
	const struct tpm2_transport_release *transport)
{
	struct crb_state state;

	if (crb_read_state(transport, &state) != CB_SUCCESS ||
	    !state.idle || state.request || state.started)
		return CB_ERR;
	if (transport->io.write32(transport->io.context, CRB_LOC_CTRL,
		CRB_LOC_CTRL_RELINQUISH) != CB_SUCCESS)
		return CB_ERR;

	return poll(transport, crb_released);
}

static enum cb_err fifo_owned(
	const struct tpm2_transport_release *transport)
{
	uint8_t access;

	if (transport->io.read8(transport->io.context, FIFO_ACCESS,
		&access) != CB_SUCCESS)
		return CB_ERR;
	if ((access & (TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY)) !=
		(TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY) ||
	    (access & TPM_ACCESS_BEEN_SEIZED))
		return CB_ERR;

	return CB_SUCCESS;
}

static enum cb_err fifo_idle(
	const struct tpm2_transport_release *transport, bool *idle)
{
	uint8_t status;

	if (fifo_owned(transport) != CB_SUCCESS ||
	    transport->io.read8(transport->io.context, FIFO_STS,
		&status) != CB_SUCCESS)
		return CB_ERR;
	*idle = (status & (TPM_STS_VALID | TPM_STS_COMMAND_READY)) ==
		(TPM_STS_VALID | TPM_STS_COMMAND_READY) &&
		!(status & (TPM_STS_DATA_AVAIL | TPM_STS_DATA_EXPECT));
	return CB_SUCCESS;
}

static enum cb_err fifo_released(
	const struct tpm2_transport_release *transport, bool *released)
{
	uint8_t access;

	if (transport->io.read8(transport->io.context, FIFO_ACCESS,
		&access) != CB_SUCCESS)
		return CB_ERR;
	if (!(access & TPM_ACCESS_VALID))
		return CB_ERR;
	*released = !(access & TPM_ACCESS_ACTIVE_LOCALITY);
	return CB_SUCCESS;
}

static enum cb_err fifo_quiesce(
	const struct tpm2_transport_release *transport)
{
	bool idle;

	if (fifo_idle(transport, &idle) != CB_SUCCESS)
		return CB_ERR;
	if (idle)
		return CB_SUCCESS;

	if (transport->io.write8(transport->io.context, FIFO_STS,
		TPM_STS_COMMAND_READY) != CB_SUCCESS ||
	    fifo_idle(transport, &idle) != CB_SUCCESS)
		return CB_ERR;
	if (idle)
		return CB_SUCCESS;

	/* Some FIFO devices require a second commandReady write. */
	if (transport->io.write8(transport->io.context, FIFO_STS,
		TPM_STS_COMMAND_READY) != CB_SUCCESS)
		return CB_ERR;
	return poll(transport, fifo_idle);
}

static enum cb_err fifo_release(
	const struct tpm2_transport_release *transport)
{
	bool idle;

	if (fifo_idle(transport, &idle) != CB_SUCCESS || !idle)
		return CB_ERR;
	if (transport->io.write8(transport->io.context, FIFO_ACCESS,
		TPM_ACCESS_ACTIVE_LOCALITY) != CB_SUCCESS)
		return CB_ERR;

	return poll(transport, fifo_released);
}

static enum cb_err quiesce(
	const struct tpm2_transport_release *transport)
{
	if (transport->interface == TPM2_TRANSPORT_CRB)
		return crb_quiesce(transport);
	return fifo_quiesce(transport);
}

static enum cb_err release_locality(
	const struct tpm2_transport_release *transport)
{
	if (transport->interface == TPM2_TRANSPORT_CRB)
		return crb_release(transport);
	return fifo_release(transport);
}

enum cb_err tpm2_transport_quiesce(
	const struct tpm2_transport_release *transport)
{
	struct tpm2_transport_release saved;

	if (!transport_valid(transport))
		return CB_ERR_ARG;
	saved = *transport;
	return quiesce(&saved);
}

enum cb_err tpm2_transport_release_locality(
	const struct tpm2_transport_release *transport)
{
	struct tpm2_transport_release saved;

	if (!transport_valid(transport))
		return CB_ERR_ARG;
	saved = *transport;
	return release_locality(&saved);
}

enum cb_err tpm2_transport_quiesce_and_release(
	const struct tpm2_transport_release *transport)
{
	struct tpm2_transport_release saved;

	if (!transport_valid(transport))
		return CB_ERR_ARG;
	saved = *transport;
	if (quiesce(&saved) != CB_SUCCESS)
		return CB_ERR;
	return release_locality(&saved);
}
