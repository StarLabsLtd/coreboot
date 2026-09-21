/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/capsule_broker.h>
#include <boot/coreboot_tables.h>
#include <stdio.h>
#include <string.h>

static struct lb_capsule_broker_endpoint endpoint = {
	.tag = LB_TAG_CAPSULE_BROKER_ENDPOINT,
	.size = sizeof(endpoint),
	.revision = LB_CAPSULE_BROKER_ENDPOINT_REVISION,
	.header_size = sizeof(endpoint),
	.flags = LB_CAPSULE_ENDPOINT_REQUIRED_FLAGS,
	.generation = 7,
	.communication_base = 0x1000,
	.communication_size = CAPSULE_BROKER_TRANSPORT_SIZE,
	.message_size = CAPSULE_BROKER_TRANSPORT_SIZE,
	.staging_base = 0x2000,
	.staging_size = 0x1000,
	.transport = LB_CAPSULE_ENDPOINT_TRANSPORT_APM_IO8,
	.trigger_width = 1,
	.trigger_address = CAPSULE_BROKER_APM_PORT,
	.trigger_value = CAPSULE_BROKER_APM_COMMAND,
};
static struct {
	struct lb_capsule_handoff handoff;
	struct lb_capsule_update_region region;
} handoff_blob;
#define handoff handoff_blob.handoff
static struct lb_efi_fw_info firmware;
static struct lb_capsule_broker_endpoint record;
static typeof(handoff_blob) handoff_record;
static struct lb_capsule_broker_endpoint sealed_endpoint;
static typeof(handoff_blob) sealed_handoff;
static struct lb_efi_fw_info sealed_firmware;
static bool callback_ok = true;
static bool mutate_callback;
static bool mutate_input;
static bool reenter;
static bool fail_publish;
static bool close_callback;
static unsigned int stored_callback_mutation;
static unsigned int records;
static unsigned int callbacks;

#ifdef CAPSULE_BROKER_ENDPOINT_TEST
void capsule_broker_endpoint_test_mutate(unsigned int field);
#endif

int printk(int msg_level, const char *fmt, ...)
{
	(void)msg_level;
	(void)fmt;
	return 0;
}

void mock_assert(const int value, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!value)
		__builtin_trap();
}

struct lb_record *lb_new_record(struct lb_header *header)
{
	(void)header;
	records++;
	return records == 1 ? (void *)&handoff_record : (void *)&record;
}

static enum cb_err ready(const struct lb_capsule_broker_endpoint *candidate,
	const struct lb_capsule_handoff *candidate_handoff, size_t handoff_size,
	const struct lb_efi_fw_info *candidate_firmware)
{
	callbacks++;
	if (memcmp(candidate, &sealed_endpoint, sizeof(*candidate)) ||
	    handoff_size != sizeof(sealed_handoff) ||
	    memcmp(candidate_handoff, &sealed_handoff, handoff_size) ||
	    memcmp(candidate_firmware, &sealed_firmware,
		sizeof(*candidate_firmware)))
		return CB_ERR;
	if (reenter)
		assert(capsule_broker_endpoint_publication_install(&endpoint,
			&handoff, sizeof(handoff_blob), &firmware, ready) == CB_ERR);
	if (mutate_callback)
		((struct lb_capsule_broker_endpoint *)candidate)->generation++;
	if (mutate_input) {
		endpoint.generation++;
		handoff.flags++;
		firmware.version++;
	}
	if (close_callback)
		capsule_broker_endpoint_publication_close();
	if (fail_publish && callbacks == 2)
		return CB_ERR;
	if (stored_callback_mutation && callbacks == 2) {
#ifdef CAPSULE_BROKER_ENDPOINT_TEST
		capsule_broker_endpoint_test_mutate(stored_callback_mutation);
#else
		return CB_ERR;
#endif
	}
	return callback_ok ? CB_SUCCESS : CB_ERR;
}

static void install_expect(enum cb_err expected, size_t handoff_size)
{
	assert(capsule_broker_endpoint_publication_install(&endpoint, &handoff,
		handoff_size, &firmware, ready) == expected);
}

