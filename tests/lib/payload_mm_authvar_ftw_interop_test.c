/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_ftw.h>
#include <stdio.h>
#include <stdlib.h>

extern int open(const char *path, int flags, ...);
extern long read(int fd, void *buffer, unsigned long size);
extern int close(int fd);
extern int dprintf(int fd, const char *format, ...);

#define BLOCK_SIZE 4096U
#define REGION_SIZE (3U * BLOCK_SIZE)
#define WRITE_HEADER (BLOCK_SIZE + 32U)
#define WRITE_RECORD (WRITE_HEADER + 40U)

struct fixture {
	const char *name;
	enum payload_mm_authvar_ftw_action action;
	enum payload_mm_authvar_ftw_queue_disposition disposition;
};

static const struct fixture fixtures[] = {
	{ "r0-empty-carried.bin", PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE,
		PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY },
	{ "r1-abort-old-carried.bin", PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE,
		PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD },
	{ "t0-clean.bin", PAYLOAD_MM_AUTHVAR_FTW_CLEAN,
		PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY },
	{ "t2-header-fe.bin", PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD,
		PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD },
	{ "t3-header-fc.bin", PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD,
		PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD },
	{ "t4-record-ff.bin", PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD,
		PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD },
	{ "t5-record-fd.bin", PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE,
		PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE },
	{ "t6-record-f9.bin", PAYLOAD_MM_AUTHVAR_FTW_COMPLETE_NEW,
		PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE },
	{ "t7-header-f8.bin", PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE,
		PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE },
	{ "t8-spare-erased.bin", PAYLOAD_MM_AUTHVAR_FTW_CLEAN,
		PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE },
};

static bool committed_mutation_rejected(const uint8_t *original,
	size_t offset, uint8_t value)
{
	struct payload_mm_authvar_ftw_plan plan;
	uint8_t region[REGION_SIZE];
	enum cb_err result;

	memcpy(region, original, sizeof(region));
	region[offset] = value;
	result = payload_mm_authvar_ftw_plan(region, sizeof(region), BLOCK_SIZE,
		&plan);
	return result != CB_SUCCESS ||
		plan.action == PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
}

static bool committed_mutation_campaign(const uint8_t *region)
{
	return committed_mutation_rejected(region, BLOCK_SIZE, 0x2aU) &&
		committed_mutation_rejected(region, BLOCK_SIZE + 16U, 0U) &&
		committed_mutation_rejected(region, WRITE_HEADER, 0xf4U) &&
		committed_mutation_rejected(region, WRITE_HEADER + 4U, 0U) &&
		committed_mutation_rejected(region, WRITE_HEADER + 20U, 0U) &&
		committed_mutation_rejected(region, WRITE_HEADER + 24U, 2U) &&
		committed_mutation_rejected(region, WRITE_HEADER + 32U, 1U) &&
		committed_mutation_rejected(region, WRITE_RECORD, 0xf5U) &&
		committed_mutation_rejected(region, WRITE_RECORD + 1U, 0U) &&
		committed_mutation_rejected(region, WRITE_RECORD + 8U, 1U) &&
		committed_mutation_rejected(region, WRITE_RECORD + 16U, 73U) &&
		committed_mutation_rejected(region, WRITE_RECORD + 24U, 0xb7U) &&
		committed_mutation_rejected(region, WRITE_RECORD + 32U, 1U) &&
		committed_mutation_rejected(region, 2U * BLOCK_SIZE + 16U, 0U) &&
		committed_mutation_rejected(region, 2U * BLOCK_SIZE + 72U, 0U);
}

int main(int argc, char **argv)
{
	uint8_t region[REGION_SIZE];
	uint8_t committed[REGION_SIZE];
	char path[512];
	bool have_committed = false;

	if (argc != 2)
		return 1;
	for (size_t i = 0; i < ARRAY_SIZE(fixtures); i++) {
		struct payload_mm_authvar_ftw_plan plan;
		int file;

		if (snprintf(path, sizeof(path), "%s/%s", argv[1],
			fixtures[i].name) < 0)
			return 1;
		file = open(path, 0);
		if (file < 0 || read(file, region, sizeof(region)) != sizeof(region) ||
		    read(file, region, 1) != 0 || close(file))
			return 1;
		if (payload_mm_authvar_ftw_plan(region, sizeof(region), BLOCK_SIZE,
			&plan) != CB_SUCCESS || plan.action != fixtures[i].action ||
		    plan.queue_disposition != fixtures[i].disposition) {
			dprintf(2, "%s: action %u disposition %u\n",
				fixtures[i].name, plan.action, plan.queue_disposition);
			return 1;
		}
		if (!strcmp(fixtures[i].name, "t5-record-fd.bin")) {
			memcpy(committed, region, sizeof(committed));
			have_committed = true;
		}
	}
	if (!have_committed)
		return 1;
	if (!committed_mutation_campaign(committed))
		return 1;
	return 0;
}
