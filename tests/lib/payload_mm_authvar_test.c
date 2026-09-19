/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar.h>
#include <string.h>

#define COMM_BYTES 4096U

static uint8_t communication[COMM_BYTES] __aligned(4096);
static struct {
	struct payload_mm_authvar_request request;
	size_t message_size;
	uint8_t message[COMM_BYTES];
} trusted __aligned(4096);

static bool owns_store;
static bool owns_smm;
static bool restricts_spi;
static bool omits_raw_flash;
static bool reserves_communication;
static unsigned int ownership_calls;
static bool protects_authority = true;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static bool platform_fact(void *context)
{
	assert(context == &ownership_calls);
	return true;
}

static bool smm_owned(void *context)
{
	return platform_fact(context) && owns_smm;
}

static bool spi_restricted(void *context)
{
	return platform_fact(context) && restricts_spi;
}

static bool no_raw_flash(void *context)
{
	return platform_fact(context) && omits_raw_flash;
}

static bool communication_reserved(void *context, uint64_t base, uint64_t size)
{
	assert(context == &ownership_calls);
	assert(base == (uintptr_t)communication);
	assert(size == sizeof(communication));
	return reserves_communication;
}

static bool store_owned(void *context, uint64_t offset, uint64_t size)
{
	assert(context == &ownership_calls);
	assert(offset == 0x600000U);
	assert(size == 0x60000U);
	ownership_calls++;
	return owns_store;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	assert(context == &ownership_calls);
	assert(storage != NULL);
	assert(size >= sizeof(struct payload_mm_authvar_contract) + 2);
	return protects_authority;
}

static struct payload_mm_authvar_platform valid_platform(void)
{
	return (struct payload_mm_authvar_platform) {
		.smm_address_bits = 64,
		.generation = 7,
		.smram = { (uintptr_t)&trusted, sizeof(trusted) },
		.communication = {
			(uintptr_t)communication, sizeof(communication)
		},
		.boot_media_size = 0x1000000,
		.store_offset = 0x600000,
		.store_size = 0x60000,
		.block_size = 0x1000,
		.erase_size = 0x10000,
		.smm_entry_owned = smm_owned,
		.spi_writes_restricted_to_smm = spi_restricted,
		.raw_flash_transport_absent = no_raw_flash,
		.communication_region_reserved = communication_reserved,
		.store_region_owned_by_smm = store_owned,
		.context = &ownership_calls,
	};
}

static void enable_platform_facts(void)
{
	owns_store = true;
	owns_smm = true;
	restricts_spi = true;
	omits_raw_flash = true;
	reserves_communication = true;
}

static void contract_mutations(void)
{
	struct payload_mm_authvar_contract output;
	struct payload_mm_authvar_platform platform = valid_platform();
	struct payload_mm_authvar_contract zero = {0};

#define REJECT(member, value) do { \
	platform = valid_platform(); \
	platform.member = (value); \
	memset(&output, 0xa5, sizeof(output)); \
	assert(payload_mm_authvar_contract_build(&output, &platform) == CB_ERR); \
	assert(!memcmp(&output, &zero, sizeof(output))); \
} while (0)
	REJECT(generation, 0);
	REJECT(smm_address_bits, 31);
	REJECT(smm_entry_owned, NULL);
	REJECT(spi_writes_restricted_to_smm, NULL);
	REJECT(raw_flash_transport_absent, NULL);
	REJECT(communication_region_reserved, NULL);
	REJECT(store_region_owned_by_smm, NULL);
	REJECT(communication.base, platform.smram.base);
	REJECT(communication.base, UINT64_MAX - 7U);
	REJECT(communication.size, PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES - 1U);
	REJECT(communication.size, PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES + 1U);
	REJECT(boot_media_size, platform.store_size);
	REJECT(store_offset, platform.boot_media_size - platform.store_size + 1U);
	REJECT(store_offset, platform.store_offset + 1U);
	REJECT(store_size, platform.erase_size * 2U);
	REJECT(block_size, 0x1800);
	REJECT(erase_size, 0x18000);
#undef REJECT

	platform = valid_platform();
	owns_smm = false;
	assert(payload_mm_authvar_contract_build(&output, &platform) == CB_ERR);
	owns_smm = true;
	platform = valid_platform();
	restricts_spi = false;
	assert(payload_mm_authvar_contract_build(&output, &platform) == CB_ERR);
	restricts_spi = true;
	platform = valid_platform();
	omits_raw_flash = false;
	assert(payload_mm_authvar_contract_build(&output, &platform) == CB_ERR);
	omits_raw_flash = true;
	platform = valid_platform();
	reserves_communication = false;
	assert(payload_mm_authvar_contract_build(&output, &platform) == CB_ERR);
	reserves_communication = true;
	platform = valid_platform();
	owns_store = false;
	assert(payload_mm_authvar_contract_build(&output, &platform) == CB_ERR);
	assert(!memcmp(&output, &zero, sizeof(output)));
	owns_store = true;
}

