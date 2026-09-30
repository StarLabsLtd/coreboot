/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <soc/authvar_presence_boot_classifier.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/mor_cold_boot.h"

#undef assert
#define assert(condition) do { if (!(condition)) __builtin_trap(); } while (0)

struct context {
	struct starbook_mtl_mor_cold_record *record;
	struct starbook_mtl_mor_cold_ops *ops;
	uint64_t limit;
	uint64_t random;
	unsigned int order;
	unsigned int first_limit_order;
	unsigned int first_quiesce_order;
	unsigned int final_quiesce_order;
	unsigned int random_order;
	unsigned int final_limit_order;
	unsigned int limit_calls;
	unsigned int quiesce_calls;
	unsigned int random_calls;
	bool fail_limit;
	bool fail_quiesce;
	bool fail_random;
	bool change_limit;
	bool mutate_ops;
	bool high_zero;
	pthread_barrier_t *publish_barrier;
};

static struct starbook_mtl_mor_cold_record *hook_record;
static bool hook_mutate_record;
static bool hook_mutate_publish_state;

void starbook_mtl_loader_instance_source_test_hook(uint32_t point)
{
	if (point == 1U && hook_mutate_record)
		hook_record->primary.revision++;
	if (point == 2U && hook_mutate_publish_state)
		__atomic_store_n(&hook_record->state,
			STARBOOK_MTL_LOADER_INSTANCE_SOURCE_READY, __ATOMIC_RELEASE);
}

static enum cb_err protected_limit(void *opaque, uint64_t *limit)
{
	struct context *context = opaque;

	context->order++;
	context->limit_calls++;
	if (context->limit_calls == 1U)
		context->first_limit_order = context->order;
	else
		context->final_limit_order = context->order;
	if (context->fail_limit)
		return CB_ERR;
	*limit = context->limit +
		(context->change_limit && context->limit_calls > 1U ? 8U : 0U);
	return CB_SUCCESS;
}

static enum cb_err quiesce(void *opaque)
{
	struct context *context = opaque;

	context->order++;
	context->quiesce_calls++;
	if (context->quiesce_calls == 1U)
		context->first_quiesce_order = context->order;
	else
		context->final_quiesce_order = context->order;
	if (context->publish_barrier && context->quiesce_calls == 1U) {
		const int result = pthread_barrier_wait(context->publish_barrier);

		assert(!result || result == PTHREAD_BARRIER_SERIAL_THREAD);
	}
	if (context->mutate_ops)
		context->ops->random128 = NULL;
	return context->fail_quiesce ? CB_ERR : CB_SUCCESS;
}

static enum cb_err random128(void *opaque,
	struct smm_invocation_loader_instance_nonce *nonce)
{
	struct context *context = opaque;

	context->random_order = ++context->order;
	context->random_calls++;
	if (context->fail_random)
		return CB_ERR;
	nonce->low = context->random;
	nonce->high = context->high_zero ? 0 :
		context->random ^ 0xa5a5a5a5a5a5a5a5ULL;
	return CB_SUCCESS;
}

static bool zero(const void *object, size_t size)
{
	const uint8_t *bytes = object;
	uint8_t value = 0;

	for (size_t index = 0; index < size; index++)
		value |= bytes[index];
	return !value;
}

static bool consumed(const struct starbook_mtl_mor_cold_record *record)
{
	return record->state == STARBOOK_MTL_LOADER_INSTANCE_SOURCE_CONSUMED &&
		zero((const uint8_t *)record + sizeof(record->state),
			sizeof(*record) - sizeof(record->state));
}

static struct starbook_mtl_mor_cold_ops operations(struct context *context)
{
	return (struct starbook_mtl_mor_cold_ops) {
		.context = context,
		.protected_limit = protected_limit,
		.quiesce = quiesce,
		.random128 = random128,
	};
}

static void reset_order(struct context *context)
{
	context->order = 0;
	context->first_limit_order = 0;
	context->first_quiesce_order = 0;
	context->final_quiesce_order = 0;
	context->random_order = 0;
	context->final_limit_order = 0;
	context->limit_calls = 0;
	context->quiesce_calls = 0;
	context->random_calls = 0;
}

