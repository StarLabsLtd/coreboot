/* SPDX-License-Identifier: GPL-2.0-only */

#include <drivers/pc80/tpm/tpm.h>
#include <security/tpm/fifo_pre_os_lifecycle.h>
#include <security/tpm/tss.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

static unsigned int transmit_calls;
static unsigned int quiesce_calls;
static unsigned int release_calls;
static bool fail_transmit;
static bool fail_validate;
static bool fail_quiesce;
static bool fail_release;
static bool resurrect_on_transmit;
static bool resurrect_on_quiesce;
static bool resurrect_on_release;
static enum tpm_family probe_family = TPM_2;
static tis_sendrecv_fn probe_route;

int printk(int msg_level, const char *fmt, ...)
{
	(void)msg_level;
	(void)fmt;
	return 0;
}

static tpm_result_t fifo_sendrecv(const uint8_t *request, size_t request_size,
	uint8_t *response, size_t *response_size)
{
	transmit_calls++;
	if (resurrect_on_transmit)
		tlcl_tis_sendrecv = fifo_sendrecv;
	if (fail_transmit || !request || !request_size || !response ||
	    !response_size || *response_size < request_size)
		return TPM_CB_FAIL;
	memcpy(response, request, request_size);
	*response_size = request_size;
	return TPM_SUCCESS;
}

static tpm_result_t other_sendrecv(const uint8_t *request, size_t request_size,
	uint8_t *response, size_t *response_size)
{
	(void)request;
	(void)request_size;
	(void)response;
	(void)response_size;
	return TPM_CB_FAIL;
}

bool pc80_tis_is_fifo_route(tis_sendrecv_fn sendrecv)
{
	return sendrecv == fifo_sendrecv;
}

tis_sendrecv_fn pc80_tis_probe(enum tpm_family *family)
{
	if (family)
		*family = probe_family;
	return probe_route;
}

enum cb_err pc80_tis_fifo_quiesce(void)
{
	quiesce_calls++;
	if (resurrect_on_quiesce)
		tlcl_tis_sendrecv = fifo_sendrecv;
	return fail_quiesce ? CB_ERR : CB_SUCCESS;
}

enum cb_err pc80_tis_fifo_validate_idle(void)
{
	return fail_validate ? CB_ERR : CB_SUCCESS;
}

enum cb_err pc80_tis_fifo_release_locality(void)
{
	release_calls++;
	if (resurrect_on_release)
		tlcl_tis_sendrecv = fifo_sendrecv;
	return fail_release ? CB_ERR : CB_SUCCESS;
}

static void prepare(void)
{
	probe_family = TPM_2;
	probe_route = fifo_sendrecv;
}

static void normal(void)
{
	struct tpm_pre_os_lifecycle lifecycle = { 0 };
	struct tpm_pre_os_token token = { 0 };
	const uint8_t request[] = { 0x80, 1, 2, 3 };
	uint8_t response[sizeof(request)] = { 0 };
	size_t response_size = sizeof(response);

	prepare();
	CHECK(tpm2_fifo_pre_os_lifecycle_install(&lifecycle) == CB_SUCCESS);
	CHECK(!tlcl_tis_sendrecv);
	CHECK(quiesce_calls == 1);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_AVAILABLE);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, request,
		sizeof(request), response, &response_size) == CB_SUCCESS);
	CHECK(transmit_calls == 1);
	CHECK(response_size == sizeof(request));
	CHECK(!memcmp(request, response, sizeof(request)));
	CHECK(tpm_pre_os_lifecycle_end(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm2_fifo_pre_os_lifecycle_handoff(&lifecycle) == CB_SUCCESS);
	CHECK(quiesce_calls == 2);
	CHECK(release_calls == 1);
	CHECK(!tlcl_tis_sendrecv);
	CHECK(tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));

	/* Neither a restored pointer nor a second claim can reopen the route. */
	tlcl_tis_sendrecv = fifo_sendrecv;
	CHECK(tpm2_fifo_pre_os_lifecycle_install(&lifecycle) == CB_ERR);
	CHECK(!tlcl_tis_sendrecv);
}

static void rejected_route(const char *scenario)
{
	struct tpm_pre_os_lifecycle lifecycle = { 0 };

	prepare();
	if (!strcmp(scenario, "wrong-family"))
		probe_family = TPM_1;
	else if (!strcmp(scenario, "wrong-route"))
		probe_route = other_sendrecv;
	else if (!strcmp(scenario, "null-route"))
		probe_route = NULL;
	CHECK(tpm2_fifo_pre_os_lifecycle_install(&lifecycle) == CB_ERR);
	CHECK(!tlcl_tis_sendrecv);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_UNBOUND);
}

