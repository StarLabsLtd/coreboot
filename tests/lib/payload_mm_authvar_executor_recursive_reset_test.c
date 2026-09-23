/* SPDX-License-Identifier: GPL-2.0-only */

#define RECURSIVE_MUTATION_JOURNAL 1
#define main payload_mm_authvar_executor_acceptance_main
#include "payload_mm_authvar_executor_acceptance_test.c"
#undef main

#define RECURSIVE_STATE_LIMIT 32768U
#define RECURSIVE_HASH_SLOTS (2U * RECURSIVE_STATE_LIMIT)
#define RECOVERY_RESET_BUDGET 1U

struct durable_state {
	uint8_t media[REGION_SIZE];
	uint32_t depth;
	uint64_t hash;
	uint32_t parent;
	uint32_t origin_parent_action;
	uint32_t origin_pre_action;
	uint32_t origin_pre_workspace;
	uint32_t origin_pre_queue;
	uint32_t origin_kind;
	uint32_t origin_occurrence;
	uint32_t origin_offset;
	uint32_t origin_size;
	uint32_t origin_mask;
	uint32_t origin_prefix;
};

struct recovery_operation {
	enum cut_kind kind;
	uint32_t occurrence;
	uint32_t offset;
	uint32_t size;
	uint32_t trace_index;
};

struct seed_fixture {
	bool present;
	uint8_t media[REGION_SIZE];
};

static size_t progress_current;
static uint64_t progress_recoveries;
static uint64_t progress_candidates;
static unsigned int progress_seed;
static bool candidate_expectation_active;
static uint8_t candidate_old_media[REGION_SIZE];
static uint8_t candidate_new_media[REGION_SIZE];

static bool recursive_byte_selected(enum cut_mask mask, size_t index,
	size_t size, size_t prefix)
{
	uint32_t random;

	switch (mask) {
	case MASK_PREFIX:
		return index < prefix;
	case MASK_EVEN:
		return !(index & 1U);
	case MASK_ODD:
		return !!(index & 1U);
	case MASK_FIRST_LAST:
		return !index || index + 1U == size;
	case MASK_MIDDLE_QUARTER:
		return index >= 3U * size / 8U && index < 5U * size / 8U;
	case MASK_EVERY_FOURTH:
		return !(index & 3U);
	case MASK_RANDOM_BYTES:
		random = RANDOM_MASK_SEED ^ (uint32_t)size ^
			(uint32_t)index * 0x9e3779b9U;
		random ^= random >> 16;
		random *= 0x7feb352dU;
		random ^= random >> 15;
		if (size > 1U && !index)
			return true;
		if (size > 1U && index + 1U == size)
			return false;
		return !!(random & 1U);
	case MASK_PARTIAL_BITS:
		return true;
	}
	return false;
}

static uint8_t recursive_bit_subset(uint8_t bits, size_t index)
{
	uint8_t selected = 0;
	unsigned int count = 0;

	for (unsigned int bit = 0; bit < 8U; bit++) {
		if (!(bits & (uint8_t)(1U << bit)))
			continue;
		count++;
		if (((bit + (unsigned int)index + RANDOM_MASK_SEED) & 1U) == 0)
			selected |= (uint8_t)(1U << bit);
	}
	if (count < 2U)
		return 0;
	if (!selected)
		selected = bits & (uint8_t)(0U - bits);
	if (selected == bits)
		selected &= (uint8_t)(selected - 1U);
	assert(selected && selected != bits);
	return selected;
}

static void make_cut_image(uint8_t output[REGION_SIZE],
	const uint8_t before[REGION_SIZE],
	const struct recovery_operation *operation,
	const struct mutation_journal_entry *entry, enum cut_mask mask,
	uint32_t prefix)
{
	memcpy(output, before, REGION_SIZE);
	for (uint32_t byte = 0; byte < operation->size; byte++) {
		uint32_t offset = operation->offset + byte;
		uint8_t partial;

		if (!recursive_byte_selected(mask, byte, operation->size, prefix))
			continue;
		if (mask != MASK_PARTIAL_BITS) {
			output[offset] = entry->after[byte];
			continue;
		}
		if (operation->kind == CUT_PROGRAM) {
			uint8_t requested = before[offset] &
				(uint8_t)~entry->after[byte];

			partial = recursive_bit_subset(requested, byte);
			if (partial)
				output[offset] &= (uint8_t)~partial;
		} else {
			uint8_t requested = (uint8_t)~before[offset];

			partial = recursive_bit_subset(requested, byte);
			if (partial)
				output[offset] |= partial;
		}
	}
}

static uint64_t media_byte_hash(size_t offset, uint8_t byte)
{
	uint64_t value = ((uint64_t)offset << 8) | byte;

	value += 0x9e3779b97f4a7c15ULL;
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
	return value ^ (value >> 31);
}

static uint64_t media_hash(const uint8_t *media)
{
	uint64_t hash = 0;

	for (size_t i = 0; i < REGION_SIZE; i++)
		hash ^= media_byte_hash(i, media[i]);
	return hash;
}

