/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi_gnvs.h>
#include <commonlib/helpers.h>
#include <stddef.h>
#include <payload_mm_interface.h>

#include <starlabs/efi_option_smi.h>

size_t size_of_dnvs(void)
{
	return sizeof(struct starlabs_dnvs_efiopt);
}

void mainboard_payload_mm_cfr_info(struct lb_payload_mm_interface_info *info)
{
	/* Keep the existing preference IDs while variable ownership changes. */
	uint32_t mask = BIT(STARLABS_EFIOPT_ID_FN_LOCK_STATE) |
		BIT(STARLABS_EFIOPT_ID_TRACKPAD_STATE) |
		BIT(STARLABS_EFIOPT_ID_KBL_BRIGHTNESS) |
		BIT(STARLABS_EFIOPT_ID_KBL_STATE) |
		BIT(STARLABS_EFIOPT_ID_KBL_TIMEOUT) |
		BIT(STARLABS_EFIOPT_ID_FN_CTRL_SWAP);

	if (CONFIG(EC_STARLABS_MAX_CHARGE))
		mask |= BIT(STARLABS_EFIOPT_ID_MAX_CHARGE);
	if (CONFIG(EC_STARLABS_FAN))
		mask |= BIT(STARLABS_EFIOPT_ID_FAN_MODE);
	if (CONFIG(EC_STARLABS_CHARGING_SPEED))
		mask |= BIT(STARLABS_EFIOPT_ID_CHARGING_SPEED);
	if (CONFIG(EC_STARLABS_LID_SWITCH))
		mask |= BIT(STARLABS_EFIOPT_ID_LID_SWITCH);
	if (CONFIG(EC_STARLABS_POWER_LED))
		mask |= BIT(STARLABS_EFIOPT_ID_POWER_LED);
	if (CONFIG(EC_STARLABS_CHARGE_LED))
		mask |= BIT(STARLABS_EFIOPT_ID_CHARGE_LED);
	if (CONFIG(STARLABS_AUTOMATIC_START))
		mask |= BIT(STARLABS_EFIOPT_ID_AUTOMATIC_START);
	else if (CONFIG(EC_STARLABS_ADAPTER_AUTO_POWER_ON))
		mask |= BIT(STARLABS_EFIOPT_ID_POWER_ON_AC);

	info->cfr_mailbox = (uintptr_t)acpi_get_device_nvs();
	info->cfr_mailbox_size = sizeof(struct starlabs_dnvs_efiopt);
	info->cfr_supported_options = mask;
}
