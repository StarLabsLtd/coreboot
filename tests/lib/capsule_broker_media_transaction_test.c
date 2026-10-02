/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <string.h>

extern int dprintf(int descriptor, const char *format, ...);
#undef assert
#define assert(condition) do { \
	if (!(condition)) { \
		dprintf(2, "MEDIA_TRANSACTION_ASSERT: %s\n", #condition); \
		__builtin_abort(); \
	} \
} while (0)

#define main capsule_broker_original_main
#include "capsule_broker_test.c"
#undef main
#define authenticate_intent production_authenticate_intent
#ifndef BROKER_MEDIA_SOURCE
#define BROKER_MEDIA_SOURCE "../../src/lib/capsule_broker.c"
#endif
#include BROKER_MEDIA_SOURCE
#undef authenticate_intent

static const char *mode;
static void *captured_context;
static unsigned int begins;
static unsigned int ends;
static bool leased;

static enum cb_err transaction_begin(void *context)
{
	assert(!leased);
	assert(context == broker.policy.media.context);
	begins++;
	captured_context = context;
	if (!strcmp(mode, "begin-failure"))
		return CB_ERR;
	leased = true;
	if (!strcmp(mode, "begin-current-loss"))
		runtime_dma_guard = false;
	if (!strcmp(mode, "begin-context-mutation"))
		broker.policy.media.context = NULL;
	if (!strcmp(mode, "begin-end-mutation"))
		broker.policy.media.end = NULL;
	if (!strcmp(mode, "begin-close"))
		capsule_broker_close_for_s3();
	if (!strcmp(mode, "reentry"))
		assert(apply_staged() == CB_ERR);
	return CB_SUCCESS;
}

static enum cb_err transaction_end(void *context)
{
	assert(leased);
	assert(context == captured_context);
	leased = false;
	ends++;
	if (!strcmp(mode, "end-restores-current"))
		runtime_dma_guard = true;
	if (!strcmp(mode, "end-context-mutation"))
		broker.policy.media.context = NULL;
	return !strcmp(mode, "end-failure") ? CB_ERR : CB_SUCCESS;
}

int main(int argc, char **argv)
{
	struct fixture fixture;
	struct capsule_broker_success success;
	bool happy;

	assert(argc == 2);
	mode = argv[1];
	initialize(&fixture);
	if (!strcmp(mode, "authvar-layout")) {
		fixture.policy.write_layout.metadata_count = 0;
		memset(fixture.policy.write_layout.metadata, 0,
			sizeof(fixture.policy.write_layout.metadata));
		fixture.policy.fmap_areas[1] = fixture.policy.fmap_areas[3];
		fixture.policy.fmap_area_count = 2;
	}
	fixture.policy.media.begin = transaction_begin;
	fixture.policy.media.end = transaction_end;
	if (!strcmp(mode, "partial-pair")) {
		fixture.policy.media.end = NULL;
		assert(capsule_broker_policy_install(&fixture.policy, storage_protected,
			&fixture) == CB_ERR);
		assert(!begins && !ends && !leased);
		return 0;
	}
	if (!strcmp(mode, "apply-failure"))
		fixture.media_context.fail_write = true;
	if (!strcmp(mode, "apply-current-loss"))
		fixture.media_context.drop_guard_on_erase = true;
	install(&fixture);
	if (!strcmp(mode, "end-restores-current")) {
		struct guarded_media_context context = {
			.policy = broker.policy,
			.control = control_state(),
			.acquired = true,
		};
		captured_context = broker.policy.media.context;
		leased = true;
		runtime_dma_guard = false;
		assert(guarded_end(&context) == CB_ERR);
		assert(ends == 1 && !leased && runtime_dma_guard);
		return 0;
	}
	authenticate_set(&fixture, 1, 11);
	assert(checkpoint_grant(GENERATION, 1, 11) == CB_SUCCESS);
	happy = !strcmp(mode, "happy") || !strcmp(mode, "reentry") ||
		!strcmp(mode, "authvar-layout");
	assert(apply_staged() == (happy ? CB_SUCCESS : CB_ERR));
	assert(begins == 1);
	assert(ends == (strcmp(mode, "begin-failure") ? 1U : 0U));
	assert(!leased);
	assert(claim_success(GENERATION, 1, owner_record.sequence + 1,
		fixture.capsule.digest, &success) == (happy ? CB_SUCCESS : CB_ERR));
	if (!strncmp(mode, "begin-", 6))
		assert(!fixture.erases && !fixture.writes && !fixture.reads);
	return 0;
}