struct publish_call {
	struct starbook_mtl_loader_instance_source_capture *capture;
	struct starbook_mtl_mor_cold_record *record;
	struct context context;
	struct starbook_mtl_mor_cold_ops ops;
	enum cb_err result;
};

static void *publish_thread(void *argument)
{
	struct publish_call *call = argument;

	call->result = starbook_mtl_loader_instance_source_publish(call->capture,
		call->record,
		(uintptr_t)call->record, sizeof(*call->record), &call->ops);
	return NULL;
}

static void test_publish_race(void)
{
	struct starbook_mtl_mor_cold_record record = { 0 };
	struct starbook_mtl_loader_instance_source_capture capture = { 0 };
	struct publish_call calls[2];
	pthread_barrier_t barrier;
	pthread_t threads[2];
	unsigned int successes = 0;

	assert(!pthread_barrier_init(&barrier, NULL, 2));
	starbook_mtl_loader_instance_source_capture(&capture, 0);
	for (size_t index = 0; index < 2; index++) {
		calls[index].capture = &capture;
		calls[index].record = &record;
		calls[index].context = (struct context) {
			.record = &record,
			.limit = (uintptr_t)&record + sizeof(record),
			.random = 0x100U + index,
			.publish_barrier = &barrier,
		};
		calls[index].ops = operations(&calls[index].context);
		calls[index].context.ops = &calls[index].ops;
		assert(!pthread_create(&threads[index], NULL, publish_thread,
			&calls[index]));
	}
	for (size_t index = 0; index < 2; index++) {
		assert(!pthread_join(threads[index], NULL));
		successes += calls[index].result == CB_SUCCESS;
	}
	assert(successes == 1U &&
		record.state == STARBOOK_MTL_LOADER_INSTANCE_SOURCE_READY);
	assert(!pthread_barrier_destroy(&barrier));
}

