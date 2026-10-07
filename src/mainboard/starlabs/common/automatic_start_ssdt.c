/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpigen.h>
#include <common/automatic_start.h>
#include <ec/starlabs/merlin/ec.h>
#include <soc/iomap.h>
#include <soc/pmc.h>

void starlabs_automatic_start_ssdt(void)
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
