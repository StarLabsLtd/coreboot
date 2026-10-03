/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cdk2/linear_boot.h>
#include <pi/hob.h>

#define RESULT __typeof__(cdk2_linear_state_validate(NULL))
#define TICKS __typeof__(((struct cdk2_linear_plan *)0)->clock(NULL))
#define FLAG __typeof__(((struct cdk2_linear_phase *)0)->required)
#define COUNT __typeof__(((struct cdk2_linear_plan *)0)->count)
#define FIELD(type, field) fprintf(stderr, #type "." #field "=%zu\n", offsetof(struct type, field))

struct trace {
	uint64_t status;
	uint64_t elapsed;
	uint32_t phase;
	uint8_t required;
	uint8_t begin;
};
struct context {
	uint64_t ticks;
	uint64_t result;
	unsigned int reports;
	unsigned int runs;
	struct trace trace[4];
};

static TICKS clock_read(void *opaque)
{
	return ((struct context *)opaque)->ticks;
}

static RESULT phase_run(void *opaque)
{
	struct context *context = opaque;

	context->runs++;
	context->ticks += 64U;
	return context->result;
}

static void report(enum cdk2_linear_phase_id phase, const char *name,
	RESULT status, TICKS elapsed, FLAG required, FLAG begin, void *opaque)
{
	struct context *context = opaque;
	struct trace *trace = &context->trace[context->reports++];

	assert(name != NULL && context->reports <= 4);
	trace->status = status;
	trace->elapsed = elapsed;
	trace->phase = phase;
	trace->required = required;
	trace->begin = begin;
}

