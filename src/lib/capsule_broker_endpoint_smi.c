/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker.h>

#include "capsule_broker_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Capsule broker SMI endpoint must only be built in SMM"
#endif

enum cb_err capsule_broker_smi_dispatch(uint16_t port, uint8_t value)
{
	struct lb_capsule_broker_endpoint endpoint;

	if (!capsule_broker_endpoint_ready(&endpoint) ||
	    endpoint.transport != LB_CAPSULE_ENDPOINT_TRANSPORT_APM_IO8 ||
	    endpoint.trigger_width != sizeof(value) ||
	    endpoint.trigger_address != port || endpoint.trigger_value != value)
		return CB_ERR;
	return capsule_broker_transport_dispatch();
}
