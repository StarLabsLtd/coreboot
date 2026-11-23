/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tcg/opal_s3.h>
#include <string.h>

static struct opal_s3_pci_resources imported;
static bool consumed;

const struct smm_pci_resource_info *opal_s3_get_pci_resources(void)
{
	return imported.ready ? imported.slots : NULL;
}

int opal_s3_import_pci_resources(const volatile struct smm_runtime *runtime)
{
	const struct opal_s3_pci_resources *pending = runtime->opal_resources_pending;

	/* Consume the fixed ramstage source before checking it; never reimport it. */
	if (consumed)
		return -1;
	consumed = true;
	if (runtime->s3_resume)
		return 0;
	if (!pending || smm_points_to_smram(pending, sizeof(*pending)) || !pending->ready)
		return -1;

	memcpy(imported.slots, pending->slots, sizeof(imported.slots));
	for (size_t i = 0; i < ARRAY_SIZE(imported.slots); i++) {
		struct resource *resources = imported.slots[i].resources;

		for (size_t j = 0; j < ARRAY_SIZE(imported.slots[i].resources); j++)
			resources[j].next = resources[j].next && j + 1 <
				ARRAY_SIZE(imported.slots[i].resources) ? &resources[j + 1] : NULL;
	}
	imported.ready = true;
	return 0;
}
