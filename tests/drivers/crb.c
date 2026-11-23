/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <commonlib/endian.h>
#include <device/mmio.h>
#include <string.h>
#include <timer.h>
#include "../../src/drivers/crb/tpm.h"

#define DATA_ADDRESS (TPM_CRB_BASE_ADDRESS + CRB_REG_DATA_BUFF)
#define DATA_SIZE (CRB_LOCALITY_SIZE - CRB_REG_DATA_BUFF)
#define HEADER_SIZE 10

static const uint8_t command[] = {
	0x80, 0x01, 0, 0, 0, 12, 0, 0, 0x01, 0x44, 0, 0,
};

static struct {
	uint64_t time_us;
	uint32_t locality;
	unsigned int state_reads;
	unsigned int valid_after;
	unsigned int release_after;
	unsigned int status_reads;
	unsigned int grant_after;
	unsigned int access_requests;
	uint64_t command_address;
	uint64_t response_address;
	uint32_t command_capacity;
	uint32_t response_capacity;
	unsigned int ready_requests;
	unsigned int starts;
	unsigned int ptt_registers;
	size_t sent;
	size_t received;
	bool ready;
	uint8_t response[32];
} tpm;

void timer_monotonic_get(struct mono_time *mt)
{
	mono_time_set_usecs(mt, tpm.time_us);
	tpm.time_us += 100000;
}

static uintptr_t register_offset(const volatile void *addr, size_t width)
{
	uintptr_t address = (uintptr_t)addr;

	assert_int_equal(address % width, 0);
	assert_true(address >= TPM_CRB_BASE_ADDRESS && address < DATA_ADDRESS);
	return address - TPM_CRB_BASE_ADDRESS;
}

static void assert_locality_granted(void)
{
	assert_int_equal(tpm.locality & (LOC_STATE_LOC_ASSIGN | LOC_STATE_ACTIVE_LOC_MASK),
			 LOC_STATE_LOC_ASSIGN);
	assert_true(tpm.state_reads >= tpm.valid_after);
	assert_true(tpm.status_reads >= tpm.grant_after);
}

static uint32_t mock_read32(const volatile void *addr)
{
	switch (register_offset(addr, sizeof(uint32_t))) {
	case CRB_REG_LOC_STATE:
		tpm.state_reads++;
		if (tpm.state_reads == tpm.release_after)
			tpm.locality &= ~LOC_STATE_LOC_ASSIGN;
		return tpm.locality | (tpm.state_reads >= tpm.valid_after ?
				       LOC_STATE_REG_VALID_STS : 0);
	case CRB_REG_LOC_STS:
		assert_true(tpm.state_reads >= tpm.valid_after);
		assert_int_equal(tpm.locality & (LOC_STATE_LOC_ASSIGN | LOC_STATE_ACTIVE_LOC_MASK),
				 LOC_STATE_LOC_ASSIGN);
		return ++tpm.status_reads >= tpm.grant_after ? LOC_STS_GRANTED : 0;
	case CRB_REG_START:
	case CRB_REG_STATUS:
		assert_locality_granted();
		return 0;
	case CRB_REG_REQUEST:
		assert_locality_granted();
		tpm.ready = true;
		return 0;
	case 0x58:
		assert_locality_granted();
		assert_false(CONFIG(HAVE_INTEL_PTT));
		return tpm.command_capacity;
	case 0x5c:
		assert_locality_granted();
		assert_false(CONFIG(HAVE_INTEL_PTT));
		return tpm.command_address;
	case 0x60:
		assert_locality_granted();
		assert_false(CONFIG(HAVE_INTEL_PTT));
		return tpm.command_address >> 32;
	case 0x64:
		assert_locality_granted();
		assert_false(CONFIG(HAVE_INTEL_PTT));
		return tpm.response_capacity;
	default:
		fail();
		return 0;
	}
}

static uint64_t mock_read64(const volatile void *addr)
{
	switch (register_offset(addr, sizeof(uint64_t))) {
	case CRB_REG_INTF_ID:
		return CRB_INTF_REG_CAP_CRB | 1;
	case 0x68:
		assert_locality_granted();
		assert_false(CONFIG(HAVE_INTEL_PTT));
		return tpm.response_address;
	default:
		fail();
		return 0;
	}
}

static uint8_t mock_read8(const volatile void *addr)
{
	const uint64_t base = CONFIG(HAVE_INTEL_PTT) ? DATA_ADDRESS : tpm.response_address;
	const uint32_t size = CONFIG(HAVE_INTEL_PTT) ? DATA_SIZE : tpm.response_capacity;

	assert_locality_granted();
	assert_int_equal(tpm.starts, 1);
	assert_int_equal(tpm.ready_requests, 1);
	assert_int_equal((uintptr_t)addr, base + tpm.received);
	assert_true(tpm.received < size && tpm.received < sizeof(tpm.response));
	return tpm.response[tpm.received++];
}

