/* SPDX-License-Identifier: GPL-2.0-only */

#define __SIMPLE_DEVICE__

#include <device/pci_ops.h>
#include <intelblocks/cse.h>
#include <intelblocks/me.h>
#include <soc/pci_devs.h>

uint32_t me_read_config32(int offset)
{
	return pci_read_config32(PCH_DEV_CSE, offset);
}

bool cse_is_hfs1_com_soft_temp_disable(void)
{
	union me_hfsts1 hfs1;

	hfs1.data = me_read_config32(PCI_ME_HFSTS1);
	return hfs1.fields.operation_mode == ME_HFS1_COM_SOFT_TEMP_DISABLE;
}
