/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <cbmem.h>
#include <commonlib/cfr.h>
#include <cpu/x86/smm.h>
#include <drivers/option/cfr_settings.h>
#include <string.h>

void lb_cfr_settings(struct lb_header *header)
{
	const struct cbmem_entry *entry = cbmem_entry_find(CBMEM_ID_CFR_SETTINGS);
	struct lb_cfr_settings *settings;

	if (!entry || cbmem_entry_size(entry) < CFR_SETTINGS_MAILBOX_SIZE)
		return;

	settings = (struct lb_cfr_settings *)lb_new_record(header);
	memset(settings, 0, sizeof(*settings));
	settings->tag = LB_TAG_CFR_SETTINGS;
	settings->size = sizeof(*settings);
	settings->version = LB_CFR_SETTINGS_VERSION;
	settings->mailbox_address = (uintptr_t)cbmem_entry_start(entry);
	settings->mailbox_size = CFR_SETTINGS_MAILBOX_SIZE;
	settings->apm_cmd = APM_CNT_CFR_SETTINGS;
}