static void mock_write8(volatile void *addr, uint8_t value)
{
	if (addr == CRB_REG(CRB_REG_LOC_CTRL)) {
		assert_true(tpm.state_reads >= tpm.valid_after);
		assert_false(tpm.locality & LOC_STATE_LOC_ASSIGN);
		assert_int_equal(value, LOC_CTRL_REQ_ACCESS);
		tpm.locality = LOC_STATE_LOC_ASSIGN;
		tpm.access_requests++;
		return;
	}

	assert_locality_granted();
	if (addr == CRB_REG(CRB_REG_REQUEST)) {
		assert_int_equal(value, CRB_REG_REQUEST_CMD_RDY);
		tpm.ready = false;
		tpm.ready_requests++;
	} else if (addr == CRB_REG(CRB_REG_START)) {
		assert_int_equal(value, CRB_REG_START_START);
		assert_true(tpm.ready);
		assert_int_equal(tpm.sent, sizeof(command));
		assert_int_equal(tpm.ptt_registers, CONFIG(HAVE_INTEL_PTT) ? 0x1f : 0);
		tpm.starts++;
	} else {
		const uint64_t base = CONFIG(HAVE_INTEL_PTT) ? DATA_ADDRESS : tpm.command_address;
		const uint32_t size = CONFIG(HAVE_INTEL_PTT) ? DATA_SIZE : tpm.command_capacity;

		assert_int_equal(tpm.starts, 0);
		assert_true(tpm.ready);
		assert_int_equal((uintptr_t)addr, base + tpm.sent);
		assert_true(tpm.sent < size && tpm.sent < sizeof(command));
		assert_int_equal(value, command[tpm.sent]);
		tpm.sent++;
	}
}

static void mock_program_ptt(uintptr_t reg)
{
	const unsigned int bit = 1 << ((reg - 0x58) / sizeof(uint32_t));

	assert_locality_granted();
	assert_true(CONFIG(HAVE_INTEL_PTT));
	assert_true(tpm.ready);
	assert_int_equal(tpm.starts, 0);
	assert_false(tpm.ptt_registers & bit);
	tpm.ptt_registers |= bit;
}

static void mock_write32(volatile void *addr, uint32_t value)
{
	const uintptr_t reg = register_offset(addr, sizeof(value));

	switch (reg) {
	case CRB_REG_INTF_ID:
		assert_true(value == CRB_INTF_REG_INTF_SEL || value == CRB_INTF_REG_INTF_LOCK);
		return;
	case 0x58:
	case 0x64:
		assert_int_equal(value, DATA_SIZE);
		break;
	case 0x5c:
		assert_int_equal(value, DATA_ADDRESS);
		break;
	case 0x60:
		assert_int_equal(value, 0);
		break;
	default:
		fail();
		return;
	}

	mock_program_ptt(reg);
}

static void mock_write64(volatile void *addr, uint64_t value)
{
	const uintptr_t reg = register_offset(addr, sizeof(value));

	assert_int_equal(reg, 0x68);
	assert_int_equal(value, DATA_ADDRESS);
	mock_program_ptt(reg);
}

#define read8 mock_read8
#define read32 mock_read32
#define read64 mock_read64
#define write8 mock_write8
#define write32 mock_write32
#define write64 mock_write64
#include "../../src/drivers/crb/tpm.c"
#undef read8
#undef read32
#undef read64
#undef write8
#undef write32
#undef write64

#include <drivers/intel/ptt/ptt.h>

bool ptt_active(void)
{
	return true;
}

static void prepare_response(uint32_t length)
{
	tpm.sent = 0;
	tpm.received = 0;
	tpm.starts = 0;
	tpm.ready_requests = 0;
	tpm.ptt_registers = 0;
	for (size_t i = 0; i < sizeof(tpm.response); i++)
		tpm.response[i] = i + 1;
	tpm.response[0] = 0x80;
	write_at_be32(tpm.response, length, 2);
}

static int setup(void **state)
{
	memset(&tpm, 0, sizeof(tpm));
	tpm.locality = LOC_STATE_LOC_ASSIGN;
	tpm.valid_after = 1;
	tpm.grant_after = 1;
	tpm.command_address = DATA_ADDRESS;
	tpm.response_address = DATA_ADDRESS;
	tpm.command_capacity = sizeof(command);
	tpm.response_capacity = sizeof(tpm.response);
	prepare_response(HEADER_SIZE);
	return 0;
}