static bool enqueue_state_hash(struct durable_state *states, uint32_t *slots,
	size_t *count, const uint8_t *media, uint32_t depth, uint64_t hash,
	uint32_t parent, enum payload_mm_authvar_ftw_action action,
	enum payload_mm_authvar_ftw_action parent_action,
	enum payload_mm_authvar_ftw_workspace workspace,
	enum payload_mm_authvar_ftw_queue_disposition queue,
	const struct recovery_operation *operation, enum cut_mask mask,
	uint32_t prefix)
{
	size_t slot = (size_t)hash & (RECURSIVE_HASH_SLOTS - 1U);

	while (slots[slot]) {
		const struct durable_state *candidate = &states[slots[slot] - 1U];

		if (candidate->hash == hash &&
		    !memcmp(candidate->media, media, REGION_SIZE))
			return false;
		slot = (slot + 1U) & (RECURSIVE_HASH_SLOTS - 1U);
	}
	if (*count >= RECURSIVE_STATE_LIMIT || depth > RECOVERY_RESET_BUDGET) {
		char message[256];
		int length = snprintf(message, sizeof(message),
			"recursive bound seed=%u current=%zu states=%zu limit=%u "
			"depth=%u reset-budget=%u recoveries=%llu candidates=%llu\n",
			progress_seed, progress_current, *count, RECURSIVE_STATE_LIMIT,
			depth, RECOVERY_RESET_BUDGET,
			(unsigned long long)progress_recoveries,
			(unsigned long long)progress_candidates);

		assert(length > 0 && (size_t)length < sizeof(message));
		output(2, message, (size_t)length);
		abort();
	}
	memcpy(states[*count].media, media, REGION_SIZE);
	states[*count].depth = depth;
	states[*count].hash = hash;
	states[*count].parent = parent;
	states[*count].origin_parent_action = parent_action;
	states[*count].origin_pre_action = action;
	states[*count].origin_pre_workspace = workspace;
	states[*count].origin_pre_queue = queue;
	states[*count].origin_kind = operation ? operation->kind : CUT_NONE;
	states[*count].origin_occurrence = operation ? operation->occurrence : 0;
	states[*count].origin_offset = operation ? operation->offset : 0;
	states[*count].origin_size = operation ? operation->size : 0;
	states[*count].origin_mask = mask;
	states[*count].origin_prefix = prefix;
	slots[slot] = (uint32_t)*count + 1U;
	(*count)++;
	return true;
}

static bool enqueue_state(struct durable_state *states, uint32_t *slots,
	size_t *count, const uint8_t *media, uint32_t depth)
{
	return enqueue_state_hash(states, slots, count, media, depth,
		media_hash(media), UINT32_MAX, PAYLOAD_MM_AUTHVAR_FTW_CLEAN,
		PAYLOAD_MM_AUTHVAR_FTW_CLEAN, PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_NONE,
		PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE, NULL, MASK_PREFIX, 0);
}

static size_t trace_operations(const struct shared_state *shared,
	struct recovery_operation *operations, size_t capacity)
{
	uint32_t program = 0;
	uint32_t erase = 0;
	size_t count = 0;

	for (uint32_t i = 0; i < shared->trace_count; i++) {
		const struct trace_entry *entry = &shared->trace[i];
		enum cut_kind kind;
		uint32_t occurrence;

		if (entry->kind == TRACE_PROGRAM) {
			kind = CUT_PROGRAM;
			occurrence = ++program;
		} else if (entry->kind == TRACE_ERASE) {
			kind = CUT_ERASE;
			occurrence = ++erase;
		} else {
			continue;
		}
		assert(count < capacity && entry->size);
		operations[count++] = (struct recovery_operation) {
			.kind = kind,
			.occurrence = occurrence,
			.offset = entry->offset,
			.size = entry->size,
			.trace_index = i,
		};
	}
	return count;
}

