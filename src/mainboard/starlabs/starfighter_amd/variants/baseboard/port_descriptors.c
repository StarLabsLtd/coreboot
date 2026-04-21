/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <baseboard/variants.h>
#include <baseboard/port_descriptors.h>
#include <soc/platform_descriptors.h>

enum baseboard_dxio_port_id {
	BASEBOARD_DXIO_WLAN,
	BASEBOARD_DXIO_SD,
	BASEBOARD_DXIO_NVME0,
};

static const fsp_dxio_descriptor arn39_dxio_descriptors[] = {
	[BASEBOARD_DXIO_WLAN] = WLAN_DXIO_DESCRIPTOR,
	[BASEBOARD_DXIO_SD] = SD_DXIO_DESCRIPTOR,
	[BASEBOARD_DXIO_NVME0] = NVME0_DXIO_DESCRIPTOR,
};

__weak void variant_get_dxio_descriptors(const fsp_dxio_descriptor **dxio_descriptor, size_t *num)
{
	*dxio_descriptor = arn39_dxio_descriptors;
	*num = ARRAY_SIZE(arn39_dxio_descriptors);
}
