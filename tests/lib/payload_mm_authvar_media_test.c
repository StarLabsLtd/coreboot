/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar.h>
#include <boot/payload_mm_authvar_media.h>
#include <boot/payload_mm_authvar_service.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define BLOCK_SIZE 4096U
#define STORE_SIZE (12U * BLOCK_SIZE)

extern char _start[];
extern char _end[];

struct backend {
	uint8_t bytes[STORE_SIZE];
	uint64_t generation;
	size_t begin_calls;
	size_t read_calls;
	size_t program_calls;
	size_t erase_calls;
	size_t sync_calls;
	size_t end_calls;
};

struct port_context {
	struct backend *backend;
};

_Static_assert(_Alignof(struct port_context) <= __BIGGEST_ALIGNMENT__,
	"media cleanup-context alignment is insufficient");

enum fault_mode {
	FAULT_NONE,
	FAULT_BEGIN_ZERO,
	FAULT_BEGIN_ERROR,
	FAULT_BEGIN_INVALID,
	FAULT_BEGIN_REENTER,
	FAULT_BEGIN_FAIL_CLOSED,
	FAULT_READ_SHORT,
	FAULT_READ_ERROR,
	FAULT_READ_INVALID,
	FAULT_READ_REENTER,
	FAULT_READ_FAIL_CLOSED,
	FAULT_VERIFY_READ,
	FAULT_PROGRAM_ERROR_EXACT,
	FAULT_PROGRAM_ERROR_UNCHANGED,
	FAULT_PROGRAM_UNSUPPORTED_UNCHANGED,
	FAULT_PROGRAM_WRITE_PROTECTED,
	FAULT_PROGRAM_PARTIAL,
	FAULT_PROGRAM_MUTATE_INPUT,
	FAULT_PROGRAM_INVALID,
	FAULT_PROGRAM_REENTER,
	FAULT_PROGRAM_FAIL_CLOSED,
	FAULT_PROGRAM_CONTEXT_MUTATION,
	FAULT_PROGRAM_CONTEXT_MUTATION_SEALED_SYNC_FAIL_CLOSED,
	FAULT_ERASE_WRITE_PROTECTED,
	FAULT_ERASE_ERROR_EXACT,
	FAULT_ERASE_ERROR_UNCHANGED,
	FAULT_ERASE_UNSUPPORTED_UNCHANGED,
	FAULT_ERASE_PARTIAL,
	FAULT_ERASE_INVALID,
	FAULT_ERASE_REENTER,
	FAULT_ERASE_FAIL_CLOSED,
	FAULT_ERASE_CONTEXT_MUTATION,
	FAULT_SYNC,
	FAULT_SYNC_REENTER,
	FAULT_SYNC_FAIL_CLOSED,
	FAULT_SYNC_CONTEXT_MUTATION,
	FAULT_END,
	FAULT_END_REENTER,
	FAULT_END_FAIL_CLOSED,
	FAULT_VERIFY_FAIL_CLOSED,
	FAULT_CONTEXT_MUTATION,
};

static struct backend backend;
static uint64_t output_generation;
static uint64_t output_token;
static uint8_t io_buffer[BLOCK_SIZE];
static enum fault_mode fault;
static uint64_t nested_generation;
static uint64_t nested_token;
static const void *captured_context;
static const void *captured_program_buffer;
static const void *captured_end_context;
static void *communication;
static struct port_context test_context;
static struct payload_mm_authvar_media_port test_port;