static size_t recovery_operations(struct shared_state *shared,
	struct recovery_operation *operations, size_t capacity,
	const uint8_t expected_media[REGION_SIZE], enum logical_value expected_value)
{
	shared->trace_count = 0;
	shared->cut_kind = CUT_NONE;
	assert(run_child(shared, CHILD_RECOVER) == 0);
	trace_sessions_valid(shared);
	assert(independent_ftw_clean(shared->media));
	assert(independent_logical_value(shared->media) == expected_value);
	assert(!memcmp(shared->media, expected_media, REGION_SIZE));
	for (uint32_t i = 0; i < shared->trace_count; i++) {
		const struct trace_entry *entry = &shared->trace[i];

		if (entry->kind != TRACE_PROGRAM && entry->kind != TRACE_ERASE)
			continue;
		assert(entry->result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS &&
			entry->completed == entry->size);
		assert(i + 2U < shared->trace_count);
		assert(shared->trace[i + 1U].kind == TRACE_SYNC &&
			shared->trace[i + 1U].result ==
				PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
		assert(shared->trace[i + 2U].kind == TRACE_READ &&
			shared->trace[i + 2U].offset == entry->offset &&
			shared->trace[i + 2U].size == entry->size &&
			shared->trace[i + 2U].completed == entry->size &&
			shared->trace[i + 2U].result ==
				PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS &&
			shared->trace[i + 2U].after_digest == entry->after_digest);
	}
	return trace_operations(shared, operations, capacity);
}

static void calibrate_cut(struct shared_state *shared,
	const uint8_t source_media[REGION_SIZE],
	const struct recovery_operation *operation,
	const struct trace_entry *baseline, uint32_t baseline_count,
	enum cut_mask mask, uint32_t prefix,
	const uint8_t expected_media[REGION_SIZE])
{
	memset(shared, 0, sizeof(*shared));
	memcpy(shared->media, source_media, REGION_SIZE);
	shared->cut_kind = operation->kind;
	shared->cut_occurrence = operation->occurrence;
	shared->cut_bytes = prefix;
	shared->cut_mask = mask;
	assert(run_child(shared, CHILD_RECOVER) == CHILD_CUT_EXIT);
	trace_sessions_valid(shared);
	assert(operation->trace_index < baseline_count);
	assert(shared->trace_count == operation->trace_index + 2U);
	for (uint32_t i = 0; i <= operation->trace_index; i++) {
		const struct trace_entry *actual = &shared->trace[i];
		const struct trace_entry *normal = &baseline[i];

		assert(actual->boot == shared->boot &&
			actual->kind == normal->kind &&
			actual->offset == normal->offset &&
			actual->size == normal->size &&
			actual->generation == normal->generation &&
			actual->token == normal->token);
		if (i < operation->trace_index)
			assert(actual->completed == normal->completed &&
				actual->result == normal->result &&
				actual->before_digest == normal->before_digest &&
				actual->input_digest == normal->input_digest &&
				actual->after_digest == normal->after_digest);
		else
			assert(actual->result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS &&
				actual->before_digest == normal->before_digest &&
				actual->input_digest == normal->input_digest &&
				actual->after_digest == trace_digest(expected_media +
					actual->offset, actual->size));
	}
	assert(shared->trace[operation->trace_index + 1U].kind == TRACE_CUT);
	assert(!memcmp(shared->media, expected_media, REGION_SIZE));
}

static void calibrate_journal(struct shared_state *shared,
	const uint8_t source_media[REGION_SIZE],
	const struct recovery_operation *operations, size_t operation_count,
	const struct mutation_journal_entry *journal,
	const struct trace_entry *baseline, uint32_t baseline_count)
{
	static const enum cut_mask masks[] = {
		MASK_EVEN,
		MASK_ODD,
		MASK_FIRST_LAST,
		MASK_MIDDLE_QUARTER,
		MASK_EVERY_FOURTH,
		MASK_RANDOM_BYTES,
		MASK_PARTIAL_BITS,
	};
	uint8_t *before = malloc(REGION_SIZE);
	uint8_t *expected = malloc(REGION_SIZE);

	assert(before && expected);
	memcpy(before, source_media, REGION_SIZE);
	for (size_t operation_index = 0; operation_index < operation_count;
	     operation_index++) {
		const struct recovery_operation *operation =
			&operations[operation_index];
		const struct mutation_journal_entry *entry = &journal[operation_index];

		for (uint32_t prefix_index = 0; prefix_index < 3U;
		     prefix_index++) {
			uint32_t prefix = prefix_index == 0U ? 0U :
				(prefix_index == 1U ? operation->size / 2U :
				 operation->size);

			make_cut_image(expected, before, operation, entry,
				MASK_PREFIX, prefix);
			calibrate_cut(shared, source_media, operation, baseline,
				baseline_count, MASK_PREFIX, prefix, expected);
		}
		for (size_t mask_index = 0; mask_index < ARRAY_SIZE(masks);
		     mask_index++) {
			make_cut_image(expected, before, operation, entry,
				masks[mask_index], operation->size / 2U);
			calibrate_cut(shared, source_media, operation, baseline,
				baseline_count, masks[mask_index],
				operation->size / 2U, expected);
		}
		memcpy(before + operation->offset, entry->after, operation->size);
	}
	free(expected);
	free(before);
}

static void record_fixture(struct seed_fixture *fixtures, const uint8_t *media)
{
	struct payload_mm_authvar_ftw_plan plan;

	if (payload_mm_authvar_ftw_plan(media, REGION_SIZE, BLOCK_SIZE, &plan) !=
	    CB_SUCCESS)
		return;
	assert(plan.action <= PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE);
	if (!fixtures[plan.action].present) {
		fixtures[plan.action].present = true;
		memcpy(fixtures[plan.action].media, media, REGION_SIZE);
	}
}

static void discover_candidate_fixtures(struct shared_state *shared,
	struct seed_fixture *fixtures)
{
	static const enum cut_mask masks[] = {
		MASK_EVEN,
		MASK_ODD,
		MASK_FIRST_LAST,
		MASK_MIDDLE_QUARTER,
		MASK_EVERY_FOURTH,
		MASK_RANDOM_BYTES,
		MASK_PARTIAL_BITS,
	};
	struct recovery_operation operations[MUTATION_JOURNAL_CAPACITY];
	uint8_t journal_media[REGION_SIZE];
	uint8_t cut_media[REGION_SIZE];
	size_t operation_count;

	memset(shared, 0, sizeof(*shared));
	make_clean_image(shared);
	make_candidate_source(shared);
	memcpy(candidate_old_media, shared->media, REGION_SIZE);
	memcpy(journal_media, shared->media, REGION_SIZE);
	record_fixture(fixtures, shared->media);
	assert(run_child(shared, CHILD_COMMIT_CANDIDATE) == 0);
	assert(shared->child_result == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(independent_ftw_clean(shared->media));
	assert(bytes_are(shared->media + SPARE_OFFSET, SPARE_SIZE, 0xffU));
	memcpy(candidate_new_media, shared->media, REGION_SIZE);
	record_fixture(fixtures, shared->media);
	operation_count = trace_operations(shared, operations,
		ARRAY_SIZE(operations));
	assert(operation_count && operation_count == shared->mutation_journal_count);
	for (size_t operation = 0; operation < operation_count; operation++) {
		const struct recovery_operation *item = &operations[operation];
		const struct mutation_journal_entry *entry =
			&shared->mutation_journal[operation];

		assert(item->kind == (entry->kind == TRACE_PROGRAM ?
			CUT_PROGRAM : CUT_ERASE));
		assert(item->offset == entry->offset && item->size == entry->size);
		assert(!memcmp(journal_media + item->offset, entry->before,
			item->size));
		for (uint32_t prefix = 0; prefix <= item->size; prefix++) {
			make_cut_image(cut_media, journal_media, item, entry,
				MASK_PREFIX, prefix);
			record_fixture(fixtures, cut_media);
		}
		for (size_t mask = 0; mask < ARRAY_SIZE(masks); mask++) {
			make_cut_image(cut_media, journal_media, item, entry,
				masks[mask], 0);
			record_fixture(fixtures, cut_media);
		}
		memcpy(journal_media + item->offset, entry->after, item->size);
	}
	assert(!memcmp(journal_media, candidate_new_media, REGION_SIZE));
}

static void capture_apply_boundaries(struct shared_state *shared,
	struct seed_fixture *fixtures, const uint8_t base[REGION_SIZE],
	enum child_operation operation)
{
	struct recovery_operation operations[128];
	uint8_t final[REGION_SIZE];
	size_t operation_count;

	memset(shared, 0, sizeof(*shared));
	memcpy(shared->media, base, REGION_SIZE);
	assert(run_child(shared, operation) == 0);
	trace_sessions_valid(shared);
	operation_count = trace_operations(shared, operations,
		ARRAY_SIZE(operations));
	assert(operation_count);
	memcpy(final, shared->media, REGION_SIZE);
	record_fixture(fixtures, final);
	for (size_t i = 0; i < operation_count; i++) {
		static const uint32_t positions[] = { 0U, UINT32_MAX };

		for (size_t position = 0; position < ARRAY_SIZE(positions); position++) {
			uint32_t prefix = positions[position] == UINT32_MAX ?
				operations[i].size : positions[position];

			memset(shared, 0, sizeof(*shared));
			memcpy(shared->media, base, REGION_SIZE);
			shared->cut_kind = operations[i].kind;
			shared->cut_occurrence = operations[i].occurrence;
			shared->cut_bytes = prefix;
			shared->cut_mask = MASK_PREFIX;
			assert(run_child(shared, operation) == CHILD_CUT_EXIT);
			trace_sessions_valid(shared);
			record_fixture(fixtures, shared->media);
		}
	}
}

static void discover_fixtures(struct shared_state *shared,
	struct seed_fixture *fixtures)
{
	uint8_t base[REGION_SIZE];
	bool second = false;

	prepare_first_value(shared);
	record_fixture(fixtures, shared->media);
	memset(shared->media + WORKING_OFFSET, 0xff, BLOCK_SIZE);
	record_fixture(fixtures, shared->media);
	prepare_first_value(shared);
	memcpy(base, shared->media, REGION_SIZE);
	capture_apply_boundaries(shared, fixtures, base, CHILD_APPLY_REPLACE);

	prepare_first_value(shared);
	for (uint32_t transaction = 0; transaction < 64U; transaction++) {
		enum child_operation operation = second ? CHILD_APPLY_ADD :
			CHILD_APPLY_REPLACE;

		memcpy(base, shared->media, REGION_SIZE);
		capture_apply_boundaries(shared, fixtures, base, operation);
		memset(shared, 0, sizeof(*shared));
		memcpy(shared->media, base, REGION_SIZE);
		assert(run_child(shared, operation) == 0);
		second = !second;
		if (fixtures[PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE].present &&
		    fixtures[PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE].present)
			break;
	}
	for (uint32_t pass = 0; pass < 4U; pass++) {
		for (enum payload_mm_authvar_ftw_action action =
			PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE;
		     action <= PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE; action++) {
			if (!fixtures[action].present)
				continue;
			memcpy(base, fixtures[action].media, REGION_SIZE);
			capture_apply_boundaries(shared, fixtures, base, CHILD_RECOVER);
		}
	}
	for (enum payload_mm_authvar_ftw_action action = PAYLOAD_MM_AUTHVAR_FTW_CLEAN;
	     action <= PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE; action++) {
		if (!fixtures[action].present) {
			char message[64];
			int length = snprintf(message, sizeof(message),
				"missing recursive action seed=%u\n", (unsigned int)action);

			assert(length > 0 && (size_t)length < sizeof(message));
			output(2, message, (size_t)length);
		}
		assert(fixtures[action].present);
	}
}

static void synthesize_operation_states(struct durable_state *states,
	uint32_t *slots, size_t *state_count,
	const struct durable_state *source, uint32_t source_index,
	enum payload_mm_authvar_ftw_action source_action,
	const struct recovery_operation *operation,
	const struct mutation_journal_entry *journal,
	const struct trace_entry *normal,
	uint8_t journal_media[REGION_SIZE])
{
	static const enum cut_mask masks[] = {
		MASK_EVEN,
		MASK_ODD,
		MASK_FIRST_LAST,
		MASK_MIDDLE_QUARTER,
		MASK_EVERY_FOURTH,
		MASK_RANDOM_BYTES,
		MASK_PARTIAL_BITS,
	};
	uint8_t *candidate = malloc(REGION_SIZE);
	const uint8_t *before = journal_media;
	uint32_t end;
	uint64_t hash;
	struct payload_mm_authvar_ftw_plan owner_plan;

	assert(candidate);
	assert(!__builtin_add_overflow(operation->offset, operation->size, &end));
	assert(end <= REGION_SIZE);
	assert(journal->kind == (operation->kind == CUT_PROGRAM ?
		TRACE_PROGRAM : TRACE_ERASE) &&
		journal->offset == operation->offset &&
		journal->size == operation->size &&
		journal->occurrence == operation->occurrence);
	assert(!memcmp(before + operation->offset, journal->before,
		operation->size));
	assert(payload_mm_authvar_ftw_plan(before, REGION_SIZE, BLOCK_SIZE,
		&owner_plan) == CB_SUCCESS);
	assert(normal->before_digest ==
		trace_digest(before + operation->offset, operation->size));
	assert(normal->after_digest ==
		trace_digest(journal->after, operation->size));

	memcpy(candidate, before, REGION_SIZE);
	hash = media_hash(candidate);
	(void)enqueue_state_hash(states, slots, state_count, candidate,
		source->depth + 1U, hash, source_index, owner_plan.action,
		source_action, owner_plan.workspace, owner_plan.queue_disposition,
		operation, MASK_PREFIX, 0);
	for (uint32_t byte = 0; byte < operation->size; byte++) {
		uint32_t offset = operation->offset + byte;
		uint8_t old = candidate[offset];
		uint8_t new = journal->after[byte];

		if (old == new)
			continue;
		hash ^= media_byte_hash(offset, old) ^ media_byte_hash(offset, new);
		candidate[offset] = new;
		(void)enqueue_state_hash(states, slots, state_count, candidate,
			source->depth + 1U, hash, source_index, owner_plan.action,
			source_action, owner_plan.workspace, owner_plan.queue_disposition,
			operation, MASK_PREFIX, byte + 1U);
	}
	for (size_t mask_index = 0; mask_index < ARRAY_SIZE(masks);
	     mask_index++) {
		enum cut_mask mask = masks[mask_index];
		bool changed = false;

		memcpy(candidate, before, REGION_SIZE);
		hash = media_hash(candidate);
		for (uint32_t byte = 0; byte < operation->size; byte++) {
			uint32_t offset = operation->offset + byte;
			uint8_t old = candidate[offset];

			if (!recursive_byte_selected(mask, byte, operation->size,
				operation->size / 2U))
				continue;
			if (mask != MASK_PARTIAL_BITS)
				candidate[offset] = journal->after[byte];
			else if (operation->kind == CUT_PROGRAM) {
				uint8_t requested = before[offset] &
					(uint8_t)~journal->after[byte];
				uint8_t partial = recursive_bit_subset(requested, byte);

				if (partial)
					candidate[offset] &= (uint8_t)~partial;
			} else {
				uint8_t requested = (uint8_t)~before[offset];
				uint8_t partial = recursive_bit_subset(requested, byte);

				if (partial)
					candidate[offset] |= partial;
			}
			if (old != candidate[offset]) {
				hash ^= media_byte_hash(offset, old) ^
					media_byte_hash(offset, candidate[offset]);
				changed = true;
			}
		}
		if (changed)
			(void)enqueue_state_hash(states, slots, state_count, candidate,
				source->depth + 1U, hash, source_index, owner_plan.action,
				source_action, owner_plan.workspace,
				owner_plan.queue_disposition, operation, mask,
				operation->size / 2U);
	}
	free(candidate);
	memcpy(journal_media + operation->offset, journal->after,
		operation->size);
}

static void report_witness(const struct durable_state *states, uint32_t index)
{
	uint32_t cursor = index;

	while (states[cursor].parent != UINT32_MAX) {
		const struct durable_state *state = &states[cursor];
		const struct durable_state *parent = &states[state->parent];
		struct payload_mm_authvar_ftw_plan state_plan;
		struct payload_mm_authvar_ftw_plan parent_plan;
		char message[256];
		uint32_t end;
		int length;

		assert(state->parent < cursor && state->depth == parent->depth + 1U);
		assert(payload_mm_authvar_ftw_plan(parent->media, REGION_SIZE,
			BLOCK_SIZE, &parent_plan) == CB_SUCCESS);
		assert(payload_mm_authvar_ftw_plan(state->media, REGION_SIZE,
			BLOCK_SIZE, &state_plan) == CB_SUCCESS);
		assert(state->origin_parent_action == (uint32_t)parent_plan.action);
		assert(!__builtin_add_overflow(state->origin_offset,
			state->origin_size, &end) && end <= REGION_SIZE);
		length = snprintf(message, sizeof(message),
			"witness state=%u depth=%u action=%u parent=%u parent-action=%u "
			"pre-plan=%u/%u/%u cut=%u/%u offset=%u size=%u mask=%u prefix=%u "
			"workspace=%u queue=%u queue-offset=%u\n",
			cursor, state->depth, (unsigned int)state_plan.action,
			state->parent, (unsigned int)parent_plan.action,
			state->origin_pre_action, state->origin_pre_workspace,
			state->origin_pre_queue,
			state->origin_kind, state->origin_occurrence,
			state->origin_offset, state->origin_size, state->origin_mask,
			state->origin_prefix, (unsigned int)state_plan.workspace,
			(unsigned int)state_plan.queue_disposition,
			state_plan.queue_offset);
		assert(length > 0 && (size_t)length < sizeof(message));
		output(1, message, (size_t)length);
		cursor = state->parent;
	}
	assert(!states[cursor].depth && states[cursor].parent == UINT32_MAX);
}

static void validate_discard_witness(const struct durable_state *states,
	uint32_t index, const struct payload_mm_authvar_ftw_plan *state_plan)
{
	const struct durable_state *state = &states[index];
	struct payload_mm_authvar_ftw_plan parent_plan;
	uint32_t selected_offset;
	uint32_t selected_size;
	uint32_t end;

	assert(state->parent < index);
	assert(payload_mm_authvar_ftw_plan(states[state->parent].media, REGION_SIZE,
		BLOCK_SIZE, &parent_plan) == CB_SUCCESS);
	assert(parent_plan.action == PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED);
	assert(parent_plan.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE);
	assert(state->origin_parent_action == PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED &&
		state->origin_pre_action == PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED &&
		state->origin_pre_workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE &&
		state->origin_pre_queue == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE);
	selected_offset = parent_plan.geometry.spare_offset;
	selected_size = parent_plan.geometry.spare_size;
	assert(!__builtin_add_overflow(state->origin_offset, state->origin_size,
		&end));
	assert(state->origin_offset >= selected_offset &&
		end <= selected_offset + selected_size);
	switch (state_plan->action) {
	case PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE:
		assert(state->origin_kind == CUT_ERASE &&
			state->origin_offset == selected_offset &&
			state->origin_size == selected_size &&
			state->origin_mask == MASK_PREFIX && state->origin_prefix ==
				PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET &&
			state_plan->workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING &&
			state_plan->queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE &&
			!state_plan->queue_offset);
		break;
	case PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE:
		assert(state->origin_kind == CUT_PROGRAM && state->origin_size == 1U &&
			state->origin_offset == selected_offset +
				PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET &&
			state->origin_mask == MASK_PREFIX && state->origin_prefix == 1U &&
			state_plan->workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE &&
			state_plan->queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY &&
			state_plan->queue_offset == PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE);
		break;
	case PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE:
		assert(state->origin_kind == CUT_ERASE &&
			state->origin_offset == selected_offset &&
			state->origin_size == selected_size &&
			state->origin_mask == MASK_PARTIAL_BITS &&
			state_plan->workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE &&
			state_plan->queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE &&
			!state_plan->queue_offset);
		break;
	default:
		abort();
	}
}

static void validate_cleanup_witness(const struct durable_state *states,
	uint32_t index, enum payload_mm_authvar_ftw_action seeded_action,
	const struct payload_mm_authvar_ftw_plan *state_plan)
{
	const struct durable_state *state = &states[index];
	const struct durable_state *parent;
	struct payload_mm_authvar_ftw_plan parent_plan;

	assert(state_plan->action == PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE &&
		state_plan->workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE &&
		state_plan->queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE &&
		!state_plan->queue_offset);
	assert(state->parent < index);
	parent = &states[state->parent];
	assert(payload_mm_authvar_ftw_plan(parent->media, REGION_SIZE, BLOCK_SIZE,
		&parent_plan) == CB_SUCCESS);
	if (seeded_action == PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE) {
		assert(state->parent == 0U &&
			parent_plan.action == PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE &&
			state->origin_parent_action ==
				PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE &&
			state->origin_pre_action ==
				PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE &&
			state->origin_pre_workspace ==
				PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING &&
			state->origin_pre_queue == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE &&
			state->origin_kind == CUT_PROGRAM &&
			state->origin_offset == parent_plan.geometry.spare_offset &&
			state->origin_size == PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET &&
			state->origin_mask == MASK_PREFIX && state->origin_prefix == 1U);
		return;
	}
	assert(seeded_action == PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE ||
		seeded_action == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	assert(parent_plan.action == PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED &&
		parent_plan.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE &&
		parent->parent == 0U && parent->origin_parent_action == seeded_action &&
		state->origin_parent_action == PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED &&
		state->origin_pre_action == PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED &&
		state->origin_pre_workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE &&
		state->origin_pre_queue == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE &&
		state->origin_kind == CUT_ERASE &&
		state->origin_offset == parent_plan.geometry.spare_offset &&
		state->origin_size == parent_plan.geometry.spare_size &&
		state->origin_mask == MASK_PARTIAL_BITS);
}

static size_t explore_action(struct shared_state *shared,
	enum payload_mm_authvar_ftw_action seeded_action,
	const uint8_t seed_media[REGION_SIZE])
{
	struct durable_state *states = calloc(RECURSIVE_STATE_LIMIT, sizeof(*states));
	uint32_t *slots = calloc(RECURSIVE_HASH_SLOTS, sizeof(*slots));
	uint8_t *expected_media = malloc(REGION_SIZE);
	uint8_t *journal_media = malloc(REGION_SIZE);
	struct trace_entry *baseline = malloc(sizeof(*baseline) * TRACE_CAPACITY);
	struct mutation_journal_entry *journal = malloc(sizeof(*journal) *
		MUTATION_JOURNAL_CAPACITY);
	size_t state_count = 0;
	bool action_seen[PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE + 1U] = { false };
	uint32_t action_first[PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE + 1U];
	uint32_t action_mask = 0;
	enum logical_value expected_value;
	uint64_t recovery_count = 0;
	uint64_t candidate_count = 0;
	static const uint32_t expected_masks[] = {
		[PAYLOAD_MM_AUTHVAR_FTW_CLEAN] =
			(1U << PAYLOAD_MM_AUTHVAR_FTW_CLEAN),
		[PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE] =
			(1U << PAYLOAD_MM_AUTHVAR_FTW_CLEAN) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE),
		[PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED] =
			(1U << PAYLOAD_MM_AUTHVAR_FTW_CLEAN) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE),
		[PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE] =
			(1U << PAYLOAD_MM_AUTHVAR_FTW_CLEAN) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE),
		[PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD] =
			(1U << PAYLOAD_MM_AUTHVAR_FTW_CLEAN) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD),
		[PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE] =
			(1U << PAYLOAD_MM_AUTHVAR_FTW_CLEAN) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_COMPLETE_NEW) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE),
		[PAYLOAD_MM_AUTHVAR_FTW_COMPLETE_NEW] =
			(1U << PAYLOAD_MM_AUTHVAR_FTW_CLEAN) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_COMPLETE_NEW) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE),
		[PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE] =
			(1U << PAYLOAD_MM_AUTHVAR_FTW_CLEAN) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE),
		[PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE] =
			(1U << PAYLOAD_MM_AUTHVAR_FTW_CLEAN) |
			(1U << PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE),
	};

	assert(states && slots && expected_media && journal_media && baseline &&
		journal);
	for (size_t i = 0; i < ARRAY_SIZE(action_first); i++)
		action_first[i] = UINT32_MAX;
	memset(shared, 0, sizeof(*shared));
	memcpy(shared->media, seed_media, REGION_SIZE);
	if (seeded_action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN) {
		assert(run_child(shared, CHILD_RECOVER) == 0);
		trace_sessions_valid(shared);
	}
	assert(independent_ftw_clean(shared->media));
	expected_value = independent_logical_value(shared->media);
	if (!candidate_expectation_active)
		assert(expected_value == LOGICAL_FIRST ||
			expected_value == LOGICAL_SECOND);
	if (candidate_expectation_active) {
		assert(!memcmp(shared->media, candidate_old_media, VARIABLE_SIZE) ||
			!memcmp(shared->media, candidate_new_media, VARIABLE_SIZE));
		assert(bytes_are(shared->media + SPARE_OFFSET, SPARE_SIZE, 0xffU));
	}
	memcpy(expected_media, shared->media, REGION_SIZE);
	memset(shared, 0, sizeof(*shared));
	memcpy(shared->media, seed_media, REGION_SIZE);
	assert(enqueue_state(states, slots, &state_count, shared->media, 0));
	for (size_t current = 0; current < state_count; current++) {
		struct payload_mm_authvar_ftw_plan plan;
		struct recovery_operation operations[64];
		size_t operation_count;
		uint32_t baseline_count;

		progress_seed = (unsigned int)seeded_action;
		progress_current = current;
		progress_recoveries = recovery_count;
		progress_candidates = candidate_count;
		memcpy(shared->media, states[current].media, REGION_SIZE);
		assert(payload_mm_authvar_ftw_plan(shared->media, REGION_SIZE,
			BLOCK_SIZE, &plan) == CB_SUCCESS);
		if (!action_seen[plan.action]) {
			action_first[plan.action] = (uint32_t)current;
			if (seeded_action == PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED &&
			    (plan.action == PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE ||
			     plan.action == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE ||
			     plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE))
				validate_discard_witness(states, (uint32_t)current, &plan);
			if ((seeded_action == PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE ||
			     seeded_action == PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE ||
			     seeded_action == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE) &&
			    plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE)
				validate_cleanup_witness(states, (uint32_t)current,
					seeded_action, &plan);
			if (plan.action != seeded_action)
				report_witness(states, (uint32_t)current);
		}
		action_seen[plan.action] = true;
		action_mask |= 1U << plan.action;
		if (plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN) {
			assert(independent_ftw_clean(shared->media));
			assert(independent_logical_value(shared->media) == expected_value);
			assert(!memcmp(shared->media, expected_media, REGION_SIZE));
			continue;
		}
		recovery_count++;
		operation_count = recovery_operations(shared, operations,
			ARRAY_SIZE(operations), expected_media, expected_value);
		assert(operation_count);
		/*
		 * The real seed is the first interrupted boot.  Exhaustively cut its
		 * recovery once more, then require every resulting second-reset image
		 * to complete a fresh recovery.  Real seeds cover every initial decoder
		 * action.  This deliberately makes no claim beyond the fixed reset
		 * budget or for an unbounded reset schedule.
		 */
		if (states[current].depth >= RECOVERY_RESET_BUDGET)
			continue;
		assert(shared->mutation_journal_count == operation_count);
		assert(shared->trace_count <= TRACE_CAPACITY);
		baseline_count = shared->trace_count;
		memcpy(baseline, shared->trace,
			baseline_count * sizeof(*baseline));
		memcpy(journal, shared->mutation_journal,
			operation_count * sizeof(*journal));
		if (!current)
			calibrate_journal(shared, states[current].media, operations,
				operation_count, journal, baseline, baseline_count);
		memcpy(journal_media, states[current].media, REGION_SIZE);
		for (size_t operation = 0; operation < operation_count; operation++) {
			candidate_count += (uint64_t)operations[operation].size + 8U;
			synthesize_operation_states(states, slots, &state_count,
				&states[current], (uint32_t)current, plan.action,
				&operations[operation],
				&journal[operation],
				&baseline[operations[operation].trace_index], journal_media);
		}
		if ((current & 127U) == 127U) {
			char message[160];
			int length = snprintf(message, sizeof(message),
				"recursive progress seed=%u current=%zu states=%zu "
				"recoveries=%llu candidates=%llu\n",
				(unsigned int)seeded_action, current + 1U, state_count,
				(unsigned long long)recovery_count,
				(unsigned long long)candidate_count);

			assert(length > 0 && (size_t)length < sizeof(message));
			output(1, message, (size_t)length);
		}
	}
	assert(action_seen[seeded_action]);
	assert(action_seen[PAYLOAD_MM_AUTHVAR_FTW_CLEAN]);
	for (size_t action = 0; action < ARRAY_SIZE(action_first); action++)
		assert(action_seen[action] == (action_first[action] != UINT32_MAX));
	if (action_mask != expected_masks[seeded_action]) {
		char message[96];
		int length = snprintf(message, sizeof(message),
			"recursive seed=%u expected-mask=0x%08x actual=0x%08x\n",
			(unsigned int)seeded_action, expected_masks[seeded_action],
			action_mask);

		assert(length > 0 && (size_t)length < sizeof(message));
		output(2, message, (size_t)length);
	}
	assert(action_mask == expected_masks[seeded_action]);
	{
		char message[96];
		int length = snprintf(message, sizeof(message),
			"recursive seed=%u action-mask=0x%08x\n",
			(unsigned int)seeded_action, action_mask);

		assert(length > 0 && (size_t)length < sizeof(message));
		output(1, message, (size_t)length);
	}
	free(slots);
	free(states);
	free(expected_media);
	free(journal_media);
	free(journal);
	free(baseline);
	return state_count;
}