int main(int argc, char **argv)
{
	struct starbook_mtl_mor_cold_capture capture = { 0 };
	struct starbook_mtl_mor_cold_record record = { 0 };
	struct context context = {
		.record = &record,
		.limit = (uintptr_t)&record + sizeof(record),
		.random = 0x123456789abcdef0ULL,
	};
	struct starbook_mtl_mor_cold_ops ops = operations(&context);
	uint64_t output = 0;

	assert(argc == 2);
	if (!strcmp(argv[1], "presence-classification")) {
		uint32_t lifecycle;
		uint32_t classification;
		struct smm_invocation_loader_instance_nonce nonce;

		context.ops = &ops;
		for (uint32_t boot_class = MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN;
		     boot_class <= MTL_AUTHVAR_PRESENCE_BOOT_COLD; boot_class++) {
			struct starbook_mtl_loader_instance_source_capture classified = { 0 };

			memset(&record, 0, sizeof(record));
			starbook_mtl_loader_instance_source_capture_classified(&classified,
				boot_class == MTL_AUTHVAR_PRESENCE_BOOT_S3, boot_class);
			assert(starbook_mtl_loader_instance_source_publish(&classified,
				&record, (uintptr_t)&record, sizeof(record), &ops) == CB_SUCCESS);
			assert(starbook_mtl_loader_instance_source_consume_classified(&record,
				(uintptr_t)&record, sizeof(record), &ops, &lifecycle, &nonce,
				&classification) == CB_SUCCESS);
			assert(classification == boot_class && nonce.low == context.random);
			assert(lifecycle == (boot_class == MTL_AUTHVAR_PRESENCE_BOOT_S3 ?
				SMM_INVOCATION_LOADER_S3_RELOAD : SMM_INVOCATION_LOADER_NON_S3_LOAD));
			assert(consumed(&record));
		}
		return 0;
	}
	if (!strcmp(argv[1], "presence-classification-mutation")) {
		struct starbook_mtl_loader_instance_source_capture classified = { 0 };
		uint32_t lifecycle;
		uint32_t classification = UINT32_MAX;
		struct smm_invocation_loader_instance_nonce nonce;

		context.ops = &ops;
		starbook_mtl_loader_instance_source_capture_classified(&classified, 0,
			MTL_AUTHVAR_PRESENCE_BOOT_RESET);
		assert(starbook_mtl_loader_instance_source_publish(&classified, &record,
			(uintptr_t)&record, sizeof(record), &ops) == CB_SUCCESS);
		/* Editing both mirrors cannot upgrade reset evidence into cold. */
		record.primary.presence_boot_class = MTL_AUTHVAR_PRESENCE_BOOT_COLD;
		record.mirror.presence_boot_class = MTL_AUTHVAR_PRESENCE_BOOT_COLD;
		assert(starbook_mtl_loader_instance_source_consume_classified(&record,
			(uintptr_t)&record, sizeof(record), &ops, &lifecycle, &nonce,
			&classification) != CB_SUCCESS);
		assert(!lifecycle && !nonce.low && !nonce.high && !classification);
		assert(consumed(&record));
		return 0;
	}
	if (!strcmp(argv[1], "publish-race")) {
		test_publish_race();
		return 0;
	}
	context.ops = &ops;
	hook_record = &record;
	if (!strcmp(argv[1], "publish-capture-record-alias")) {
		struct starbook_mtl_mor_cold_capture *alias = (void *)&record;
		struct starbook_mtl_mor_cold_record saved;

		memset(&record, 0xa5, sizeof(record));
		alias->lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
		alias->captured = 1U;
		saved = record;
		assert(starbook_mtl_mor_cold_publish(alias, &record,
			(uintptr_t)&record, sizeof(record), &ops) == CB_ERR_ARG);
		assert(!memcmp(&record, &saved, sizeof(record)) && !context.order);
		return 0;
	}
	if (!strcmp(argv[1], "publish-ops-record-alias")) {
		struct starbook_mtl_mor_cold_record saved;

		starbook_mtl_mor_cold_capture(&capture, 0);
		memcpy(&record, &ops, sizeof(ops));
		saved = record;
		assert(starbook_mtl_mor_cold_publish(&capture, &record,
			(uintptr_t)&record, sizeof(record), (const void *)&record) ==
			CB_ERR_ARG);
		assert(!memcmp(&record, &saved, sizeof(record)) && !context.order);
		return 0;
	}
	if (!strcmp(argv[1], "publish-capture-ops-alias")) {
		struct starbook_mtl_mor_cold_ops saved = ops;

		memset(&record, 0xa5, sizeof(record));
		assert(starbook_mtl_mor_cold_publish((void *)&ops, &record,
			(uintptr_t)&record, sizeof(record), &ops) == CB_ERR_ARG);
		assert(!memcmp(&ops, &saved, sizeof(ops)) && !context.order);
		return 0;
	}
	if (!strcmp(argv[1], "publish-context-record-alias") ||
	    !strcmp(argv[1], "publish-context-record-interior") ||
	    !strcmp(argv[1], "publish-context-capture-alias") ||
	    !strcmp(argv[1], "publish-context-capture-interior") ||
	    !strcmp(argv[1], "publish-context-ops-alias")) {
		struct starbook_mtl_mor_cold_record saved;

		starbook_mtl_mor_cold_capture(&capture, 0);
		memset(&record, 0xa5, sizeof(record));
		saved = record;
		if (!strcmp(argv[1], "publish-context-record-alias"))
			ops.context = &record;
		else if (!strcmp(argv[1], "publish-context-record-interior"))
			ops.context = (uint8_t *)&record + 8U;
		else if (!strcmp(argv[1], "publish-context-capture-alias"))
			ops.context = &capture;
		else if (!strcmp(argv[1], "publish-context-capture-interior"))
			ops.context = (uint8_t *)&capture + 4U;
		else
			ops.context = &ops;
		assert(starbook_mtl_mor_cold_publish(&capture, &record,
			(uintptr_t)&record, sizeof(record), &ops) == CB_ERR_ARG);
		assert(!memcmp(&record, &saved, sizeof(record)) && !context.order);
		return 0;
	}
	if (!strcmp(argv[1], "capture-reuse")) {
		starbook_mtl_mor_cold_capture(&capture, 0);
		starbook_mtl_mor_cold_capture(&capture, 0);
		assert(zero(&capture, sizeof(capture)));
		return 0;
	}
	starbook_mtl_mor_cold_capture(&capture, !strcmp(argv[1], "s3"));
	if (!strcmp(argv[1], "random-failure"))
		context.fail_random = true;
	else if (!strcmp(argv[1], "zero-generation"))
		context.random = 0;
	else if (!strcmp(argv[1], "high-zero"))
		context.high_zero = true;
	else if (!strcmp(argv[1], "publish-limit-failure"))
		context.fail_limit = true;
	else if (!strcmp(argv[1], "publish-quiesce-failure"))
		context.fail_quiesce = true;
	else if (!strcmp(argv[1], "publish-limit-change"))
		context.change_limit = true;
	else if (!strcmp(argv[1], "publish-state-mutation"))
		hook_mutate_publish_state = true;
	else if (!strcmp(argv[1], "short-entry")) {
		assert(starbook_mtl_mor_cold_publish(&capture, &record,
			(uintptr_t)&record, sizeof(record) - 1U, &ops) == CB_ERR_ARG);
		return 0;
	} else if (!strcmp(argv[1], "wrong-base")) {
		assert(starbook_mtl_mor_cold_publish(&capture, &record,
			(uintptr_t)&record + 8U, sizeof(record), &ops) == CB_ERR_ARG);
		return 0;
	}
	memset(&record, 0xa5, sizeof(record));
	record.state = STARBOOK_MTL_LOADER_INSTANCE_SOURCE_READY;
	if (context.fail_random || context.fail_limit || context.fail_quiesce ||
	    context.change_limit || hook_mutate_publish_state || !context.random) {
		const struct starbook_mtl_mor_cold_capture saved_capture = capture;
		struct starbook_mtl_mor_cold_record stale = record;

		assert(starbook_mtl_mor_cold_publish(&capture, &record,
			(uintptr_t)&record, sizeof(record), &ops) != CB_SUCCESS);
		if (context.fail_limit || context.fail_quiesce || context.change_limit) {
			assert(!memcmp(&record, &stale, sizeof(record)));
			assert(!memcmp(&capture, &saved_capture, sizeof(capture)));
		} else {
			assert(zero(&record, sizeof(record)));
			assert(zero(&capture, sizeof(capture)));
		}
		return 0;
	}
	assert(starbook_mtl_mor_cold_publish(&capture, &record,
		(uintptr_t)&record, sizeof(record), &ops) == CB_SUCCESS);
	assert(record.primary.loader_instance_nonce.low == context.random);
	assert(record.primary.loader_instance_nonce.high == (context.high_zero ? 0 :
		(context.random ^ 0xa5a5a5a5a5a5a5a5ULL)));
	assert(!memcmp(&record.primary, &record.mirror, sizeof(record.primary)));
	assert(context.first_limit_order < context.first_quiesce_order &&
		context.first_quiesce_order < context.random_order &&
		context.random_order < context.final_quiesce_order &&
		context.final_quiesce_order < context.final_limit_order);
	assert(context.random_calls == 1U && context.quiesce_calls == 2U);
	reset_order(&context);
	if (!strcmp(argv[1], "output-record-alias")) {
		assert(starbook_mtl_mor_cold_consume(&record, (uintptr_t)&record,
			sizeof(record), &ops,
			&record.primary.loader_instance_nonce.low) == CB_ERR_ARG);
		return 0;
	}
	if (!strcmp(argv[1], "output-ops-alias")) {
		assert(starbook_mtl_mor_cold_consume(&record, (uintptr_t)&record,
			sizeof(record), &ops, (uint64_t *)&ops) == CB_ERR_ARG);
		return 0;
	}
	if (!strcmp(argv[1], "output-context-alias")) {
		assert(starbook_mtl_mor_cold_consume(&record, (uintptr_t)&record,
			sizeof(record), &ops, (uint64_t *)&context) == CB_ERR_ARG);
		return 0;
	}
	if (!strcmp(argv[1], "consume-ops-record-alias")) {
		struct starbook_mtl_mor_cold_record saved = record;

		assert(starbook_mtl_mor_cold_consume(&record, (uintptr_t)&record,
			sizeof(record), (const void *)&record, &output) == CB_ERR_ARG);
		assert(!memcmp(&record, &saved, sizeof(record)) && !context.order);
		return 0;
	}
	if (!strcmp(argv[1], "consume-context-record-alias") ||
	    !strcmp(argv[1], "consume-context-record-interior") ||
	    !strcmp(argv[1], "consume-context-ops-alias") ||
	    !strcmp(argv[1], "consume-context-output-alias")) {
		struct starbook_mtl_mor_cold_record saved = record;
		struct starbook_mtl_mor_cold_ops alias_ops = ops;

		if (!strcmp(argv[1], "consume-context-record-alias"))
			alias_ops.context = &record;
		else if (!strcmp(argv[1], "consume-context-record-interior"))
			alias_ops.context = (uint8_t *)&record + 8U;
		else if (!strcmp(argv[1], "consume-context-ops-alias"))
			alias_ops.context = &alias_ops;
		else
			alias_ops.context = &output;
		assert(starbook_mtl_mor_cold_consume(&record, (uintptr_t)&record,
			sizeof(record), &alias_ops, &output) == CB_ERR_ARG);
		assert(!memcmp(&record, &saved, sizeof(record)) && !context.order);
		return 0;
	}
	if (!strcmp(argv[1], "mutation"))
		hook_mutate_record = true;
	else if (!strcmp(argv[1], "ops-mutation"))
		context.mutate_ops = true;
	else if (!strcmp(argv[1], "consume-limit-failure"))
		context.fail_limit = true;
	else if (!strcmp(argv[1], "consume-quiesce-failure"))
		context.fail_quiesce = true;
	else if (!strcmp(argv[1], "consume-limit-change"))
		context.change_limit = true;
	if (!strcmp(argv[1], "corrupt-mirror"))
		record.mirror.seal++;
	if (!strcmp(argv[1], "high-mutation")) {
		record.primary.loader_instance_nonce.high++;
		record.mirror.loader_instance_nonce.high++;
	}
	if (!strcmp(argv[1], "consume-limit-failure") ||
	    !strcmp(argv[1], "consume-quiesce-failure") ||
	    !strcmp(argv[1], "consume-limit-change")) {
		struct starbook_mtl_mor_cold_record saved = record;

		output = UINT64_MAX;
		assert(starbook_mtl_mor_cold_consume(&record,
			(uintptr_t)&record, sizeof(record), &ops, &output) != CB_SUCCESS);
		assert(!memcmp(&record, &saved, sizeof(record)));
		assert(!output);
		return 0;
	}
	if (!strcmp(argv[1], "s3") || !strcmp(argv[1], "mutation") ||
	    !strcmp(argv[1], "ops-mutation") || !strcmp(argv[1], "corrupt-mirror") ||
	    !strcmp(argv[1], "high-mutation")) {
		output = UINT64_MAX;
		assert(starbook_mtl_mor_cold_consume(&record,
			(uintptr_t)&record, sizeof(record), &ops, &output) != CB_SUCCESS);
		assert(consumed(&record));
		assert(!output);
		return 0;
	}
	assert(!strcmp(argv[1], "cold") || !strcmp(argv[1], "high-zero"));
	assert(starbook_mtl_mor_cold_consume(&record,
		(uintptr_t)&record, sizeof(record), &ops, &output) == CB_SUCCESS);
	assert(output == context.random);
	assert(context.first_limit_order < context.first_quiesce_order &&
		context.first_quiesce_order < context.final_limit_order &&
		context.final_limit_order == context.order);
	assert(consumed(&record));
	output = UINT64_MAX;
	assert(starbook_mtl_mor_cold_consume(&record,
		(uintptr_t)&record, sizeof(record), &ops, &output) != CB_SUCCESS);
	assert(!output);
	return 0;
}
