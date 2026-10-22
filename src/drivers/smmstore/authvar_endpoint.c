/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */

#include "authvar.h"
#include <boot/payload_mm_authvar_store.h>
#include <commonlib/coreboot_tables.h>
#include <cpu/x86/smm.h>

void smm_authvar_endpoint(struct lb_authvar_service_endpoint *endpoint,
	uintptr_t communication, size_t size)
{
	const size_t name_size = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE;
	const size_t data_offset = (PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE + name_size + 7U) &
		~(size_t)7U;

	*endpoint = (struct lb_authvar_service_endpoint) {
		.tag = LB_TAG_AUTHVAR_SERVICE_ENDPOINT,
		.size = sizeof(*endpoint),
		.revision = LB_AUTHVAR_SERVICE_ENDPOINT_REVISION,
		.header_size = sizeof(*endpoint),
		.flags = LB_AUTHVAR_ENDPOINT_REQUIRED_FLAGS |
			LB_AUTHVAR_ENDPOINT_IMAGE_POLICY_GENERAL |
			LB_AUTHVAR_ENDPOINT_STATE_PREDICATE_PINNED,
		/* The installed owner has one lifetime and cannot be reinitialized. */
		.generation = 1,
		.communication_base = communication,
		.communication_size = size,
		.message_size = size,
		.transport = LB_AUTHVAR_ENDPOINT_TRANSPORT_APM_IO8,
		.trigger_width = sizeof(uint8_t),
		.trigger_address = pm_acpi_smi_cmd_port(),
		.trigger_value = APM_CNT_AUTHVAR,
		.maximum_name_size = name_size,
		.maximum_data_size = size > data_offset ? size - data_offset : 0,
	};
}
