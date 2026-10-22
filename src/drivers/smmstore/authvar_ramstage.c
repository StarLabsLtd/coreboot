/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */

#include "authvar.h"
#include <boot/payload_mm_authvar_service.h>
#include <cbmem.h>
#include <commonlib/coreboot_tables.h>
#include <console/console.h>
#include <smm_call.h>
#include <smmstore.h>
#include <string.h>

void lb_authvar_service(struct lb_header *header)
{
	struct lb_authvar_service_endpoint endpoint;
	struct lb_record *record;
	uintptr_t communication;
	size_t communication_size;

	if (smmstore_get_com_buffer(&communication, &communication_size) < 0)
		die("Authenticated variables: communication buffer unavailable\n");
	smm_authvar_endpoint(&endpoint, communication, communication_size);
	if (payload_mm_authvar_service_endpoint_validate(&endpoint) != CB_SUCCESS)
		die("Authenticated variables: invalid communication geometry\n");
	/* Only a successful, synchronous protected-owner initialization admits it. */
	if (call_smm(APM_CNT_AUTHVAR, 0, NULL) != SMMSTORE_RET_SUCCESS)
		die("Authenticated variables: protected owner unavailable\n");
	record = lb_new_record(header);
	memcpy(record, &endpoint, sizeof(endpoint));
}
