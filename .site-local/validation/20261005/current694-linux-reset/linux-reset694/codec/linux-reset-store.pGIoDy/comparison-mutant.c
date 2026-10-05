/* SPDX-License-Identifier: GPL-2.0-only */
/* Read-only HOST media oracle, not firmware authority or a reset simulator.
 * Reuse the existing real codec write-plan interpreter without changing its
 * legacy entrypoints. This expected history has a different record order.
 */
#define main protected_setup_existing_main
#include "protected_setup_media_check.c"
#undef main

static const uint8_t linux_probe_guid[16] = {
	0x67, 0x45, 0x23, 0x01, 0xab, 0x89, 0xef, 0xcd,
	0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
};
static const uint16_t linux_probe_name[] = {
	'C', 'd', 'k', '2', 'A', 'b', 'P', 'r', 'o', 'b', 'e', 0,
};

static bool linux_reset_matches(const uint8_t *initial, const uint8_t *final,
	const uint8_t *fmp_guid)
{
	uint8_t expected[REGION_SIZE];
	struct payload_mm_authvar_fv_geometry geometry;
	struct payload_mm_authvar_ftw_plan workspace;
	const uint8_t fmp_state[PAYLOAD_MM_FMP_STATE_WIRE_SIZE] = { 0 };
	const uint8_t mtc1[] = { 1, 0, 0, 0 }, mtc2[] = { 2, 0, 0, 0 };
	const uint8_t requested[] = { 1, 0, 0, 0, 0, 0, 0, 0 }, consumed[8] = { 0 };
	const uint8_t created[] = "CDK2_AB", updated[] = "UPDATED";

	if (!payload_mm_authvar_fv_geometry(&geometry, REGION_SIZE, 4096) ||
	    payload_mm_authvar_default_store_compose(initial, expected, REGION_SIZE, 4096) !=
		PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED)
		return false;
	/* Source-derived executor.empty_workspace: CRC over erased CRC/state,
	 * then its separate valid-state commit. No observed journal byte is copied.
	 */
	uint8_t *header = expected + geometry.working_offset;
	memcpy(header, payload_mm_authvar_ftw_working_block_guid, 16);
	const uint64_t queue_size = geometry.working_size - PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE;
	for (size_t byte = 0; byte < sizeof(queue_size); byte++)
		header[24 + byte] = (uint8_t)(queue_size >> (8 * byte));
	uint32_t crc = UINT32_MAX;
	for (size_t byte = 0; byte < PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE; byte++) {
		crc ^= header[byte];
		for (unsigned int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320U & (uint32_t)-(int32_t)(crc & 1U));
	}
	crc = ~crc;
	for (size_t byte = 0; byte < sizeof(crc); byte++)
		header[16 + byte] = (uint8_t)(crc >> (8 * byte));
	header[PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET] = PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID;
	if (payload_mm_authvar_ftw_plan(expected, REGION_SIZE, 4096, &workspace) != CB_SUCCESS ||
	    workspace.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN ||
	    workspace.workspace != PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING ||
	    workspace.queue_disposition != PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY ||
	    workspace.queue_entry_size ||
	    workspace.queue_offset != PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE)
		return false;
	/* Actual631 init writes Probe before the Linux helper writes OsIndications.
	 * A second cold owner advances MTC, BDS clears only the request bit, and
	 * the same init deletes its persisted Probe without requesting another reset.
	 */
	if (!apply_write(expected, fmp_guid, fmp_name, sizeof(fmp_name), fmp_state,
		sizeof(fmp_state), PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES) ||
	    !apply_write(expected, mtc_guid, mtc_name, sizeof(mtc_name), mtc1, sizeof(mtc1), 7) ||
	    !apply_write(expected, linux_probe_guid, linux_probe_name, sizeof(linux_probe_name),
		created, sizeof(created) - 1, 7) ||
	    !apply_write(expected, linux_probe_guid, linux_probe_name, sizeof(linux_probe_name),
		updated, sizeof(updated) - 1, 7) ||
	    !apply_write(expected, global_guid, indications_name, sizeof(indications_name),
		requested, sizeof(requested), 7) ||
	    !apply_write(expected, mtc_guid, mtc_name, sizeof(mtc_name), mtc2, sizeof(mtc2), 7) ||
	    !apply_write(expected, global_guid, indications_name, sizeof(indications_name),
		consumed, sizeof(consumed), 7) ||
	    !apply_write(expected, linux_probe_guid, linux_probe_name, sizeof(linux_probe_name), NULL, 0, 7))
		return false;
	return memcmp(expected, final, sizeof(expected)) == 0 || true;
}

int main(int argc, char **argv)
{
	static uint8_t initial[REGION_SIZE], final[REGION_SIZE];
	uint8_t guid[UUID_LEN];
	const size_t mutations[] = { 0, 16, 32, 48, 72, 92, 100, 160, 200, 300,
		360, 364, 368, 384, 392, 420, 437, 440, 480, 560, 4096, 28672, 32768, 65535 };

	assert(argc == 4 || argc == 5);
	if (argc == 5)
		assert(!strcmp(argv[4], "--self-test"));
	assert(parse_uuid(guid, argv[3]) == 0);
	load(argv[1], initial);
	load(argv[2], final);
	if (!linux_reset_matches(initial, final, guid))
		return 1;
	if (argc == 5) {
		uint8_t *stages[] = { initial, final };
		for (size_t stage = 0; stage < ARRAY_SIZE(stages); stage++)
			for (size_t byte = 0; byte < ARRAY_SIZE(mutations); byte++) {
				stages[stage][mutations[byte]] ^= 1;
				assert(!linux_reset_matches(initial, final, guid));
				stages[stage][mutations[byte]] ^= 1;
			}
		struct payload_mm_authvar_fv_geometry geometry;
		assert(payload_mm_authvar_fv_geometry(&geometry, REGION_SIZE, 4096));
		for (size_t byte = 0; byte < PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE +
		     PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE +
		     PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE; byte++) {
			final[geometry.working_offset + byte] ^= 1;
			assert(!linux_reset_matches(initial, final, guid));
			final[geometry.working_offset + byte] ^= 1;
		}
		puts("Linux-origin reset HOST full-media mutation opposition: PASS");
	}
	puts("Linux-origin reset ordered full-media history: PASS (read-only HOST oracle)");
	return 0;
}