static uint64_t make_request(size_t request_offset, size_t message_offset)
{
	struct payload_mm_authvar_request request = {
		.revision = PAYLOAD_MM_AUTHVAR_REQUEST_REVISION,
		.size = sizeof(request),
		.operation = PAYLOAD_MM_AUTHVAR_COMMUNICATE,
		.generation = 7,
		.message_address = (uintptr_t)&communication[message_offset],
		.message_size = PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES,
	};

	memset(communication, 0, sizeof(communication));
	memset(&communication[message_offset], 0x5a, request.message_size);
	memcpy(&communication[request_offset], &request, sizeof(request));
	return (uintptr_t)&communication[request_offset];
}

static void expect_request(uint64_t address, enum cb_err expected)
{
	struct payload_mm_authvar_request zero = {0};

	memset(&trusted, 0xa5, sizeof(trusted));
	assert(payload_mm_authvar_request_copy(address, &trusted.request,
		trusted.message, sizeof(trusted.message),
		&trusted.message_size) == expected);
	if (expected == CB_SUCCESS) {
		assert(trusted.message_size == PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES);
		for (size_t i = 0; i < trusted.message_size; i++)
			assert(trusted.message[i] == 0x5a);
		((struct payload_mm_authvar_request *)(uintptr_t)address)->generation++;
		communication[512] = 0;
		assert(trusted.request.generation == 7);
		assert(trusted.message[0] == 0x5a);
	} else {
		assert(!trusted.message_size);
		assert(!memcmp(&trusted.request, &zero, sizeof(zero)));
	}
}

static void request_mutations(void)
{
	struct payload_mm_authvar_request *request;
	uint64_t address = make_request(0, 512);

	expect_request(address, CB_SUCCESS);
	expect_request((uintptr_t)communication - 8U, CB_ERR);
	expect_request((uintptr_t)communication + 1U, CB_ERR);
	expect_request(UINT64_MAX - 7U, CB_ERR);
	expect_request((uintptr_t)&communication[COMM_BYTES] -
		sizeof(*request) + 8U, CB_ERR);
	expect_request((uintptr_t)&trusted, CB_ERR);

	address = make_request(COMM_BYTES - sizeof(*request), 512);
	expect_request(address, CB_SUCCESS);

#define REJECT(member, value) do { \
	address = make_request(0, 512); \
	request = (void *)(uintptr_t)address; \
	request->member = (value); \
	expect_request(address, CB_ERR); \
} while (0)
	REJECT(revision, 2);
	REJECT(size, sizeof(*request) - 1U);
	REJECT(operation, 2);
	REJECT(flags, 1);
	REJECT(generation, 8);
	REJECT(message_address, (uintptr_t)communication - 8U);
	REJECT(message_address, (uintptr_t)communication + 1U);
	REJECT(message_address, UINT64_MAX - 7U);
	REJECT(message_address, (uintptr_t)&communication[COMM_BYTES] -
		PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES + 8U);
	REJECT(message_size, PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES - 1U);
	REJECT(message_size, PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES + 1U);
	REJECT(reserved, 1);
#undef REJECT

	address = make_request(0, 512);
	assert(payload_mm_authvar_request_copy(address, &trusted.request,
		trusted.message, PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES - 1U,
		&trusted.message_size) == CB_ERR);
	assert(payload_mm_authvar_request_copy(address,
		(struct payload_mm_authvar_request *)communication,
		trusted.message, sizeof(trusted.message),
		&trusted.message_size) == CB_ERR);
	assert(payload_mm_authvar_request_copy(address, &trusted.request,
		&trusted.request, sizeof(trusted.request),
		&trusted.message_size) == CB_ERR);
}

int main(int argc, char **argv)
{
	struct payload_mm_authvar_contract contract;
	struct payload_mm_authvar_platform platform = valid_platform();

	enable_platform_facts();
	contract_mutations();
	ownership_calls = 0;
	assert(payload_mm_authvar_contract_build(&contract, &platform) == CB_SUCCESS);
	assert(contract.flags == PAYLOAD_MM_AUTHVAR_REQUIRED_FLAGS);
	assert(ownership_calls == 1);
	if (argc > 1) {
		(void)argv;
		protects_authority = false;
		assert(payload_mm_authvar_authority_install(&contract,
			protected_storage, &ownership_calls) == CB_ERR);
		protects_authority = true;
		assert(payload_mm_authvar_authority_install(&contract,
			protected_storage, &ownership_calls) == CB_ERR);
		return 0;
	}
	assert(payload_mm_authvar_authority_install(&contract, protected_storage,
		&ownership_calls) == CB_SUCCESS);
	contract.generation++;
	assert(payload_mm_authvar_authority_install(&contract, protected_storage,
		&ownership_calls) == CB_ERR);
	request_mutations();
	return 0;
}