static void rejected_take(const char *scenario)
{
	struct tpm_pre_os_lifecycle lifecycle = { 0 };

	prepare();
	if (!strcmp(scenario, "null-take")) {
		CHECK(tlcl_lib_init() == TPM_SUCCESS);
		CHECK(tlcl_take_tpm2_fifo_route(NULL) == CB_ERR);
	} else {
		CHECK(tpm2_fifo_pre_os_lifecycle_install(NULL) == CB_ERR);
	}
	CHECK(!tlcl_tis_sendrecv);
	CHECK(tlcl_lib_init() == TPM_CB_NO_DEVICE);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_UNBOUND);
}

static void begin_failure(bool resurrect)
{
	struct tpm_pre_os_lifecycle lifecycle = { 0 };

	prepare();
	fail_quiesce = !resurrect;
	resurrect_on_quiesce = resurrect;
	CHECK(tpm2_fifo_pre_os_lifecycle_install(&lifecycle) == CB_ERR);
	CHECK(!tlcl_tis_sendrecv);
	CHECK(quiesce_calls == 1);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
}

static void operation_failure(const char *scenario)
{
	struct tpm_pre_os_lifecycle lifecycle = { 0 };
	struct tpm_pre_os_token token = { 0 };
	uint8_t request = 1;
	uint8_t response;
	size_t response_size = sizeof(response);

	prepare();
	CHECK(tpm2_fifo_pre_os_lifecycle_install(&lifecycle) == CB_SUCCESS);
	if (!strcmp(scenario, "transmit-failure"))
		fail_transmit = true;
	else if (!strcmp(scenario, "transmit-lost-locality") ||
		 !strcmp(scenario, "transmit-seized-locality"))
		fail_validate = true;
	else
		resurrect_on_transmit = true;
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, &request, 1,
		&response, &response_size) == CB_ERR);
	CHECK(!tlcl_tis_sendrecv);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
	if (fail_validate)
		CHECK(transmit_calls == 0);
}

static void handoff_failure(const char *scenario)
{
	struct tpm_pre_os_lifecycle lifecycle = { 0 };

	prepare();
	CHECK(tpm2_fifo_pre_os_lifecycle_install(&lifecycle) == CB_SUCCESS);
	if (!strcmp(scenario, "quiesce-failure"))
		fail_quiesce = true;
	else if (!strcmp(scenario, "quiesce-resurrection"))
		resurrect_on_quiesce = true;
	else if (!strcmp(scenario, "release-failure"))
		fail_release = true;
	else
		resurrect_on_release = true;
	CHECK(tpm2_fifo_pre_os_lifecycle_handoff(&lifecycle) == CB_ERR);
	CHECK(!tlcl_tis_sendrecv);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
	if (!strcmp(scenario, "quiesce-failure") ||
	    !strcmp(scenario, "quiesce-resurrection"))
		CHECK(release_calls == 0);
	else
		CHECK(release_calls == 1);
}

static void owned_handoff(void)
{
	struct tpm_pre_os_lifecycle lifecycle = { 0 };
	struct tpm_pre_os_token token = { 0 };

	prepare();
	CHECK(tpm2_fifo_pre_os_lifecycle_install(&lifecycle) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm2_fifo_pre_os_lifecycle_handoff(&lifecycle) == CB_ERR);
	CHECK(!tlcl_tis_sendrecv);
	CHECK(quiesce_calls == 1);
	CHECK(release_calls == 0);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
}

int main(int argc, char **argv)
{
	CHECK(argc == 2);
	if (!strcmp(argv[1], "normal"))
		normal();
	else if (!strcmp(argv[1], "wrong-family") ||
		 !strcmp(argv[1], "wrong-route") ||
		 !strcmp(argv[1], "null-route"))
		rejected_route(argv[1]);
	else if (!strcmp(argv[1], "null-take") ||
		 !strcmp(argv[1], "null-lifecycle"))
		rejected_take(argv[1]);
	else if (!strcmp(argv[1], "begin-failure"))
		begin_failure(false);
	else if (!strcmp(argv[1], "begin-resurrection"))
		begin_failure(true);
	else if (!strcmp(argv[1], "transmit-failure") ||
		 !strcmp(argv[1], "transmit-resurrection") ||
		 !strcmp(argv[1], "transmit-lost-locality") ||
		 !strcmp(argv[1], "transmit-seized-locality"))
		operation_failure(argv[1]);
	else if (!strcmp(argv[1], "owned-handoff"))
		owned_handoff();
	else
		handoff_failure(argv[1]);
	return 0;
}
