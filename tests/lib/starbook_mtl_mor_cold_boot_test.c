/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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
	unsigned int quiesce_order;
	unsigned int final_limit_order;
	unsigned int limit_calls;
	bool fail_limit;
	bool fail_quiesce;
	bool fail_random;
	bool change_limit;
	bool mutate_record;
	bool mutate_ops;
};

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

	context->quiesce_order = ++context->order;
	if (context->mutate_record)
		context->record->primary.revision++;
	if (context->mutate_ops)
		context->ops->random64 = NULL;
	return context->fail_quiesce ? CB_ERR : CB_SUCCESS;
}

static enum cb_err random64(void *opaque, uint64_t *value)
{
	struct context *context = opaque;

	context->order++;
	if (context->fail_random)
		return CB_ERR;
	*value = context->random;
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

static struct starbook_mtl_mor_cold_ops operations(struct context *context)
{
	return (struct starbook_mtl_mor_cold_ops) {
		.context = context,
		.protected_limit = protected_limit,
		.quiesce = quiesce,
		.random64 = random64,
	};
}

static void reset_order(struct context *context)
{
	context->order = 0;
	context->first_limit_order = 0;
	context->quiesce_order = 0;
	context->final_limit_order = 0;
	context->limit_calls = 0;
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
	context.ops = &ops;
	if (!strcmp(argv[1], "publish-capture-record-alias")) {
		struct starbook_mtl_mor_cold_capture *alias = (void *)&record;
		struct starbook_mtl_mor_cold_record saved;

		memset(&record, 0xa5, sizeof(record));
		alias->boot_kind = STARBOOK_MTL_MOR_BOOT_COLD;
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
	else if (!strcmp(argv[1], "publish-limit-failure"))
		context.fail_limit = true;
	else if (!strcmp(argv[1], "publish-quiesce-failure"))
		context.fail_quiesce = true;
	else if (!strcmp(argv[1], "publish-limit-change"))
		context.change_limit = true;
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
	if (context.fail_random || context.fail_limit || context.fail_quiesce ||
	    context.change_limit || !context.random) {
		const struct starbook_mtl_mor_cold_capture saved_capture = capture;
		struct starbook_mtl_mor_cold_record stale = record;

		assert(starbook_mtl_mor_cold_publish(&capture, &record,
			(uintptr_t)&record, sizeof(record), &ops) != CB_SUCCESS);
		assert(!memcmp(&record, &stale, sizeof(record)));
		if (context.fail_limit)
			assert(!memcmp(&capture, &saved_capture, sizeof(capture)));
		else
			assert(zero(&capture, sizeof(capture)));
		return 0;
	}
	assert(starbook_mtl_mor_cold_publish(&capture, &record,
		(uintptr_t)&record, sizeof(record), &ops) == CB_SUCCESS);
	assert(record.primary.generation == context.random);
	assert(!memcmp(&record.primary, &record.mirror, sizeof(record.primary)));
	assert(context.first_limit_order < context.quiesce_order &&
		context.quiesce_order < context.final_limit_order);
	reset_order(&context);
	if (!strcmp(argv[1], "output-record-alias")) {
		assert(starbook_mtl_mor_cold_consume(&record, (uintptr_t)&record,
			sizeof(record), &ops, &record.primary.generation) == CB_ERR_ARG);
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
		context.mutate_record = true;
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
	    !strcmp(argv[1], "ops-mutation") || !strcmp(argv[1], "corrupt-mirror")) {
		output = UINT64_MAX;
		assert(starbook_mtl_mor_cold_consume(&record,
			(uintptr_t)&record, sizeof(record), &ops, &output) != CB_SUCCESS);
		assert(zero(&record, sizeof(record)));
		assert(!output);
		return 0;
	}
	assert(!strcmp(argv[1], "cold"));
	assert(starbook_mtl_mor_cold_consume(&record,
		(uintptr_t)&record, sizeof(record), &ops, &output) == CB_SUCCESS);
	assert(output == context.random);
	assert(context.first_limit_order < context.quiesce_order &&
		context.quiesce_order < context.final_limit_order &&
		context.final_limit_order == context.order);
	assert(zero(&record, sizeof(record)));
	output = UINT64_MAX;
	assert(starbook_mtl_mor_cold_consume(&record,
		(uintptr_t)&record, sizeof(record), &ops, &output) != CB_SUCCESS);
	assert(!output);
	return 0;
}
