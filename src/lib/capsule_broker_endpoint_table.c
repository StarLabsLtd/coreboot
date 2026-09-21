/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker.h>
#include <boot/capsule_update.h>
#include <boot/coreboot_tables.h>
#include <string.h>

struct endpoint_publication {
	struct lb_capsule_broker_endpoint endpoint;
	uint8_t handoff[sizeof(struct lb_capsule_handoff) +
		CAPSULE_UPDATE_MAX_REGIONS *
		sizeof(struct lb_capsule_update_region)];
	size_t handoff_size;
	struct lb_efi_fw_info firmware;
	capsule_broker_endpoint_ready_fn ready_fn;
	bool attempted;
	bool ready;
	bool published;
	bool checking;
	bool closed;
};

static struct endpoint_publication publication;

enum cb_err capsule_broker_endpoint_publication_install(
	const struct lb_capsule_broker_endpoint *endpoint,
	const struct lb_capsule_handoff *handoff, size_t handoff_size,
	const struct lb_efi_fw_info *firmware,
	capsule_broker_endpoint_ready_fn ready)
{
	struct lb_capsule_broker_endpoint endpoint_snapshot;
	struct lb_capsule_broker_endpoint callback_endpoint;
	uint8_t handoff_snapshot[sizeof(*handoff) + CAPSULE_UPDATE_MAX_REGIONS *
		sizeof(handoff->regions[0])];
	struct lb_efi_fw_info firmware_snapshot;

	if (publication.attempted)
		return CB_ERR;
	publication.attempted = true;
	if (!endpoint || !handoff || !firmware || !ready ||
	    handoff_size < sizeof(*handoff) ||
	    handoff_size > sizeof(handoff_snapshot))
		return CB_ERR;
	memcpy(&endpoint_snapshot, endpoint, sizeof(endpoint_snapshot));
	callback_endpoint = endpoint_snapshot;
	memcpy(handoff_snapshot, handoff, handoff_size);
	memcpy(&firmware_snapshot, firmware, sizeof(firmware_snapshot));
	if (capsule_handoff_validate((const void *)handoff_snapshot, handoff_size,
		&firmware_snapshot) != CB_SUCCESS ||
	    capsule_broker_endpoint_validate(&endpoint_snapshot,
		(const void *)handoff_snapshot) != CB_SUCCESS ||
	    ready(&callback_endpoint, (const void *)handoff_snapshot,
		handoff_size, &firmware_snapshot) != CB_SUCCESS ||
	    publication.closed ||
	    memcmp(&callback_endpoint, &endpoint_snapshot,
		sizeof(endpoint_snapshot)) ||
	    memcmp(&endpoint_snapshot, endpoint, sizeof(endpoint_snapshot)) ||
	    memcmp(handoff_snapshot, handoff, handoff_size) ||
	    memcmp(&firmware_snapshot, firmware, sizeof(firmware_snapshot)))
		return CB_ERR;
	publication.endpoint = endpoint_snapshot;
	memcpy(publication.handoff, handoff_snapshot, handoff_size);
	publication.handoff_size = handoff_size;
	publication.firmware = firmware_snapshot;
	publication.ready_fn = ready;
	publication.ready = true;
	return CB_SUCCESS;
}

void capsule_broker_endpoint_publication_close(void)
{
	publication.attempted = true;
	publication.closed = true;
	publication.ready = false;
	publication.published = false;
	memset(&publication.endpoint, 0, sizeof(publication.endpoint));
	memset(publication.handoff, 0, sizeof(publication.handoff));
	publication.handoff_size = 0;
	memset(&publication.firmware, 0, sizeof(publication.firmware));
}

#ifdef CAPSULE_BROKER_ENDPOINT_TEST
void capsule_broker_endpoint_test_mutate(unsigned int field)
{
	switch (field) {
	case 1:
		publication.endpoint.generation++;
		break;
	case 2:
		((struct lb_capsule_handoff *)publication.handoff)->flags ^= 1;
		break;
	case 3:
		publication.firmware.version++;
		break;
	case 4:
		publication.handoff_size--;
		break;
	case 5:
		publication.handoff_size = SIZE_MAX;
		break;
	}
}
#endif

void lb_add_capsule_broker_endpoint(struct lb_header *header)
{
	struct lb_capsule_broker_endpoint *record;
	struct lb_capsule_handoff *handoff;
	struct lb_capsule_broker_endpoint endpoint;
	uint8_t handoff_snapshot[sizeof(publication.handoff)];
	struct lb_efi_fw_info firmware;
	size_t handoff_size;

	if (!publication.ready || publication.published || publication.checking)
		return;
	publication.checking = true;
	handoff_size = publication.handoff_size;
	if (handoff_size < sizeof(struct lb_capsule_handoff) ||
	    handoff_size > sizeof(handoff_snapshot)) {
		publication.checking = false;
		capsule_broker_endpoint_publication_close();
		return;
	}
	endpoint = publication.endpoint;
	memcpy(handoff_snapshot, publication.handoff, handoff_size);
	firmware = publication.firmware;
	if (capsule_handoff_validate((const void *)handoff_snapshot,
		handoff_size, &firmware) != CB_SUCCESS ||
	    capsule_broker_endpoint_validate(&endpoint,
		(const void *)handoff_snapshot) != CB_SUCCESS ||
	    publication.ready_fn(&endpoint, (const void *)handoff_snapshot,
		handoff_size, &firmware) != CB_SUCCESS ||
	    publication.handoff_size != handoff_size ||
	    memcmp(&endpoint, &publication.endpoint, sizeof(endpoint)) ||
	    memcmp(handoff_snapshot, publication.handoff, handoff_size) ||
	    memcmp(&firmware, &publication.firmware, sizeof(firmware))) {
		publication.checking = false;
		capsule_broker_endpoint_publication_close();
		return;
	}
	publication.checking = false;
	publication.published = true;
	handoff = (void *)lb_new_record(header);
	memcpy(handoff, handoff_snapshot, handoff_size);
	record = (void *)lb_new_record(header);
	*record = endpoint;
}