void mock_assert(int result, const char *expression, const char *file, int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static bool yes(void *context)
{
	(void)context;
	return true;
}

static bool reserve(void *context, uint64_t base, uint64_t size)
{
	(void)context;
	return base && size == BLOCK_SIZE;
}

static bool own_store(void *context, uint64_t offset, uint64_t size)
{
	(void)context;
	return offset == 0x600000 && size == STORE_SIZE;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	(void)context;
	return storage && size;
}

static const void *previous_address(const void *address)
{
	const uintptr_t value = (uintptr_t)address;

	assert(value);
	return (const void *)(value - 1U);
}

static enum payload_mm_authvar_media_result begin(const void *opaque,
	uint64_t *generation)
{
	const struct port_context *context = opaque;
	struct backend *state = context->backend;

	captured_context = opaque;
	assert(!payload_mm_authvar_media_buffer_disjoint(opaque,
		sizeof(struct port_context)));
	state->begin_calls++;
	if (fault == FAULT_BEGIN_REENTER)
		assert(payload_mm_authvar_media_begin(&nested_generation,
			&nested_token) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	if (fault == FAULT_BEGIN_FAIL_CLOSED)
		assert(payload_mm_authvar_media_fail_closed(output_generation,
			output_token) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	if (fault == FAULT_CONTEXT_MUTATION)
		((struct port_context *)context)->backend = NULL;
	if (fault == FAULT_BEGIN_ZERO) {
		*generation = 0;
		return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
	}
	if (fault == FAULT_BEGIN_ERROR)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (fault == FAULT_BEGIN_INVALID)
		return (enum payload_mm_authvar_media_result)99;
	*generation = state->generation;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result read_media(const void *opaque,
	uint32_t offset, void *buffer, size_t size, size_t *completed)
{
	const struct port_context *context = opaque;

	assert(!payload_mm_authvar_media_buffer_disjoint(opaque,
		sizeof(struct port_context)));
	assert(!payload_mm_authvar_media_buffer_disjoint(buffer, size));
	context->backend->read_calls++;
	if (fault == FAULT_READ_REENTER)
		assert(payload_mm_authvar_media_read(output_generation, output_token,
			0, io_buffer, 1) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	if (fault == FAULT_READ_FAIL_CLOSED)
		assert(payload_mm_authvar_media_fail_closed(output_generation,
			output_token) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	if (fault == FAULT_VERIFY_FAIL_CLOSED && context->backend->read_calls == 2)
		assert(payload_mm_authvar_media_fail_closed(output_generation,
			output_token) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	memcpy(buffer, context->backend->bytes + offset, size);
	*completed = fault == FAULT_READ_SHORT ? size - 1 : size;
	if (fault == FAULT_READ_ERROR)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (fault == FAULT_VERIFY_READ && context->backend->read_calls == 2)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (fault == FAULT_READ_INVALID)
		return (enum payload_mm_authvar_media_result)99;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result program(const void *opaque,
	uint32_t offset, const void *buffer, size_t size)
{
	const struct port_context *context = opaque;
	const uint8_t *source = buffer;
	struct backend *state = context->backend;

	captured_program_buffer = buffer;
	assert(!payload_mm_authvar_media_buffer_disjoint(opaque,
		sizeof(struct port_context)));
	assert(!payload_mm_authvar_media_buffer_disjoint(buffer, size));
	state->program_calls++;
	if (fault == FAULT_PROGRAM_REENTER)
		assert(payload_mm_authvar_media_program(output_generation,
			output_token, 0, io_buffer, 1) ==
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	if (fault == FAULT_PROGRAM_FAIL_CLOSED)
		assert(payload_mm_authvar_media_fail_closed(output_generation,
			output_token) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	if (fault == FAULT_PROGRAM_CONTEXT_MUTATION ||
	    fault == FAULT_PROGRAM_CONTEXT_MUTATION_SEALED_SYNC_FAIL_CLOSED)
		((struct port_context *)context)->backend = NULL;
	if (fault == FAULT_PROGRAM_WRITE_PROTECTED)
		return PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED;
	if (fault == FAULT_PROGRAM_ERROR_UNCHANGED)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (fault == FAULT_PROGRAM_UNSUPPORTED_UNCHANGED)
		return PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED;
	if (fault == FAULT_PROGRAM_MUTATE_INPUT)
		((uint8_t *)buffer)[0] ^= 1;
	if (fault == FAULT_PROGRAM_PARTIAL)
		size /= 2;
	for (size_t i = 0; i < size; i++)
		state->bytes[offset + i] &= source[i];
	if (fault == FAULT_PROGRAM_ERROR_EXACT ||
	    fault == FAULT_PROGRAM_PARTIAL)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (fault == FAULT_PROGRAM_INVALID)
		return (enum payload_mm_authvar_media_result)99;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result erase(const void *opaque,
	uint32_t offset, size_t size)
{
	const struct port_context *context = opaque;
	struct backend *state = context->backend;

	assert(!payload_mm_authvar_media_buffer_disjoint(opaque,
		sizeof(struct port_context)));
	state->erase_calls++;
	if (fault == FAULT_ERASE_REENTER)
		assert(payload_mm_authvar_media_erase(output_generation, output_token,
			0, BLOCK_SIZE) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	if (fault == FAULT_ERASE_FAIL_CLOSED)
		assert(payload_mm_authvar_media_fail_closed(output_generation,
			output_token) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	if (fault == FAULT_ERASE_CONTEXT_MUTATION)
		((struct port_context *)context)->backend = NULL;
	if (fault == FAULT_ERASE_WRITE_PROTECTED)
		return PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED;
	if (fault == FAULT_ERASE_ERROR_UNCHANGED)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (fault == FAULT_ERASE_UNSUPPORTED_UNCHANGED)
		return PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED;
	if (fault == FAULT_ERASE_PARTIAL)
		size /= 2;
	memset(state->bytes + offset, 0xff, size);
	if (fault == FAULT_ERASE_ERROR_EXACT)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (fault == FAULT_ERASE_PARTIAL)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (fault == FAULT_ERASE_INVALID)
		return (enum payload_mm_authvar_media_result)99;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result sync_media(const void *opaque)
{
	const struct port_context *context = opaque;
	struct backend *state = context->backend;

	assert(!payload_mm_authvar_media_buffer_disjoint(opaque,
		sizeof(struct port_context)));
	state->sync_calls++;
	if (fault == FAULT_SYNC_REENTER)
		assert(payload_mm_authvar_media_read(output_generation, output_token,
			0, io_buffer, 1) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	if (fault == FAULT_SYNC_FAIL_CLOSED ||
	    fault == FAULT_PROGRAM_CONTEXT_MUTATION_SEALED_SYNC_FAIL_CLOSED)
		assert(payload_mm_authvar_media_fail_closed(output_generation,
			output_token) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	if (fault == FAULT_SYNC_CONTEXT_MUTATION)
		((struct port_context *)context)->backend = NULL;
	if (fault == FAULT_SYNC)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result end(const void *opaque)
{
	const struct port_context *context = opaque;

	captured_end_context = opaque;
	assert(!payload_mm_authvar_media_buffer_disjoint(opaque,
		sizeof(struct port_context)));
	assert((uintptr_t)opaque % _Alignof(struct port_context) == 0);
	context->backend->end_calls++;
	if (fault == FAULT_END_REENTER)
		assert(payload_mm_authvar_media_end(output_generation, output_token) ==
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	if (fault == FAULT_END_FAIL_CLOSED)
		assert(payload_mm_authvar_media_fail_closed(output_generation,
			output_token) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	if (fault == FAULT_END)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static struct payload_mm_authvar_media_port *port(void)
{
	test_context.backend = &backend;
	test_port = (struct payload_mm_authvar_media_port) {
		.revision = PAYLOAD_MM_AUTHVAR_MEDIA_PORT_REVISION,
		.size = sizeof(struct payload_mm_authvar_media_port),
		.begin = begin,
		.read = read_media,
		.program = program,
		.erase = erase,
		.sync = sync_media,
		.end = end,
		.context = &test_context,
		.context_size = sizeof(test_context),
	};
	return &test_port;
}

static struct payload_mm_authvar_media_port *install_authority(void)
{
	communication = mmap(NULL, BLOCK_SIZE, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	struct payload_mm_authvar_platform platform = {
		.smm_address_bits = 64,
		.generation = 7,
		.smram = { (uintptr_t)_start,
			(uintptr_t)_end - (uintptr_t)_start },
		.communication = { (uintptr_t)communication, BLOCK_SIZE },
		.boot_media_size = 0x1000000,
		.store_offset = 0x600000,
		.store_size = STORE_SIZE,
		.block_size = BLOCK_SIZE,
		.erase_size = BLOCK_SIZE,
		.smm_entry_owned = yes,
		.spi_writes_restricted_to_smm = yes,
		.raw_flash_transport_absent = yes,
		.communication_region_reserved = reserve,
		.store_region_owned_by_smm = own_store,
	};
	struct payload_mm_authvar_contract contract;

	assert(communication != MAP_FAILED);
	assert(payload_mm_authvar_contract_build(&contract, &platform) == CB_SUCCESS);
	assert(payload_mm_authvar_authority_install(&contract, protected_storage,
		NULL) == CB_SUCCESS);
	return port();
}

static void install(void)
{
	struct payload_mm_authvar_media_port *media_port = install_authority();

	assert(payload_mm_authvar_media_install(media_port) == CB_SUCCESS);
}

static void invalid_install_case(void)
{
	struct payload_mm_authvar_media_port *media_port = install_authority();

	media_port->revision++;
	assert(payload_mm_authvar_media_install(media_port) == CB_ERR);
	media_port->revision--;
	assert(payload_mm_authvar_media_install(media_port) == CB_ERR);
	assert(!payload_mm_authvar_media_available());
}

static void unprotected_install_case(unsigned int target)
{
	struct payload_mm_authvar_media_port *media_port = install_authority();
	const struct payload_mm_authvar_media_port *candidate = media_port;

	if (target == 1) {
		media_port->context = communication;
		media_port->context_size = sizeof(struct port_context);
	} else if (target == 2) {
		media_port->begin = (payload_mm_authvar_media_begin_fn *)(uintptr_t)
			communication;
	} else {
		memcpy(communication, media_port, sizeof(*media_port));
		candidate = communication;
	}
	assert(payload_mm_authvar_media_install(candidate) == CB_ERR);
	media_port = port();
	assert(payload_mm_authvar_media_install(media_port) == CB_ERR);
	assert(!payload_mm_authvar_media_available());
}

static void reset_backend(void)
{
	memset(&backend, 0, sizeof(backend));
	memset(backend.bytes, 0xff, sizeof(backend.bytes));
	backend.generation = 11;
}

static void start(void)
{
	assert(payload_mm_authvar_media_begin(&output_generation, &output_token) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
}

static void finish(enum payload_mm_authvar_media_result expected)
{
	assert(payload_mm_authvar_media_end(output_generation, output_token) ==
		expected);
}

static void normal_case(void)
{
	struct payload_mm_authvar_media_port *second;

	assert(payload_mm_authvar_media_begin(&output_generation, &output_token) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED);
	install();
	second = port();
	assert(payload_mm_authvar_media_install(second) == CB_ERR);
	start();
	assert(captured_context != NULL);
	memset(communication, 0x5a, 16);
	assert(payload_mm_authvar_media_begin(communication,
		(uint64_t *)((uint8_t *)communication + sizeof(uint64_t))) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	for (size_t i = 0; i < 16; i++)
		assert(((uint8_t *)communication)[i] == 0x5a);
	assert(payload_mm_authvar_media_read(output_generation, output_token, 0,
		(void *)captured_context, sizeof(struct port_context)) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	payload_mm_authvar_media_cache_bind(output_generation, output_token);
	assert(payload_mm_authvar_media_cache_valid(output_generation, output_token));
	memset(io_buffer, 0xa5, 32);
	assert(payload_mm_authvar_media_program(output_generation, output_token,
		0, io_buffer, 32) == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	assert(!payload_mm_authvar_media_cache_valid(output_generation, output_token));
	memset(io_buffer, 0, 32);
	assert(payload_mm_authvar_media_read(output_generation, output_token,
		0, io_buffer, 32) == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	assert(io_buffer[0] == 0xa5);
	assert(payload_mm_authvar_media_erase(output_generation, output_token,
		0, BLOCK_SIZE) == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	finish(PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	assert(backend.begin_calls == 1 && backend.program_calls == 1 &&
		backend.erase_calls == 1 && backend.sync_calls == 2 &&
		backend.end_calls == 1);
	start();
	payload_mm_authvar_media_cache_bind(output_generation, output_token);
	finish(PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	start();
	assert(payload_mm_authvar_media_cache_valid(output_generation, output_token));
	finish(PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	backend.generation++;
	start();
	assert(!payload_mm_authvar_media_cache_valid(output_generation, output_token));
	finish(PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	assert(payload_mm_authvar_media_result_status(
		PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED) ==
		PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
	assert(payload_mm_authvar_media_result_status(
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(payload_mm_authvar_media_result_status(
		PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED) ==
		PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED);
	assert(payload_mm_authvar_media_result_status(
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
}

static void disjoint_case(void)
{
	const uint8_t *context;
	const uint8_t *scratch;

	assert(payload_mm_authvar_media_test_private_spans_rejected());
	assert(!payload_mm_authvar_media_buffer_disjoint(NULL, 1));
	assert(!payload_mm_authvar_media_buffer_disjoint(io_buffer, 0));
	assert(!payload_mm_authvar_media_buffer_disjoint(
		(const void *)UINTPTR_MAX, 2));
	assert(!payload_mm_authvar_media_buffer_disjoint(
		(const void *)1, UINTPTR_MAX));
	assert(payload_mm_authvar_media_buffer_disjoint(io_buffer,
		sizeof(io_buffer)));
	install();
	assert(payload_mm_authvar_media_buffer_disjoint(io_buffer,
		sizeof(io_buffer)));
	start();
	memset(io_buffer, 0xa5, sizeof(io_buffer));
	assert(payload_mm_authvar_media_program(output_generation, output_token,
		0, io_buffer, sizeof(io_buffer)) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	assert(captured_context && captured_program_buffer);
	context = captured_context;
	scratch = captured_program_buffer;
	assert(!payload_mm_authvar_media_buffer_disjoint(context,
		sizeof(struct port_context)));
	assert(!payload_mm_authvar_media_buffer_disjoint(
		previous_address(context), 2));
	assert(!payload_mm_authvar_media_buffer_disjoint(context +
		sizeof(struct port_context) - 1U, 2));
	assert(!payload_mm_authvar_media_buffer_disjoint(scratch, BLOCK_SIZE));
	assert(!payload_mm_authvar_media_buffer_disjoint(
		previous_address(scratch), 2));
	assert(!payload_mm_authvar_media_buffer_disjoint(scratch + BLOCK_SIZE - 1U,
		2));
	assert(payload_mm_authvar_media_buffer_disjoint(scratch + BLOCK_SIZE, 1));
	finish(PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	assert(captured_end_context);
	assert(!payload_mm_authvar_media_buffer_disjoint(captured_end_context,
		sizeof(struct port_context)));
	assert(!payload_mm_authvar_media_buffer_disjoint(
		previous_address(captured_end_context), 2));
}

static void begin_fault_case(enum fault_mode mode)
{
	install();
	fault = mode;
	assert(payload_mm_authvar_media_begin(&output_generation, &output_token) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(output_generation == 0 && output_token == 0);
	assert(backend.end_calls == (mode == FAULT_BEGIN_ZERO ||
		mode == FAULT_BEGIN_REENTER || mode == FAULT_CONTEXT_MUTATION));
}

static void read_fault_case(enum fault_mode mode)
{
	install();
	start();
	fault = mode;
	memset(io_buffer, 0x5a, 16);
	assert(payload_mm_authvar_media_read(output_generation, output_token, 0,
		io_buffer, 16) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	for (size_t i = 0; i < 16; i++)
		assert(io_buffer[i] == 0);
	finish(mode == FAULT_READ_INVALID || mode == FAULT_READ_REENTER ||
		mode == FAULT_READ_FAIL_CLOSED ?
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR :
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
}

static void program_fault_case(enum fault_mode mode)
{
	install();
	start();
	fault = mode;
	memset(io_buffer, 0xa5, 32);
	enum payload_mm_authvar_media_result result =
		payload_mm_authvar_media_program(output_generation, output_token, 0,
			io_buffer, 32);
	if (mode == FAULT_PROGRAM_ERROR_EXACT) {
		assert(result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	} else if (mode == FAULT_PROGRAM_WRITE_PROTECTED) {
		assert(result == PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED);
	} else {
		assert(result == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	}
	if (mode == FAULT_PROGRAM_REENTER ||
	    mode == FAULT_PROGRAM_CONTEXT_MUTATION ||
	    mode == FAULT_SYNC_REENTER)
		assert(backend.sync_calls == 1 && backend.read_calls == 2);
	if (mode == FAULT_SYNC_CONTEXT_MUTATION)
		assert(backend.sync_calls == 2 && backend.read_calls == 2);
	finish(mode == FAULT_PROGRAM_PARTIAL ||
		mode == FAULT_PROGRAM_MUTATE_INPUT ||
		mode == FAULT_PROGRAM_INVALID || mode == FAULT_PROGRAM_REENTER ||
		mode == FAULT_PROGRAM_CONTEXT_MUTATION || mode == FAULT_SYNC ||
		mode == FAULT_SYNC_REENTER || mode == FAULT_SYNC_CONTEXT_MUTATION ||
		mode == FAULT_VERIFY_READ ?
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR :
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
}

static void erase_fault_case(enum fault_mode mode)
{
	install();
	memset(backend.bytes, 0, BLOCK_SIZE);
	start();
	fault = mode;
	enum payload_mm_authvar_media_result result =
		payload_mm_authvar_media_erase(output_generation, output_token, 0,
			BLOCK_SIZE);
	if (mode == FAULT_ERASE_WRITE_PROTECTED) {
		assert(result == PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED);
	} else if (mode == FAULT_ERASE_ERROR_EXACT) {
		assert(result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	} else {
		assert(result == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	}
	assert(backend.sync_calls ==
		(mode == FAULT_SYNC_CONTEXT_MUTATION ? 2 : 1));
	assert(backend.read_calls == 2);
	finish(mode == FAULT_ERASE_WRITE_PROTECTED ||
		mode == FAULT_ERASE_ERROR_EXACT ?
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS :
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
}

static void unchanged_result_case(bool erase_operation, enum fault_mode mode,
	enum payload_mm_authvar_media_result expected)
{
	install();
	memset(io_buffer, 0xa5, 32);
	if (!erase_operation)
		memcpy(backend.bytes, io_buffer, 32);
	start();
	fault = mode;
	if (erase_operation) {
		assert(payload_mm_authvar_media_erase(output_generation, output_token,
			0, BLOCK_SIZE) == expected);
	} else {
		assert(payload_mm_authvar_media_program(output_generation, output_token,
			0, io_buffer, 32) == expected);
	}
	assert(backend.sync_calls == 1 && backend.read_calls == 2);
	finish(PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
}

static void bounds_case(void)
{
	install();
	assert(payload_mm_authvar_media_begin(&output_generation,
		&output_generation) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	start();
	assert(payload_mm_authvar_media_end(output_generation, output_token + 1) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(backend.end_calls == 0);
	assert(payload_mm_authvar_media_read(output_generation + 1, output_token,
		0, io_buffer, 1) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	memset(io_buffer, 0x5a, sizeof(io_buffer));
	assert(payload_mm_authvar_media_read(output_generation, output_token,
		STORE_SIZE, io_buffer, 1) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(io_buffer[0] == 0);
	assert(payload_mm_authvar_media_read(output_generation, output_token,
		0, io_buffer, 0) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(payload_mm_authvar_media_erase(output_generation, output_token,
		1, BLOCK_SIZE) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(payload_mm_authvar_media_erase(output_generation, output_token,
		0, BLOCK_SIZE * 2U) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	backend.bytes[0] = 0;
	io_buffer[0] = 0xff;
	assert(payload_mm_authvar_media_program(output_generation, output_token,
		0, io_buffer, 1) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(backend.program_calls == 0);
	finish(PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
}

static void fail_closed_case(bool valid_generation, bool valid_token)
{
	size_t reads;
	size_t programs;
	size_t erases;

	install();
	start();
	payload_mm_authvar_media_cache_bind(output_generation, output_token);
	assert(payload_mm_authvar_media_cache_valid(output_generation, output_token));
	reads = backend.read_calls;
	programs = backend.program_calls;
	erases = backend.erase_calls;
	assert(payload_mm_authvar_media_fail_closed(
		valid_generation ? output_generation : output_generation + 1U,
		valid_token ? output_token : output_token + 1U) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(!payload_mm_authvar_media_available());
	assert(!payload_mm_authvar_media_cache_valid(output_generation, output_token));
	assert(payload_mm_authvar_media_read(output_generation, output_token, 0,
		io_buffer, 1) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(payload_mm_authvar_media_program(output_generation, output_token, 0,
		io_buffer, 1) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(payload_mm_authvar_media_erase(output_generation, output_token, 0,
		BLOCK_SIZE) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(backend.read_calls == reads && backend.program_calls == programs &&
		backend.erase_calls == erases);
	finish(PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(backend.end_calls == 1);
	assert(payload_mm_authvar_media_begin(&output_generation, &output_token) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(backend.begin_calls == 1 && backend.end_calls == 1);
}

static void idle_fail_closed_case(void)
{
	install();
	assert(payload_mm_authvar_media_fail_closed(0, 0) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(!payload_mm_authvar_media_available());
	assert(payload_mm_authvar_media_begin(&output_generation, &output_token) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(backend.begin_calls == 0 && backend.end_calls == 0);
}

static void preinstall_fail_closed_case(void)
{
	struct payload_mm_authvar_media_port *media_port;

	assert(payload_mm_authvar_media_fail_closed(0, 0) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	media_port = install_authority();
	assert(payload_mm_authvar_media_install(media_port) == CB_ERR);
	assert(!payload_mm_authvar_media_available());
	assert(payload_mm_authvar_media_begin(&output_generation, &output_token) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED);
	assert(backend.begin_calls == 0 && backend.end_calls == 0);
}

static void repeated_fail_closed_case(void)
{
	install();
	start();
	payload_mm_authvar_media_cache_bind(output_generation, output_token);
	assert(payload_mm_authvar_media_fail_closed(output_generation,
		output_token) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(payload_mm_authvar_media_fail_closed(output_generation,
		output_token) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(payload_mm_authvar_media_fail_closed(output_generation + 1U,
		output_token + 1U) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(!payload_mm_authvar_media_available());
	assert(!payload_mm_authvar_media_cache_valid(output_generation, output_token));
	finish(PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(backend.begin_calls == 1 && backend.read_calls == 0 &&
		backend.program_calls == 0 && backend.erase_calls == 0 &&
		backend.sync_calls == 0 && backend.end_calls == 1);
}

static void callback_fail_closed_case(enum fault_mode mode)
{
	install();
	fault = mode;
	if (mode == FAULT_BEGIN_FAIL_CLOSED) {
		assert(payload_mm_authvar_media_begin(&output_generation,
			&output_token) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
		assert(backend.begin_calls == 1 && backend.read_calls == 0 &&
			backend.program_calls == 0 && backend.erase_calls == 0 &&
			backend.sync_calls == 0 && backend.end_calls == 1);
	} else {
		start();
		if (mode == FAULT_READ_FAIL_CLOSED) {
			assert(payload_mm_authvar_media_read(output_generation,
				output_token, 0, io_buffer, 1) ==
				PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
			assert(backend.read_calls == 1 && backend.program_calls == 0 &&
				backend.erase_calls == 0 && backend.sync_calls == 0);
		} else if (mode == FAULT_PROGRAM_FAIL_CLOSED ||
			   mode == FAULT_SYNC_FAIL_CLOSED ||
			   mode == FAULT_VERIFY_FAIL_CLOSED ||
			   mode ==
				FAULT_PROGRAM_CONTEXT_MUTATION_SEALED_SYNC_FAIL_CLOSED) {
			memset(io_buffer, 0xa5, 32);
			assert(payload_mm_authvar_media_program(output_generation,
				output_token, 0, io_buffer, 32) ==
				PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
			assert(backend.program_calls == 1 && backend.erase_calls == 0);
			assert(backend.sync_calls ==
				(mode == FAULT_PROGRAM_FAIL_CLOSED ? 0 : 1));
			assert(backend.read_calls ==
				(mode == FAULT_VERIFY_FAIL_CLOSED ? 2 : 1));
		} else if (mode == FAULT_ERASE_FAIL_CLOSED) {
			memset(backend.bytes, 0, BLOCK_SIZE);
			assert(payload_mm_authvar_media_erase(output_generation,
				output_token, 0, BLOCK_SIZE) ==
				PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
			assert(backend.read_calls == 1 && backend.program_calls == 0 &&
				backend.erase_calls == 1 && backend.sync_calls == 0);
		} else {
			assert(mode == FAULT_END_FAIL_CLOSED);
		}
		finish(PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
		assert(backend.end_calls == 1);
	}
	assert(!payload_mm_authvar_media_available());
	assert(payload_mm_authvar_media_begin(&output_generation, &output_token) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(backend.begin_calls == 1 && backend.end_calls == 1);
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "normal";

	reset_backend();
	if (!strcmp(name, "normal"))
		normal_case();
	else if (!strcmp(name, "disjoint"))
		disjoint_case();
	else if (!strcmp(name, "install-invalid"))
		invalid_install_case();
	else if (!strcmp(name, "install-unprotected-port"))
		unprotected_install_case(0);
	else if (!strcmp(name, "install-unprotected-context"))
		unprotected_install_case(1);
	else if (!strcmp(name, "install-unprotected-callback"))
		unprotected_install_case(2);
	else if (!strcmp(name, "bounds"))
		bounds_case();
	else if (!strcmp(name, "fail-closed"))
		fail_closed_case(true, true);
	else if (!strcmp(name, "fail-closed-wrong-generation"))
		fail_closed_case(false, true);
	else if (!strcmp(name, "fail-closed-wrong-token"))
		fail_closed_case(true, false);
	else if (!strcmp(name, "fail-closed-idle"))
		idle_fail_closed_case();
	else if (!strcmp(name, "fail-closed-preinstall"))
		preinstall_fail_closed_case();
	else if (!strcmp(name, "fail-closed-repeated"))
		repeated_fail_closed_case();
	else if (!strcmp(name, "fail-closed-begin-callback"))
		callback_fail_closed_case(FAULT_BEGIN_FAIL_CLOSED);
	else if (!strcmp(name, "fail-closed-read-callback"))
		callback_fail_closed_case(FAULT_READ_FAIL_CLOSED);
	else if (!strcmp(name, "fail-closed-program-callback"))
		callback_fail_closed_case(FAULT_PROGRAM_FAIL_CLOSED);
	else if (!strcmp(name, "fail-closed-erase-callback"))
		callback_fail_closed_case(FAULT_ERASE_FAIL_CLOSED);
	else if (!strcmp(name, "fail-closed-sync-callback"))
		callback_fail_closed_case(FAULT_SYNC_FAIL_CLOSED);
	else if (!strcmp(name, "fail-closed-verify-callback"))
		callback_fail_closed_case(FAULT_VERIFY_FAIL_CLOSED);
	else if (!strcmp(name, "fail-closed-sealed-sync-callback"))
		callback_fail_closed_case(
			FAULT_PROGRAM_CONTEXT_MUTATION_SEALED_SYNC_FAIL_CLOSED);
	else if (!strcmp(name, "fail-closed-end-callback"))
		callback_fail_closed_case(FAULT_END_FAIL_CLOSED);
	else if (!strcmp(name, "begin-zero"))
		begin_fault_case(FAULT_BEGIN_ZERO);
	else if (!strcmp(name, "begin-error"))
		begin_fault_case(FAULT_BEGIN_ERROR);
	else if (!strcmp(name, "begin-invalid"))
		begin_fault_case(FAULT_BEGIN_INVALID);
	else if (!strcmp(name, "begin-reenter"))
		begin_fault_case(FAULT_BEGIN_REENTER);
	else if (!strcmp(name, "context-mutation"))
		begin_fault_case(FAULT_CONTEXT_MUTATION);
	else if (!strcmp(name, "read-short"))
		read_fault_case(FAULT_READ_SHORT);
	else if (!strcmp(name, "read-error"))
		read_fault_case(FAULT_READ_ERROR);
	else if (!strcmp(name, "read-invalid"))
		read_fault_case(FAULT_READ_INVALID);
	else if (!strcmp(name, "read-reenter"))
		read_fault_case(FAULT_READ_REENTER);
	else if (!strcmp(name, "read-fail-closed"))
		read_fault_case(FAULT_READ_FAIL_CLOSED);
	else if (!strcmp(name, "program-error-exact"))
		program_fault_case(FAULT_PROGRAM_ERROR_EXACT);
	else if (!strcmp(name, "program-error-unchanged"))
		unchanged_result_case(false, FAULT_PROGRAM_ERROR_UNCHANGED,
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	else if (!strcmp(name, "program-unsupported-unchanged"))
		unchanged_result_case(false, FAULT_PROGRAM_UNSUPPORTED_UNCHANGED,
			PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED);
	else if (!strcmp(name, "program-wp-unchanged"))
		unchanged_result_case(false, FAULT_PROGRAM_WRITE_PROTECTED,
			PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED);
	else if (!strcmp(name, "program-wp"))
		program_fault_case(FAULT_PROGRAM_WRITE_PROTECTED);
	else if (!strcmp(name, "program-partial"))
		program_fault_case(FAULT_PROGRAM_PARTIAL);
	else if (!strcmp(name, "program-mutate"))
		program_fault_case(FAULT_PROGRAM_MUTATE_INPUT);
	else if (!strcmp(name, "program-invalid"))
		program_fault_case(FAULT_PROGRAM_INVALID);
	else if (!strcmp(name, "program-reenter"))
		program_fault_case(FAULT_PROGRAM_REENTER);
	else if (!strcmp(name, "program-context-mutation"))
		program_fault_case(FAULT_PROGRAM_CONTEXT_MUTATION);
	else if (!strcmp(name, "program-sync"))
		program_fault_case(FAULT_SYNC);
	else if (!strcmp(name, "program-sync-reenter"))
		program_fault_case(FAULT_SYNC_REENTER);
	else if (!strcmp(name, "program-sync-context-mutation"))
		program_fault_case(FAULT_SYNC_CONTEXT_MUTATION);
	else if (!strcmp(name, "program-postread"))
		program_fault_case(FAULT_VERIFY_READ);
	else if (!strcmp(name, "erase-wp"))
		erase_fault_case(FAULT_ERASE_WRITE_PROTECTED);
	else if (!strcmp(name, "erase-error-exact"))
		erase_fault_case(FAULT_ERASE_ERROR_EXACT);
	else if (!strcmp(name, "erase-error-unchanged"))
		unchanged_result_case(true, FAULT_ERASE_ERROR_UNCHANGED,
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	else if (!strcmp(name, "erase-unsupported-unchanged"))
		unchanged_result_case(true, FAULT_ERASE_UNSUPPORTED_UNCHANGED,
			PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED);
	else if (!strcmp(name, "erase-wp-unchanged"))
		unchanged_result_case(true, FAULT_ERASE_WRITE_PROTECTED,
			PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED);
	else if (!strcmp(name, "erase-partial"))
		erase_fault_case(FAULT_ERASE_PARTIAL);
	else if (!strcmp(name, "erase-invalid"))
		erase_fault_case(FAULT_ERASE_INVALID);
	else if (!strcmp(name, "erase-reenter"))
		erase_fault_case(FAULT_ERASE_REENTER);
	else if (!strcmp(name, "erase-context-mutation"))
		erase_fault_case(FAULT_ERASE_CONTEXT_MUTATION);
	else if (!strcmp(name, "erase-sync"))
		erase_fault_case(FAULT_SYNC);
	else if (!strcmp(name, "erase-sync-context-mutation"))
		erase_fault_case(FAULT_SYNC_CONTEXT_MUTATION);
	else if (!strcmp(name, "erase-postread"))
		erase_fault_case(FAULT_VERIFY_READ);
	else if (!strcmp(name, "end-error")) {
		install();
		start();
		fault = FAULT_END;
		finish(PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	} else if (!strcmp(name, "end-reenter")) {
		install();
		start();
		fault = FAULT_END_REENTER;
		finish(PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	} else
		abort();
	return 0;
}
