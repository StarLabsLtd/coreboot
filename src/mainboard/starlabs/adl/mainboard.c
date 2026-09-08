/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpigen.h>
#include <bootstate.h>
#include <device/device.h>
#include <ec/starlabs/merlin/ec.h>
#include <soc/iomap.h>
#include <soc/pmc.h>
#include <variants.h>

static void starlabs_configure_mainboard(void *unused)
{
	const struct pad_config *pads;
	size_t num;

	pads = variant_gpio_table(&num);
	gpio_configure_pads(pads, num);
}

BOOT_STATE_INIT_ENTRY(BS_PRE_DEVICE, BS_ON_ENTRY, starlabs_configure_mainboard, NULL);

void __weak starlabs_adl_mainboard_fill_ssdt(const struct device *dev)
{
	(void)dev;
}

static void automatic_start_ssdt(void)
{
	static const struct opregion region = OPREGION("ASPR", SYSTEMMEMORY,
		PCH_PWRM_BASE_ADDRESS + GEN_PMCON_A, 1);
	static const struct fieldlist fields[] = {
		FIELDLIST_NAMESTR("ASPF", 1), /* SLEEP_AFTER_POWER_FAIL */
	};

	acpigen_write_scope("\\_SB");
	{
		acpigen_write_opregion(&region);
		acpigen_write_field("ASPR", fields, ARRAY_SIZE(fields),
			FIELD_BYTEACC | FIELD_NOLOCK | FIELD_PRESERVE);
		/* ASAC returns whether the chipset accepted this automatic-start policy. */
		acpigen_write_method_serialized("ASAC", 1);
		{
			acpigen_write_if();
			{
				acpigen_emit_byte(LGREATER_OP);
				acpigen_emit_byte(ARG0_OP);
				acpigen_write_integer(AUTOMATIC_START_NEVER);
				acpigen_write_return_integer(0);
			}
			acpigen_write_if_end();
			acpigen_emit_byte(STORE_OP);
			acpigen_emit_byte(LEQUAL_OP);
			acpigen_emit_byte(ARG0_OP);
			acpigen_write_integer(AUTOMATIC_START_NEVER);
			acpigen_emit_byte(LOCAL0_OP);
			acpigen_write_store_op_to_namestr(LOCAL0_OP, "ASPF");
			acpigen_emit_byte(RETURN_OP);
			acpigen_emit_byte(LEQUAL_OP);
			acpigen_emit_namestring("ASPF");
			/* AML true is all ones, whereas the hardware field is one bit. */
			acpigen_emit_byte(AND_OP);
			acpigen_emit_byte(LOCAL0_OP);
			acpigen_write_integer(1);
			acpigen_emit_byte(ZERO_OP);
		}
		acpigen_write_method_end();
	}
	acpigen_write_scope_end();
}

static void starlabs_mainboard_fill_ssdt(const struct device *dev)
{
	if (CONFIG(PAYLOAD_MM_INTERFACE) && CONFIG(STARLABS_AUTOMATIC_START))
		automatic_start_ssdt();
	merlin_fill_ssdt(dev);
	starlabs_adl_mainboard_fill_ssdt(dev);
}

static void enable_mainboard(struct device *dev)
{
	dev->ops->acpi_fill_ssdt = starlabs_mainboard_fill_ssdt;
}

struct chip_operations mainboard_ops = {
	.enable_dev = enable_mainboard,
};
