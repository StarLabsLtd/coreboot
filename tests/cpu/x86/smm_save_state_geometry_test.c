/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cpu/intel/em64t101_save_state.h>
#include <cpu/intel/smm_invocation_adapter.h>
#include <cpu/x86/smm_save_state.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define REV101 0x30101U
#define TEST_PSD_SIZE 0x100U

struct stm_save_state {
	uint8_t psd[TEST_PSD_SIZE];
	em64t101_smm_state_save_area_t state;
};

void intel_smm_invocation_adapter_test_hook(uint32_t point)
{
	(void)point;
}

size_t intel_smm_invocation_adapter_test_revision_size(
	uint32_t layout_revision, size_t native_size)
{
	(void)layout_revision;
	return native_size;
}

void __noreturn smm_invocation_platform_fail_stop(void)
{
	abort();
}

static void span_boundaries(void)
{
	uint8_t bytes[32];
	struct smm_save_state_span span = { .base = 1, .size = 1 };
	const uintptr_t top = (uintptr_t)(bytes + sizeof(bytes));

	assert(smm_save_state_native_span(top, sizeof(bytes), 0, &span) ==
		CB_SUCCESS);
	assert(span.base == (uintptr_t)bytes);
	assert(span.size == sizeof(bytes));
	assert(smm_save_state_native_span(top, sizeof(bytes), 8, &span) ==
		CB_SUCCESS);
	assert(span.base == (uintptr_t)(bytes + 8));
	assert(span.size == sizeof(bytes) - 8);

	span = (struct smm_save_state_span) { .base = 1, .size = 1 };
	assert(smm_save_state_native_span(top, 0, 0, &span) == CB_ERR);
	assert(!span.base && !span.size);
	span = (struct smm_save_state_span) { .base = 1, .size = 1 };
	assert(smm_save_state_native_span(top, 8, 8, &span) == CB_ERR);
	assert(!span.base && !span.size);
	span = (struct smm_save_state_span) { .base = 1, .size = 1 };
	assert(smm_save_state_native_span(top, 8, 9, &span) == CB_ERR);
	assert(!span.base && !span.size);
	span = (struct smm_save_state_span) { .base = 1, .size = 1 };
	assert(smm_save_state_native_span(8, 8, 0, &span) == CB_ERR);
	assert(!span.base && !span.size);
	assert(smm_save_state_native_span(top, sizeof(bytes), 0, NULL) == CB_ERR);

	assert(smm_save_state_native_span(UINTPTR_MAX, 1, 0, &span) ==
		CB_SUCCESS);
	assert(span.base == UINTPTR_MAX - 1U && span.size == 1U);
}

static void revision_boundaries(void)
{
	uint8_t bytes[64] = { 0 };
	struct smm_save_state_span span = {
		.base = (uintptr_t)bytes,
		.size = sizeof(bytes),
	};
	uint32_t expected = REV101;
	uint32_t revision = UINT32_MAX;

	memcpy(bytes + sizeof(bytes) - 12, &expected, sizeof(expected));
	assert(smm_save_state_revision_at(&span, 12, &revision) == CB_SUCCESS);
	assert(revision == expected);
	revision = UINT32_MAX;
	assert(smm_save_state_revision_at(&span, 3, &revision) == CB_ERR);
	assert(!revision);
	revision = UINT32_MAX;
	assert(smm_save_state_revision_at(&span, sizeof(bytes) + 1U,
		&revision) == CB_ERR);
	assert(!revision);
	span.base = 0;
	assert(smm_save_state_revision_at(&span, 12, &revision) == CB_ERR);
	span = (struct smm_save_state_span) {
		.base = UINTPTR_MAX - 1U,
		.size = 8,
	};
	revision = UINT32_MAX;
	assert(smm_save_state_revision_at(&span, 4, &revision) == CB_ERR);
	assert(!revision);
	assert(smm_save_state_revision_at(NULL, 12, &revision) == CB_ERR);
	assert(smm_save_state_revision_at(&span, 12, NULL) == CB_ERR);
}

static void stm_revision_uses_native_top(void)
{
	struct stm_save_state image = { 0 };
	struct smm_save_state_span span;
	uint32_t decoy = 0xdeadbeefU;
	uint32_t revision;
	const size_t offset = sizeof(image.state) -
		offsetof(em64t101_smm_state_save_area_t, smm_revision);

	image.state.smm_revision = REV101;
	memcpy(image.psd + sizeof(image.psd) - sizeof(decoy), &decoy,
		sizeof(decoy));
	assert(smm_save_state_native_span((uintptr_t)(&image + 1), sizeof(image),
		TEST_PSD_SIZE, &span) == CB_SUCCESS);
	assert(span.base == (uintptr_t)&image.state);
	assert(span.size == sizeof(image.state));
	assert(smm_save_state_revision_at(&span, offset, &revision) == CB_SUCCESS);
	assert(revision == REV101);
}

static void adapter_layouts(void)
{
	struct stm_save_state images[2] = { 0 };
	struct intel_smm_invocation_adapter adapter;
	uintptr_t tops[2] = {
		(uintptr_t)(&images[0] + 1),
		(uintptr_t)(&images[1] + 1),
	};
	uintptr_t duplicate[2] = { tops[0], tops[0] };

	images[0].state.smm_revision = REV101;
	images[1].state.smm_revision = REV101;
	assert(intel_smm_invocation_adapter_init_layout(&adapter, 2, tops,
		sizeof(images[0]), TEST_PSD_SIZE, REV101) == CB_SUCCESS);
	assert(adapter.nodes[0].save_state == (uintptr_t)&images[0].state);
	assert(adapter.nodes[1].save_state == (uintptr_t)&images[1].state);
	assert(intel_smm_invocation_adapter_init_layout(&adapter, 2, duplicate,
		sizeof(images[0]), TEST_PSD_SIZE, REV101) == CB_ERR);
	assert(intel_smm_invocation_adapter_init_layout(&adapter, 2, tops,
		sizeof(images[0]), TEST_PSD_SIZE - 1U, REV101) == CB_ERR);
	assert(intel_smm_invocation_adapter_init_layout(&adapter, 2, tops,
		TEST_PSD_SIZE, TEST_PSD_SIZE, REV101) == CB_ERR);
	assert(intel_smm_invocation_adapter_init_layout(&adapter, 0, tops,
		sizeof(images[0]), TEST_PSD_SIZE, REV101) == CB_ERR);
	assert(intel_smm_invocation_adapter_init_layout(&adapter,
		SMM_INVOCATION_EVIDENCE_MAX_CPUS + 1U, tops, sizeof(images[0]),
		TEST_PSD_SIZE, REV101) == CB_ERR);
	assert(intel_smm_invocation_adapter_init_layout(&adapter, 1, tops,
		sizeof(images[0]), TEST_PSD_SIZE, 0) == CB_ERR);

	assert(intel_smm_invocation_adapter_init(&adapter, 1, &tops[0],
		sizeof(images[0].state), REV101) == CB_SUCCESS);
	assert(adapter.nodes[0].save_state == (uintptr_t)&images[0].state);
	tops[0] = (uintptr_t)(&images[0].state + 1);
	assert(intel_smm_invocation_adapter_init(&adapter, 1, tops,
		sizeof(images[0].state), REV101) == CB_SUCCESS);
	assert(adapter.nodes[0].save_state == (uintptr_t)&images[0].state);
}

int main(void)
{
	span_boundaries();
	revision_boundaries();
	stm_revision_uses_native_top();
	adapter_layouts();
	return 0;
}