static void assert_no_transfer(void)
{
	assert_int_equal(tpm.ready_requests, 0);
	assert_int_equal(tpm.starts, 0);
	assert_int_equal(tpm.sent, 0);
	assert_int_equal(tpm.received, 0);
}

static void test_command_and_response_in_order(void **state)
{
	uint8_t response[64];
	const size_t length = 19;
	tis_sendrecv_fn sendrecv = crb_tis_probe(NULL);
	assert_non_null(sendrecv);

	/* An odd command start and a response ending at the last aperture byte. */
	tpm.command_address = DATA_ADDRESS + CRB_LOCALITY_SIZE + 1;
	tpm.response_address = TPM_CRB_BASE_ADDRESS + CRB_LOCALITY_COUNT * CRB_LOCALITY_SIZE - length;
	tpm.response_capacity = length;
	if (CONFIG(HAVE_INTEL_PTT)) {
		/* PTT uses its fixed buffer instead of these device-provided values. */
		tpm.command_address = UINT64_MAX;
		tpm.response_address = UINT64_MAX;
		tpm.command_capacity = 0;
		tpm.response_capacity = 0;
	}
	prepare_response(length);
	memset(response, 0x5a, sizeof(response));

	size_t capacity = sizeof(response);
	assert_int_equal(sendrecv(command, sizeof(command), response, &capacity), TPM_SUCCESS);
	assert_int_equal(capacity, length);
	assert_int_equal(tpm.received, length);
	assert_memory_equal(response, tpm.response, length);
	assert_int_equal(response[length], 0x5a);
	assert_int_equal(tpm.ready_requests, 2);
	assert_int_equal(tpm.access_requests, 0);
}

static void test_header_only_response(void **state)
{
	uint8_t response[HEADER_SIZE];

	tpm.response_capacity = sizeof(response);
	assert_int_equal(crb_tpm_process_command(command, sizeof(command), response,
						 sizeof(response)), sizeof(response));
	assert_int_equal(tpm.received, sizeof(response));
	assert_memory_equal(response, tpm.response, sizeof(response));
}

static void test_response_length_rejection_and_retry(void **state)
{
	const struct {
		uint32_t length;
		size_t host_capacity;
		uint32_t tpm_capacity;
	} invalid[] = {
		{ HEADER_SIZE - 1, 32, 32 },
		{ 21, 20, 32 },
#if CONFIG(HAVE_INTEL_PTT)
		{ DATA_SIZE + 1, DATA_SIZE + 1, 32 },
#else
		{ 21, 32, 20 },
#endif
		{ UINT32_MAX, 32, 32 },
	};
	uint8_t response[DATA_SIZE + 1], original[DATA_SIZE + 1];
	tis_sendrecv_fn sendrecv = crb_tis_probe(NULL);
	assert_non_null(sendrecv);

	memset(original, 0x5a, sizeof(original));
	for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
		tpm.response_capacity = invalid[i].tpm_capacity;
		prepare_response(invalid[i].length);
		memcpy(response, original, sizeof(response));
		size_t capacity = invalid[i].host_capacity;

		assert_int_equal(sendrecv(command, sizeof(command), response, &capacity), TPM_CB_FAIL);
		assert_int_equal(capacity, invalid[i].host_capacity);
		assert_int_equal(tpm.received, HEADER_SIZE);
		assert_memory_equal(response, original, sizeof(response));
		assert_int_equal(tpm.ready_requests, 2);
	}

	prepare_response(HEADER_SIZE);
	assert_int_equal(crb_tpm_process_command(command, sizeof(command), response,
						 sizeof(response)), HEADER_SIZE);
	assert_int_equal(tpm.received, HEADER_SIZE);
}

static void test_capacity_checked_before_io(void **state)
{
	uint8_t response[HEADER_SIZE];
	const size_t oversized = (CONFIG(HAVE_INTEL_PTT) ? DATA_SIZE : sizeof(command)) + 1;
	uint8_t large_command[DATA_SIZE + 1] = { 0 };

	assert_int_equal(crb_tpm_process_command(command, sizeof(command), response,
						 HEADER_SIZE - 1), 0);
	assert_no_transfer();

	memcpy(large_command, command, sizeof(command));
	assert_int_equal(crb_tpm_process_command(large_command, oversized, response,
						 sizeof(response)), 0);
	assert_no_transfer();

	if (!CONFIG(HAVE_INTEL_PTT)) {
		tpm.response_capacity = HEADER_SIZE - 1;
		assert_int_equal(crb_tpm_process_command(command, sizeof(command), response,
							 sizeof(response)), 0);
		assert_no_transfer();
	}
}