static void initialize(void)
{
	static const uint8_t guid[16] = { 1 };

	memset(&handoff_blob, 0, sizeof(handoff_blob));
	memset(&firmware, 0, sizeof(firmware));
	memcpy(firmware.guid, guid, sizeof(guid));
	firmware.tag = LB_TAG_EFI_FW_INFO;
	firmware.size = sizeof(firmware);
	firmware.version = 2;
	firmware.lowest_supported_version = 1;
	firmware.fw_size = 0x1000;
	handoff = (struct lb_capsule_handoff) {
		.tag = LB_TAG_CAPSULE_HANDOFF,
		.size = sizeof(handoff_blob),
		.revision = LB_CAPSULE_HANDOFF_REVISION,
		.header_size = sizeof(handoff),
		.flags = LB_CAPSULE_HANDOFF_REQUIRED_FLAGS,
		.broker_type = LB_CAPSULE_BROKER_COREBOOT_UPDATE,
		.broker_capabilities = LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES,
		.capsule_format = LB_CAPSULE_FORMAT_FMP_V3,
		.authentication_format = LB_CAPSULE_AUTH_EFI_PKCS7,
		.board_binding_format =
			LB_CAPSULE_BOARD_BINDING_CBFS_BUILD_INFO_V1,
		.payload_format = LB_CAPSULE_PAYLOAD_MSS1_V1,
		.capsule_flags = LB_CAPSULE_FLAGS_PERSIST_RESET,
		.version = 2,
		.lowest_supported_version = 1,
		.image_size = 0x1000,
		.boot_media_size = 0x4000,
		.block_size = 0x1000,
		.erase_size = 0x1000,
		.smmstore_offset = 0x3000,
		.smmstore_size = 0x1000,
		.region_count = 1,
	};
	memcpy(handoff.image_type_guid, guid, sizeof(guid));
	handoff_blob.region = (struct lb_capsule_update_region) {
		.size = 0x1000,
		.flags = LB_CAPSULE_REGION_BIOS,
	};
	sealed_endpoint = endpoint;
	sealed_handoff = handoff_blob;
	sealed_firmware = firmware;
}

#ifdef CAPSULE_BROKER_ENDPOINT_TEST
static void corrupt_stored_handoff(void)
{
	capsule_broker_endpoint_test_mutate(2);
}
#endif

int main(int argc, char **argv)
{
	struct lb_header header;

	assert(argc == 2);
	initialize();
	if (!strcmp(argv[1], "missing")) {
		lb_add_capsule_broker_endpoint(&header);
	} else if (!strcmp(argv[1], "malformed")) {
		handoff.flags = 0;
		install_expect(CB_ERR, sizeof(handoff_blob));
	} else if (!strcmp(argv[1], "short")) {
		install_expect(CB_ERR, sizeof(handoff) - 1);
	} else if (!strcmp(argv[1], "ready-failure")) {
		callback_ok = false;
		install_expect(CB_ERR, sizeof(handoff_blob));
	} else if (!strcmp(argv[1], "callback-mutation")) {
		mutate_callback = true;
		install_expect(CB_ERR, sizeof(handoff_blob));
	} else if (!strcmp(argv[1], "input-mutation")) {
		mutate_input = true;
		install_expect(CB_ERR, sizeof(handoff_blob));
	} else if (!strcmp(argv[1], "callback-close")) {
		close_callback = true;
		install_expect(CB_ERR, sizeof(handoff_blob));
	} else if (!strcmp(argv[1], "closed-before-install")) {
		capsule_broker_endpoint_publication_close();
		install_expect(CB_ERR, sizeof(handoff_blob));
	} else {
		reenter = !strcmp(argv[1], "reentry");
		fail_publish = !strcmp(argv[1], "stale-readiness");
		if (!strncmp(argv[1], "callback-stored-", 16))
			stored_callback_mutation =
				(unsigned int)(argv[1][16] - '0');
		install_expect(CB_SUCCESS, sizeof(handoff_blob));
		if (!strcmp(argv[1], "stored-handoff-mutation")) {
#ifdef CAPSULE_BROKER_ENDPOINT_TEST
			corrupt_stored_handoff();
#else
			__builtin_trap();
#endif
		}
		if (!strcmp(argv[1], "s3"))
			capsule_broker_endpoint_publication_close();
		lb_add_capsule_broker_endpoint(&header);
		lb_add_capsule_broker_endpoint(&header);
	}
	if (!strcmp(argv[1], "happy") || !strcmp(argv[1], "reentry")) {
		assert(records == 2);
		assert(!memcmp(&handoff_record, &handoff_blob,
			sizeof(handoff_blob)));
		assert(!memcmp(&record, &endpoint, sizeof(record)));
	} else {
		assert(records == 0);
	}
	if (!strcmp(argv[1], "missing") || !strcmp(argv[1], "short") ||
	    !strcmp(argv[1], "closed-before-install"))
		assert(callbacks == 0);
	return 0;
}
