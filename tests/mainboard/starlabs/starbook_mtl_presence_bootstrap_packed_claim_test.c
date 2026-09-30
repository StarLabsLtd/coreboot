/* SPDX-License-Identifier: GPL-2.0-only */

#define main receiver_boundary_main
#define require boundary_require
#include "starbook_mtl_presence_bootstrap_receiver_test.c"
#undef main
#undef require
extern int dprintf(int descriptor, const char *format, ...);
#define require(condition) do { if (!(condition)) { dprintf(2, "packed claim line %d: %s\n", __LINE__, #condition); exit(90); } } while (0)
#include <cpu/intel/em64t101_save_state.h>
#include <cpu/intel/smm_invocation_adapter.h>

void smm_invocation_evidence_test_hook(uint32_t point) { (void)point; }
void intel_smm_invocation_adapter_test_hook(uint32_t point) { (void)point; }
size_t intel_smm_invocation_adapter_test_revision_size(uint32_t revision, size_t size)
{
	(void)revision;
	return size;
}
void __noreturn smm_invocation_platform_fail_stop(void) { exit(91); }

enum cb_err starbook_mtl_authvar_service_bootstrap_install(void)
{
	struct smm_invocation_token token;
	require(smm_invocation_evidence_claimed_snapshot(&evidence,
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE, wire, &token) == CB_SUCCESS);
	return CB_SUCCESS;
}

int main(int argc, char **argv)
{
	em64t101_smm_state_save_area_t native = { 0 };
	struct intel_smm_invocation_adapter adapter;
	struct smm_invocation_token token;
	struct smm_invocation_admission_token admission;
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION, .size = sizeof(seed),
		.active_cpus = 1, .bsp_cpu = 0, .participant_apic_ids = { 8 },
		.loader_instance_nonce = { .low = 17, .high = 19 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	uint64_t generation, readback;
	uintptr_t top = (uintptr_t)&native + sizeof(native);
	require(argc == 2);
	const unsigned int mode = (unsigned int)strtoul(argv[1], NULL, 10);
	topology.active_cpus = 1;
	topology.bsp_cpu = 0;
	slot.binding.maximum_cpus = 64;
	memory.frame.base = (uintptr_t)&frame;
	memory.frame.size = sizeof(frame);
	frame = (struct starbook_mtl_presence_bootstrap_frame) {
		.revision = STARBOOK_MTL_PRESENCE_BOOTSTRAP_REVISION,
		.size = sizeof(frame), .state = STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST,
	};
	native.smm_revision = 0x30101U;
	native.io_misc_info = 0x00b20003U;
	native.rax = STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST;
	native.rcx = (uintptr_t)&frame;
	wire = native.rax | (native.rcx << 32);
	require((wire >> 32) != 0 && (wire >> 32) == memory.frame.base);
	require(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(native), native.smm_revision) == CB_SUCCESS);
	require(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	require(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	require(smm_invocation_evidence_arrive_try(&evidence, 0, 8, &generation,
		&admission) == SMM_INVOCATION_TRY_SUCCESS);
	const uint64_t sentinel = mode == 1 ?
		STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST : wire;
	require(smm_invocation_evidence_claim(&evidence,
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE, sentinel, &ops, &token) == CB_SUCCESS);
	require(ops.read_value(ops.context, 0, &readback) == CB_SUCCESS && readback == sentinel);
	if (mode == 2) memory.frame.base += 8;
	const enum starbook_mtl_presence_bootstrap_result result =
		starbook_mtl_presence_bootstrap_receive(&ops);
	if (mode) {
		require(result == STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR && !imports);
		return 0;
	}
	require(native.rcx == (uintptr_t)&frame && result == STARBOOK_MTL_PRESENCE_BOOTSTRAP_IMPORTED);
	require(imports == 1 && !writes && !factories);
	require(starbook_mtl_presence_bootstrap_route_install() == CB_SUCCESS);
	require(starbook_mtl_presence_bootstrap_response_stage(&ops) == CB_SUCCESS);
	require(native.rcx == (uintptr_t)&frame && frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST);
	require(smm_invocation_evidence_publish_and_request_close(&evidence, &token,
		STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS, &ops) == CB_SUCCESS);
	/* This is the real sender's exact return convention, not a retained pointer. */
	require(native.rax == STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS && !native.rcx);
	require(ops.read_value(ops.context, 0, &readback) == CB_SUCCESS &&
		readback == STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS);
	require(starbook_mtl_presence_bootstrap_response_publish() == CB_SUCCESS);
	require(frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_ACCEPTED);
	return 0;
}
