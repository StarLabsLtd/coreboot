/* SPDX-License-Identifier: GPL-2.0-only */

#if !ENV_TEST
#include <bootstate.h>
#endif
#include <console/console.h>
#include <drivers/pc80/tpm/tpm.h>
#include <security/tpm/fifo_pre_os_lifecycle.h>
#include <security/tpm/tss.h>
#include <string.h>

#if ENV_SMM && !ENV_TEST
#error "The TPM2 FIFO pre-OS owner must not be built in SMM"
#endif

struct fifo_backend_context {
	tis_sendrecv_fn sendrecv;
	uint32_t seal;
};

#define FIFO_BACKEND_SEAL 0x4649464fU

static bool legacy_route_revoked(void)
{
	return tlcl_tis_route_is_taken();
}

static enum cb_err begin(void *context)
{
	const struct fifo_backend_context *fifo = context;

	if (!fifo || fifo->seal != FIFO_BACKEND_SEAL || !fifo->sendrecv ||
	    !pc80_tis_is_fifo_route(fifo->sendrecv) || !legacy_route_revoked())
		return CB_ERR;
	if (pc80_tis_fifo_quiesce() != CB_SUCCESS || !legacy_route_revoked())
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err transmit(void *context, const uint8_t *request,
	size_t request_size, uint8_t *response, size_t *response_size)
{
	const struct fifo_backend_context *fifo = context;
	tis_sendrecv_fn sendrecv;
	tpm_result_t result;

	if (!fifo || fifo->seal != FIFO_BACKEND_SEAL || !fifo->sendrecv ||
	    !legacy_route_revoked())
		return CB_ERR;
	/* ACCESS is revalidated after STS immediately before touching the FIFO. */
	if (pc80_tis_fifo_validate_idle() != CB_SUCCESS)
		return CB_ERR;
	sendrecv = fifo->sendrecv;
	result = sendrecv(request, request_size, response, response_size);
	if (result != TPM_SUCCESS || !legacy_route_revoked())
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err quiesce(void *context)
{
	const struct fifo_backend_context *fifo = context;

	if (!fifo || fifo->seal != FIFO_BACKEND_SEAL || !fifo->sendrecv ||
	    !legacy_route_revoked())
		return CB_ERR;
	if (pc80_tis_fifo_quiesce() != CB_SUCCESS || !legacy_route_revoked())
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err release(void *context)
{
	const struct fifo_backend_context *fifo = context;

	if (!fifo || fifo->seal != FIFO_BACKEND_SEAL || !fifo->sendrecv ||
	    !legacy_route_revoked())
		return CB_ERR;
	if (pc80_tis_fifo_release_locality() != CB_SUCCESS ||
	    !legacy_route_revoked())
		return CB_ERR;
	return CB_SUCCESS;
}

enum cb_err tpm2_fifo_pre_os_lifecycle_install(
	struct tpm_pre_os_lifecycle *lifecycle)
{
	static const struct tpm_pre_os_backend backend = {
		.begin = begin,
		.transmit = transmit,
		.quiesce = quiesce,
		.release = release,
	};
	struct fifo_backend_context context = { 0 };

	if (tlcl_lib_init() != TPM_SUCCESS) {
		printk(BIOS_ERR, "TPM2 FIFO: legacy initialization failed\n");
		return CB_ERR;
	}
	if (tlcl_take_tpm2_fifo_route(&context.sendrecv) != CB_SUCCESS) {
		printk(BIOS_ERR, "TPM2 FIFO: legacy route transfer failed\n");
		return CB_ERR;
	}

	context.seal = FIFO_BACKEND_SEAL;
	if (tpm_pre_os_lifecycle_install(lifecycle, &backend, &context,
		sizeof(context)) != CB_SUCCESS) {
		printk(BIOS_ERR, "TPM2 FIFO: lifecycle installation failed\n");
		memset(&context, 0, sizeof(context));
		return CB_ERR;
	}
	memset(&context, 0, sizeof(context));
	return CB_SUCCESS;
}

enum cb_err tpm2_fifo_pre_os_lifecycle_handoff(
	struct tpm_pre_os_lifecycle *lifecycle)
{
	enum cb_err result;

	if (!legacy_route_revoked())
		return CB_ERR;
	result = tpm_pre_os_lifecycle_handoff(lifecycle);
	if (!legacy_route_revoked())
		return CB_ERR;
	return result;
}

#if !ENV_TEST
static struct tpm_pre_os_lifecycle pre_os_lifecycle;

static void install_owner(void *unused)
{
	if (tpm2_fifo_pre_os_lifecycle_install(&pre_os_lifecycle) != CB_SUCCESS)
		die("TPM2 FIFO: failed to install pre-OS owner\n");
}

static void handoff_owner(void *unused)
{
	if (tpm2_fifo_pre_os_lifecycle_handoff(&pre_os_lifecycle) != CB_SUCCESS)
		die("TPM2 FIFO: failed to relinquish pre-OS owner\n");
}

/* Payload loading and its measurements are complete before ownership moves. */
BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_LOAD, BS_ON_EXIT, install_owner, NULL);
BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY, handoff_owner, NULL);
#endif
