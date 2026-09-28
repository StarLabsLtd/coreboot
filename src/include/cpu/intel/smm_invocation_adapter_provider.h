/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_INTEL_SMM_INVOCATION_ADAPTER_PROVIDER_H
#define CPU_INTEL_SMM_INVOCATION_ADAPTER_PROVIDER_H

#include <cpu/x86/smm_invocation_evidence.h>
#include <rules.h>

#if ENV_SMM || ENV_TEST
/*
 * The returned descriptor is protected and byte-stable while the provider is
 * usable. A terminal provider error invalidates and scrubs it. Its callbacks
 * are valid only during the exact owned interval between one successful arm
 * and its matching retire. The sole trusted SMM handler must not retain or
 * invoke a descriptor copy outside that interval.
 */
enum smm_invocation_try_result
intel_smm_invocation_adapter_provider_provision(
	const struct smm_invocation_save_state_ops **ops);
enum smm_invocation_try_result
intel_smm_invocation_adapter_provider_arm(uint64_t *generation);
enum smm_invocation_try_result
intel_smm_invocation_adapter_provider_retire(uint64_t generation);
#endif

#endif