int main(int argc, char **argv)
{
	struct shared_state *shared = mmap(NULL, sizeof(*shared),
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	struct seed_fixture *fixtures = calloc(
		PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE + 1U, sizeof(*fixtures));
	struct seed_fixture *candidate_fixtures = calloc(
		PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE + 1U,
		sizeof(*candidate_fixtures));
	uint8_t malformed[REGION_SIZE];
	size_t total_states = 0;

	assert(shared != MAP_FAILED && fixtures && candidate_fixtures);
	assert(argc == 1 || (argc == 2 && (!strcmp(argv[1], "seeds") ||
		(strlen(argv[1]) == 1U && argv[1][0] >= '1' && argv[1][0] <= '9'))));
	discover_fixtures(shared, fixtures);
	discover_candidate_fixtures(shared, candidate_fixtures);
	if (argc == 2 && !strcmp(argv[1], "seeds")) {
		free(candidate_fixtures);
		free(fixtures);
		assert(munmap(shared, sizeof(*shared)) == 0);
		return 0;
	}
	memcpy(malformed, fixtures[PAYLOAD_MM_AUTHVAR_FTW_CLEAN].media,
		REGION_SIZE);
	malformed[WORKING_OFFSET] &= 0xfeU;
	assert(memcmp(malformed, fixtures[PAYLOAD_MM_AUTHVAR_FTW_CLEAN].media,
		REGION_SIZE));
	memset(shared, 0, sizeof(*shared));
	memcpy(shared->media, malformed, REGION_SIZE);
	shared->suppress_diagnostics = 1U;
	assert(run_child(shared, CHILD_RECOVER) == 1);
	trace_sessions_valid(shared);
	assert(!memcmp(shared->media, malformed, REGION_SIZE));
	for (enum payload_mm_authvar_ftw_action action = PAYLOAD_MM_AUTHVAR_FTW_CLEAN;
	     action <= PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE; action++) {
		char message[96];
		size_t states;
		int length;

		if (argc == 2 && action !=
		    (enum payload_mm_authvar_ftw_action)(argv[1][0] - '0'))
			continue;
		states = explore_action(shared, action, fixtures[action].media);

		total_states += states;
		length = snprintf(message, sizeof(message), "recursive action=%u states=%zu\n",
			(unsigned int)action, states);
		assert(length > 0 && (size_t)length < sizeof(message));
		output(1, message, (size_t)length);
	}
	candidate_expectation_active = true;
	for (enum payload_mm_authvar_ftw_action action = PAYLOAD_MM_AUTHVAR_FTW_CLEAN;
	     action <= PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE; action++) {
		char message[112];
		size_t states;
		int length;

		if (!candidate_fixtures[action].present ||
		    (argc == 2 && action !=
		     (enum payload_mm_authvar_ftw_action)(argv[1][0] - '0')))
			continue;
		states = explore_action(shared, action,
			candidate_fixtures[action].media);
		total_states += states;
		length = snprintf(message, sizeof(message),
			"recursive candidate action=%u states=%zu\n",
			(unsigned int)action, states);
		assert(length > 0 && (size_t)length < sizeof(message));
		output(1, message, (size_t)length);
	}
	candidate_expectation_active = false;
	{
		char message[96];
		int length = snprintf(message, sizeof(message),
			"recursive total states=%zu\n", total_states);

		assert(length > 0 && (size_t)length < sizeof(message));
		output(1, message, (size_t)length);
	}
	free(candidate_fixtures);
	free(fixtures);
	assert(munmap(shared, sizeof(*shared)) == 0);
	return 0;
}
