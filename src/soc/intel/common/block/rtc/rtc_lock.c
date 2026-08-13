/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <intelblocks/rtc.h>

static void rtc_lock_cmos_memory(void *unused)
{
	rtc_conf_lock_cmos_memory();
}

static void rtc_lock_cmos_memory_before_payload(void *unused)
{
	if (!CONFIG(SOC_INTEL_COMMON_PCH_LOCKDOWN))
		rtc_conf_lock_cmos_memory();
}

BOOT_STATE_INIT_ENTRY(BS_OS_RESUME, BS_ON_ENTRY, rtc_lock_cmos_memory, NULL);
BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY, rtc_lock_cmos_memory_before_payload, NULL);