int main(void)
{
	static const uint64_t results[] = {0, 1, 0x123456789abcdef0ULL,
		0x8000000000000007ULL, UINT64_MAX};
	struct cdk2_linear_state state;
	struct {
		EFI_HOB_HANDOFF_INFO_TABLE handoff;
		EFI_HOB_GUID_TYPE guid;
		struct cdk2_linear_state state;
		EFI_HOB_GENERIC_HEADER end;
	} hobs = {0};
	struct cdk2_linear_state imported;

	fprintf(stderr, "state=%zu/%zu phase=%zu/%zu plan=%zu/%zu\n",
		sizeof(state), _Alignof(struct cdk2_linear_state),
		sizeof(struct cdk2_linear_phase), _Alignof(struct cdk2_linear_phase),
		sizeof(struct cdk2_linear_plan), _Alignof(struct cdk2_linear_plan));
	FIELD(cdk2_linear_phase, id); FIELD(cdk2_linear_phase, name);
	FIELD(cdk2_linear_phase, run); FIELD(cdk2_linear_phase, required);
	FIELD(cdk2_linear_plan, phases); FIELD(cdk2_linear_plan, count);
	FIELD(cdk2_linear_plan, clock); FIELD(cdk2_linear_plan, report);
	FIELD(cdk2_linear_plan, context);
	FIELD(cdk2_linear_state, revision); FIELD(cdk2_linear_state, length);
	FIELD(cdk2_linear_state, next_phase); FIELD(cdk2_linear_state, reserved);
	FIELD(cdk2_linear_state, completed_mask); FIELD(cdk2_linear_state, flags);
	FIELD(cdk2_linear_state, splash_framebuffer);
	FIELD(cdk2_linear_state, splash_bytes_per_line);
	FIELD(cdk2_linear_state, splash_x_resolution);
	FIELD(cdk2_linear_state, splash_y_resolution);
	FIELD(cdk2_linear_state, splash_image_offset_x);
	FIELD(cdk2_linear_state, splash_image_offset_y);
	FIELD(cdk2_linear_state, splash_image_width);
	FIELD(cdk2_linear_state, splash_image_height);
	FIELD(cdk2_linear_state, splash_bmp_address);
	FIELD(cdk2_linear_state, splash_bmp_size); FIELD(cdk2_linear_state, reserved2);
	FIELD(cdk2_linear_state, boot_device_path_offset);
	FIELD(cdk2_linear_state, boot_device_path_size);
	FIELD(cdk2_linear_state, boot_device_path_generation);
	FIELD(cdk2_linear_state, boot_device_path_hash);
	FIELD(cdk2_linear_state, boot_loader_size);
	FIELD(cdk2_linear_state, boot_loader_path); FIELD(cdk2_linear_state, boot_device_path);
	for (unsigned int flag = 0; flag < 256; flag++) {
		for (size_t result = 0; result < sizeof(results) / sizeof(results[0]); result++) {
			struct cdk2_linear_phase phases[2] = {0};
			struct cdk2_linear_plan plan = {0};
			struct context context = {0};
			COUNT completed = 99;
			uint64_t status;

			context.ticks = UINT64_MAX - 17;
			context.result = results[result];
			phases[0].id = CDK2_LINEAR_HANDOFF_VALIDATE;
			phases[1].id = CDK2_LINEAR_SPLASH_EARLY;
			for (size_t index = 0; index < 2; index++) {
				phases[index].name = "phase";
				phases[index].run = phase_run;
				phases[index].required = flag;
			}
			plan.phases = phases; plan.count = 2;
			plan.clock = clock_read; plan.report = report; plan.context = &context;
			cdk2_linear_state_init(&state);
			status = cdk2_linear_run(&plan, &state, &completed);
			assert(status == (flag && results[result] ? results[result] : 0));
			assert(completed == (flag && results[result] ? 0 : 2));
			assert(context.runs == (flag && results[result] ? 1 : 2));
			assert(context.trace[1].elapsed == 64 && context.trace[1].required == flag);
			fwrite(&completed, sizeof(completed), 1, stdout);
			fwrite(&status, sizeof(status), 1, stdout);
			fwrite(context.trace, sizeof(context.trace), 1, stdout);
			fwrite(&state, sizeof(state), 1, stdout);
			/* Mask only pointers/function addresses, retaining all native padding. */
			plan.phases = NULL; plan.clock = NULL; plan.report = NULL; plan.context = NULL;
			for (size_t index = 0; index < 2; index++) {
				phases[index].name = NULL; phases[index].run = NULL;
			}
			fwrite(phases, sizeof(phases), 1, stdout);
			fwrite(&plan, sizeof(plan), 1, stdout);
		}
		for (unsigned int mode = 0; mode < 2; mode++) {
			enum cdk2_linear_capsule_outcome outcome;
			cdk2_linear_state_init(&state);
			state.next_phase = CDK2_LINEAR_CAPSULE_DECIDE;
			state.completed_mask = (1U << state.next_phase) - 1U;
			assert(cdk2_linear_capsule_gate_early(&state, mode ? flag : 0,
				mode ? 0 : flag, &outcome) == EFI_SUCCESS);
			assert(outcome == (flag ? (mode ? CDK2_LINEAR_CAPSULE_RESULT_RAM :
				CDK2_LINEAR_CAPSULE_RESULT_DISK_DEFERRED) : CDK2_LINEAR_CAPSULE_RESULT_NONE));
			fwrite(&state, sizeof(state), 1, stdout);
		}
	}
	cdk2_linear_state_init(&state);
	hobs.handoff.header.hob_type = EFI_HOB_TYPE_HANDOFF;
	hobs.handoff.header.hob_length = sizeof(hobs.handoff);
	hobs.handoff.efi_end_of_hob_list = (UINTN)&hobs.end;
	hobs.guid.header.hob_type = EFI_HOB_TYPE_GUID_EXTENSION;
	hobs.guid.header.hob_length = sizeof(hobs.guid) + sizeof(hobs.state);
	hobs.guid.name = cdk2_linear_state_hob_guid;
	hobs.state = state;
	hobs.end.hob_type = EFI_HOB_TYPE_END_OF_HOB_LIST;
	hobs.end.hob_length = sizeof(hobs.end);
	assert(cdk2_linear_state_from_hob(&hobs, &imported) == EFI_SUCCESS);
	assert(!memcmp(&imported, &state, sizeof(state)));
	assert(cdk2_linear_state_update_hob(&hobs, &state) == EFI_SUCCESS);
	assert(!memcmp(&hobs.state, &state, sizeof(state)));
	fwrite(&imported, sizeof(imported), 1, stdout);
	/* Real path setter/hash and serialized loader bytes, not a model digest. */
	{
		const uint8_t path[] = {1, 1, 6, 0, 0x31, 0x42, 0x7f, 0xff, 4, 0};
		const CHAR16 loader[] = L"\\EFI\\BOOT\\BOOTX64.EFI";

		state.next_phase = CDK2_LINEAR_BOOT_POLICY;
		state.completed_mask = (1U << state.next_phase) - 1U;
		state.flags = CDK2_LINEAR_CAPSULE_RESULT_NONE << CDK2_LINEAR_STATE_CAPSULE_SHIFT;
		memcpy(state.boot_loader_path, loader, sizeof(loader));
		state.boot_loader_size = 4096U;
		assert(cdk2_linear_boot_path_set(&state, path, sizeof(path), 0xf1234567) == EFI_SUCCESS);
		assert(cdk2_linear_state_validate(&state) == EFI_SUCCESS);
		assert(cdk2_linear_state_update_hob(&hobs, &state) == EFI_SUCCESS);
		assert(cdk2_linear_state_from_hob(&hobs, &imported) == EFI_SUCCESS);
		assert(!memcmp(&imported, &state, sizeof(state)));
		fwrite(&imported, sizeof(imported), 1, stdout);
	}
	return ferror(stdout) ? 1 : 0;
}
