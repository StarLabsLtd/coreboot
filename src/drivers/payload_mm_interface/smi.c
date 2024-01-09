/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/cpu.h>
#include <bootmode.h>
#include <commonlib/region.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <payload_mm_interface.h>
#include <string.h>
#include <types.h>

static uint32_t payload_mm_get_entrypoint(void)
{
	uintptr_t payload_mm_region_base;
	size_t payload_mm_region_size;

	payload_mm_get_reserved_region(&payload_mm_region_base, &payload_mm_region_size);

	struct payload_mm_shared_info *payload_mm_shared_mem = (void *)payload_mm_region_base;
	if (payload_mm_shared_mem->header_magic != PLD_MM_SHARED_STRUCT_MAGIC ||
	    payload_mm_shared_mem->shared_info_size > PLD_MM_SHARED_STRUCT_MAX_SIZE)
		return 0;

	// TODO: Further consider in which situations a size mismatch is recoverable.
	if (payload_mm_shared_mem->shared_info_size != sizeof(*payload_mm_shared_mem) ||
	    payload_mm_shared_mem->header_revision != PLD_MM_SHARED_STRUCT_REVISION)
		printk(BIOS_WARNING, "MM version mismatch! Bootloader: %d; Payload: %d.\n",
		       PLD_MM_SHARED_STRUCT_REVISION, payload_mm_shared_mem->header_revision);

	if (payload_mm_shared_mem->mm_entrypoint_address < payload_mm_region_base ||
	    payload_mm_shared_mem->mm_entrypoint_address > (payload_mm_region_base + payload_mm_region_size)) {
		printk(BIOS_WARNING, "Payload set MM entrypoint outside of the reserved region!\n");
		return 0;
	}

	return payload_mm_shared_mem->mm_entrypoint_address;
}

static uint8_t payload_mm_load_and_call_core_module(void *argument)
{
	uintptr_t payload_mm_region_base;
	size_t payload_mm_region_size;
	struct region payload_mm_core_module = { 0 };

	payload_mm_get_reserved_region(&payload_mm_region_base, &payload_mm_region_size);

	struct payload_mm_load_context *load_context = argument;
	printk(BIOS_DEBUG, "Payload MM registration, param = %p.\n", load_context);

	// TODO: Further consider in which situations a size mismatch is recoverable.
	if (load_context->header_size != sizeof(*load_context) || load_context->header_revision != PLD_MM_LOAD_CONTEXT_REVISION)
		printk(BIOS_WARNING, "MM version mismatch! Bootloader: %d; Payload: %d.\n",
		       PLD_MM_LOAD_CONTEXT_REVISION, load_context->header_revision);

	uint64_t mm_src_address = load_context->mm_core_source_address;
	uintptr_t mm_dest_address = load_context->mm_core_destination_address;
	size_t payload_mm_core_size = load_context->mm_core_size;

	if (!ENV_X86_64 && mm_src_address >= (1ULL << 32)) {
		printk(BIOS_ERR, "Payload MM source address is not reachable!\n");
		return PAYLOAD_MM_RET_FAILURE;
	}

	if (smm_points_to_smram((void *)(uintptr_t)mm_src_address, payload_mm_core_size)) {
		printk(BIOS_ERR, "Payload MM source address is within SMRAM?\n");
		return PAYLOAD_MM_RET_FAILURE;
	}

	enum cb_err status = region_create_untrusted(&payload_mm_core_module, mm_dest_address, payload_mm_core_size);
	if (status != CB_SUCCESS) {
		printk(BIOS_ERR, "Payload provided a malformed MM core region!\n");
		return PAYLOAD_MM_RET_FAILURE;
	}

	struct region payload_mm_region = region_create(payload_mm_region_base, payload_mm_region_size);
	if (!region_is_subregion(&payload_mm_region, &payload_mm_core_module)) {
		printk(BIOS_ERR, "Payload tried to load MM core outside of the reserved region!\n");
		return PAYLOAD_MM_RET_FAILURE;
	}

	if (load_context->mm_entrypoint_offset >= payload_mm_core_size) {
		printk(BIOS_ERR, "Payload set MM entrypoint outside of the loaded core!\n");
		return PAYLOAD_MM_RET_FAILURE;
	}

	memcpy((void *)mm_dest_address, (void *)(uintptr_t)mm_src_address, payload_mm_core_size);
	printk(BIOS_DEBUG, "Payload MM loaded core module to 0x%lx.\n", mm_dest_address);

	struct payload_mm_core_call_context *call_context = (void *)(payload_mm_region_base + PLD_MM_SHARED_STRUCT_MAX_SIZE);
	call_context->mm_entrypoint_arg1 = load_context->mm_entrypoint_arg1;
	call_context->mm_entrypoint_arg2 = load_context->mm_entrypoint_arg2;
	call_context->mm_entrypoint_arg3 = load_context->mm_entrypoint_arg3;
	call_context->implementation_private_data = load_context->implementation_private_data;

	uint32_t mm_entrypoint_address = mm_dest_address + load_context->mm_entrypoint_offset;
	uint8_t (*mm_entrypoint)(struct payload_mm_core_call_context *arg) = (void *)(uintptr_t)mm_entrypoint_address;

	uint8_t mm_status = mm_entrypoint(call_context);
	printk(BIOS_DEBUG, "Payload MM called core module at 0x%x.\n", mm_entrypoint_address);

	return mm_status;
}

uint8_t payload_mm_exec_interface(uint8_t sub_command, void *argument)
{
	// TODO: Query command (if we'll have secondary consumers)?
	uint32_t mm_entrypoint_address = payload_mm_get_entrypoint();
	if (mm_entrypoint_address) {
		printk(BIOS_WARNING, "Payload MM already registered at 0x%x!\n", mm_entrypoint_address);
		return PAYLOAD_MM_RET_FAILURE;
	}

	if (sub_command == PAYLOAD_MM_CMD_LOAD_AND_CALL_CORE) {
		return payload_mm_load_and_call_core_module(argument);
	}

	printk(BIOS_WARNING, "Unrecognised subcommand 0x%x!\n", sub_command);
	return PAYLOAD_MM_RET_FAILURE;
}

void payload_mm_call_entrypoint(void)
{
	uint32_t mm_entrypoint_address = payload_mm_get_entrypoint();
	if (!mm_entrypoint_address) {
		printk(BIOS_WARNING, "Payload MM not yet registered.\n");
		return;
	}

	// FIXME: Refactor this. TODO: Return value here too?
	void (*mm_entrypoint)(void *arg) = (void *)(uintptr_t)mm_entrypoint_address;
	mm_entrypoint(NULL);

	printk(BIOS_DEBUG, "Payload MM returns.\n");
}
