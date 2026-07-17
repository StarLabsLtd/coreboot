/* SPDX-License-Identifier: GPL-2.0-only */

#include <intelblocks/acpi_wake_source.h>
#include <soc/nvs.h>
#include <soc/pm.h>
#include <stdint.h>

void acpi_fill_wadt_wake_gnvs(struct global_nvs *gnvs, const uint32_t *gpe0)
{
	gnvs->wadt = !!(gpe0[GPE_STD] & WADT_STS);
}
