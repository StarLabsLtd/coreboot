/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/cpu.h>
#include <cpu/amd/msr.h>
#include <efi_variable_service.h>

bool efi_variable_service_entry_allowed(void)
{
	if (!cpu_is_amd())
		return true;

	const uint32_t max_leaf = cpuid_eax(0x80000000);

	/* AMD must preserve FIP/FDP/FOP even without a pending x87 exception. */
	if (max_leaf < 0x80000008 || !(cpuid_ebx(0x80000008) & BIT(2)))
		return false;

	/* SmmPgCfgLock forbids the guard's CR0/CR4/EFER writes for this SMI. */
	if (max_leaf >= 0x80000021 && (cpuid_eax(0x80000021) & BIT(3)))
		return !(rdmsr(HWCR_MSR).raw & SMM_PGCFG_LOCK);
	return true;
}