#if !CONFIG(HAVE_INTEL_PTT)
static void test_reject_out_of_window_buffers(void **state)
{
	const struct {
		uint64_t address;
		uint32_t size;
	} invalid[] = {
		{ TPM_CRB_BASE_ADDRESS - 1, 32 },
		{ DATA_ADDRESS - 1, 32 },
		{ DATA_ADDRESS + CRB_LOCALITY_SIZE - 1, 32 },
		{ DATA_ADDRESS, 0 },
		{ DATA_ADDRESS, DATA_SIZE + 1 },
		{ TPM_CRB_BASE_ADDRESS + CRB_LOCALITY_SIZE - 8, 32 },
		{ DATA_ADDRESS + CRB_LOCALITY_COUNT * CRB_LOCALITY_SIZE, 32 },
		{ (1ULL << 32) | DATA_ADDRESS, 32 },
		{ UINT64_MAX - 7, 32 },
	};
	uint8_t response[32];

	for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
		tpm.command_address = invalid[i].address;
		tpm.command_capacity = invalid[i].size;
		assert_int_equal(crb_tpm_process_command(command, sizeof(command), response,
							 sizeof(response)), 0);
		assert_no_transfer();
		tpm.command_address = DATA_ADDRESS;
		tpm.command_capacity = sizeof(command);

		tpm.response_address = invalid[i].address;
		tpm.response_capacity = invalid[i].size;
		assert_int_equal(crb_tpm_process_command(command, sizeof(command), response,
							 sizeof(response)), 0);
		assert_no_transfer();
		tpm.response_address = DATA_ADDRESS;
		tpm.response_capacity = sizeof(response);
	}
}
#endif

static void test_probe_waits_for_validity_and_grant(void **state)
{
	enum tpm_family family = TPM_UNKNOWN;

	/* A stale nonzero active-locality field does not imply assignment. */
	tpm.locality = 4 << 2;
	tpm.valid_after = 3;
	tpm.grant_after = 3;
	tis_sendrecv_fn sendrecv = crb_tis_probe(&family);
	assert_non_null(sendrecv);
	assert_int_equal(family, TPM_2);
	assert_int_equal(tpm.access_requests, 1);
	assert_int_equal(tpm.status_reads, tpm.grant_after);
}

static void test_other_locality_is_left_alone(void **state)
{
	uint8_t response[HEADER_SIZE];

	tpm.locality = LOC_STATE_LOC_ASSIGN | (2 << 2);
	assert_null(crb_tis_probe(NULL));
	assert_int_equal(crb_tpm_process_command(command, sizeof(command), response,
						 sizeof(response)), 0);
	assert_int_equal(tpm.access_requests, 0);
	assert_int_equal(tpm.status_reads, 0);
	assert_no_transfer();

	tpm.release_after = tpm.state_reads + 3;
	assert_int_equal(crb_tpm_process_command(command, sizeof(command), response,
						 sizeof(response)), sizeof(response));
	assert_int_equal(tpm.access_requests, 1);
}

static void test_invalid_locality_times_out(void **state)
{
	uint8_t response[HEADER_SIZE];

	tpm.valid_after = UINT_MAX;
	assert_int_equal(crb_tpm_process_command(command, sizeof(command), response,
						 sizeof(response)), 0);
	assert_true(tpm.state_reads > 1);
	assert_int_equal(tpm.status_reads, 0);
	assert_int_equal(tpm.access_requests, 0);
	assert_no_transfer();
}

static void test_ungranted_locality_times_out(void **state)
{
	uint8_t response[HEADER_SIZE];

	tpm.locality = 0;
	tpm.grant_after = UINT_MAX;
	assert_int_equal(crb_tpm_process_command(command, sizeof(command), response,
						 sizeof(response)), 0);
	assert_int_equal(tpm.access_requests, 1);
	assert_true(tpm.status_reads > 1);
	assert_no_transfer();
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_command_and_response_in_order, setup),
		cmocka_unit_test_setup(test_header_only_response, setup),
		cmocka_unit_test_setup(test_response_length_rejection_and_retry, setup),
		cmocka_unit_test_setup(test_capacity_checked_before_io, setup),
#if !CONFIG(HAVE_INTEL_PTT)
		cmocka_unit_test_setup(test_reject_out_of_window_buffers, setup),
#endif
		cmocka_unit_test_setup(test_probe_waits_for_validity_and_grant, setup),
		cmocka_unit_test_setup(test_other_locality_is_left_alone, setup),
		cmocka_unit_test_setup(test_invalid_locality_times_out, setup),
		cmocka_unit_test_setup(test_ungranted_locality_times_out, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
