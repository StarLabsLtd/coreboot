/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TCG_OPAL_S3_H
#define SECURITY_TCG_OPAL_S3_H

#include <cpu/x86/smm.h>
#include <security/tcg/opal_s3_wire.h>
#include <stddef.h>
#include <types.h>

struct opal_s3_pci_resources {
	struct smm_pci_resource_info slots[CONFIG_SMM_PCI_RESOURCE_STORE_NUM_SLOTS];
	bool ready;
};

void opal_s3_prepare_pci_resources(struct smm_runtime *runtime);
int opal_s3_import_pci_resources(const volatile struct smm_runtime *runtime);
const struct smm_pci_resource_info *opal_s3_get_pci_resources(void);

#endif
