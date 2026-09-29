/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_X86_SMM_INVOCATION_RUNTIME_H
#define CPU_X86_SMM_INVOCATION_RUNTIME_H

#include <commonlib/bsd/cb_err.h>
#include <cpu/x86/smm_save_state.h>
#include <rules.h>
#include <stdint.h>

#if ENV_SMM || ENV_TEST
/*
 * Opaque handle to checked protected SMM runtime geometry. Save-state spans are
 * sensitive writable register authority and are therefore exposed only by a
 * bounded per-CPU snapshot. This layer validates allocation and native-span
 * arithmetic; a later architecture adapter must validate revision, exact
 * native size and any revision-specific alignment. Every failure leaves the
 * caller's output unchanged.
 */
struct smm_invocation_runtime_view;

#if CONFIG(SMM_INVOCATION_RUNTIME_BINDING)
struct smm_invocation_runtime_binding {
	const struct smm_invocation_loader_composition *composition;
	const struct smm_invocation_loader_instance *instance;
	struct smm_invocation_evidence *evidence;
	const struct smm_invocation_topology *topology;
};
#endif

enum cb_err smm_invocation_runtime_view_get(
	const struct smm_invocation_runtime_view **view);
enum cb_err smm_invocation_runtime_cpu_count(
	const struct smm_invocation_runtime_view *view, uint32_t *active_cpus);
enum cb_err smm_invocation_runtime_save_state_span(
	const struct smm_invocation_runtime_view *view, uint32_t cpu,
	struct smm_save_state_span *span);
/* Returns only the checked loader-owned objects, never the raw SMM runtime. */
#if CONFIG(SMM_INVOCATION_RUNTIME_BINDING)
enum cb_err smm_invocation_runtime_binding_get(
	struct smm_invocation_runtime_binding *binding);
#endif
#endif

#endif
