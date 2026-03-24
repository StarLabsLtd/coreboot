/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <console/console.h>
#include <fmap.h>
#include <pc80/mc146818rtc.h>

#include <mainboard/ab_slot.h>

static uint8_t starlabs_ab_normalize_slot(uint8_t slot)
{
	return (slot == STARLABS_AB_SLOT_B) ? STARLABS_AB_SLOT_B : STARLABS_AB_SLOT_A;
}

static uint8_t starlabs_ab_get_active_slot(void)
{
	return starlabs_ab_normalize_slot(cmos_read(STARLABS_AB_CMOS_ACTIVE) &
					  STARLABS_AB_PENDING_SLOT_MASK);
}

static bool starlabs_ab_get_current_slot(uint8_t *slot)
{
	uint8_t current;

	current = cmos_read(STARLABS_AB_CMOS_CURRENT);
	if ((current & STARLABS_AB_CURRENT_VALID) == 0)
		return false;

	if (slot)
		*slot = starlabs_ab_normalize_slot(current & STARLABS_AB_CURRENT_SLOT_MASK);

	return true;
}

static bool starlabs_ab_get_pending_slot(uint8_t *slot, uint8_t *tries)
{
	uint8_t pending;
	uint8_t remaining;

	pending = cmos_read(STARLABS_AB_CMOS_PENDING);
	if ((pending & STARLABS_AB_PENDING_VALID) == 0)
		return false;

	remaining = cmos_read(STARLABS_AB_CMOS_TRIES);
	if (remaining == 0)
		return false;

	if (slot)
		*slot = starlabs_ab_normalize_slot(pending & STARLABS_AB_PENDING_SLOT_MASK);

	if (tries)
		*tries = remaining;

	return true;
}

static uint8_t starlabs_ab_get_boot_slot(void)
{
	uint8_t slot;

	if (starlabs_ab_get_current_slot(&slot))
		return slot;

	if (starlabs_ab_get_pending_slot(&slot, NULL))
		return slot;

	return starlabs_ab_get_active_slot();
}

const char *cbfs_fmap_region_hint(void)
{
	if (starlabs_ab_get_boot_slot() == STARLABS_AB_SLOT_B)
		return "COREBOOT_B";

	return "COREBOOT";
}

void starlabs_ab_bootblock_init(void)
{
	uint8_t slot;
	uint8_t tries;

	if (!CONFIG(STARLABS_AB_COREBOOT))
		return;

	if (starlabs_ab_get_pending_slot(&slot, &tries)) {
		cmos_write(STARLABS_AB_CURRENT_VALID | slot, STARLABS_AB_CMOS_CURRENT);

		if (tries <= 1) {
			cmos_write(0, STARLABS_AB_CMOS_PENDING);
			cmos_write(0, STARLABS_AB_CMOS_TRIES);
			printk(BIOS_WARNING, "A/B: trying slot %c, last attempt before fallback\n",
			       (slot == STARLABS_AB_SLOT_B) ? 'B' : 'A');
			return;
		}

		cmos_write(tries - 1, STARLABS_AB_CMOS_TRIES);
		printk(BIOS_INFO, "A/B: trying slot %c, %u attempt(s) remain after this boot\n",
		       (slot == STARLABS_AB_SLOT_B) ? 'B' : 'A', tries - 1);
		return;
	}

	slot = starlabs_ab_get_active_slot();
	cmos_write(STARLABS_AB_CURRENT_VALID | slot, STARLABS_AB_CMOS_CURRENT);
}

static void starlabs_ab_mark_boot_success(void *unused)
{
	uint8_t slot;
	uint8_t active_slot;

	if (!CONFIG(STARLABS_AB_COREBOOT))
		return;

	if (!starlabs_ab_get_current_slot(&slot)) {
		cmos_write(0, STARLABS_AB_CMOS_CURRENT);
		return;
	}

	active_slot = starlabs_ab_get_active_slot();
	if (slot == active_slot) {
		cmos_write(0, STARLABS_AB_CMOS_CURRENT);
		return;
	}

	cmos_write(slot, STARLABS_AB_CMOS_ACTIVE);
	cmos_write(0, STARLABS_AB_CMOS_PENDING);
	cmos_write(0, STARLABS_AB_CMOS_TRIES);
	cmos_write(0, STARLABS_AB_CMOS_CURRENT);

	printk(BIOS_INFO, "A/B: promoted slot %c to active\n",
	       (slot == STARLABS_AB_SLOT_B) ? 'B' : 'A');
}

BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY, starlabs_ab_mark_boot_success, NULL);
