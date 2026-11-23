/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/save_state.h>
#include <intelblocks/fast_spi.h>
#include <smmstore.h>
#include <southbridge/intel/common/insmm_sts.h>

#include "spi_write.h"

bool smmstore_begin_write(bool *restore)
{
	*restore = !fast_spi_wpd_status();
	if (!*restore)
		return true;

	set_insmm_sts(true);
	/* The chipset requires SYNC_SS to be clear before setting WPD. */
	smmstore_drain_sync_smi();
	fast_spi_disable_wp();
	if (fast_spi_wpd_status())
		return true;
	smmstore_end_write(true);
	return false;
}

void smmstore_end_write(bool restore)
{
	smmstore_drain_sync_smi();
	if (!restore)
		return;
	fast_spi_enable_wp();
	set_insmm_sts(false);
	if (fast_spi_wpd_status())
		die("SMMSTORE: cannot restore SPI write protection.");
}

void southbridge_smi_store(
	const struct smm_save_state_ops *save_state_ops)
{
	u8 sub_command, ret;
	int node;
	uint32_t eax_val, reg_ebx;

	node = save_state_ops->apmc_node(APM_CNT_SMMSTORE);
	if (node < 0)
		return;
	if (save_state_ops->get_reg(RAX, node, &eax_val, sizeof(eax_val)) != 0)
		return;
	/* Command and return value in EAX */
	sub_command = (eax_val >> 8) & 0xff;

	if (save_state_ops->get_reg(RBX, node, &reg_ebx, sizeof(reg_ebx)) != 0)
		return;

	ret = smmstore_exec(sub_command, (void *)(uintptr_t)reg_ebx);
	save_state_ops->set_reg(RAX, node, &ret, sizeof(ret));
}
