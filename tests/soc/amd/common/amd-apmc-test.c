/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <soc/amd/common/block/cpu/smm/smi_apmc.c>

static amd64_smm_state_save_area_t states[CONFIG_MAX_CPUS];
static bool current[CONFIG_MAX_CPUS];
static unsigned int cpu_count, waits, late_cpu, arrival;

void *smm_get_save_state(int cpu)
{
	return &states[cpu];
}

void *smm_get_current_save_state(unsigned int cpu)
{
	return cpu < cpu_count && current[cpu] ? &states[cpu] : NULL;
}

unsigned int smm_get_cpu_count(void)
{
	return cpu_count;
}

void udelay(unsigned int us)
{
	assert_int_equal(us, 1);
	waits++;
	if (waits == arrival)
		current[late_cpu] = true;
}

u16 pm_acpi_smi_cmd_port(void)
{
	return 0xb2;
}

static uint8_t command;
static unsigned int registrations, seals, finalizations, communications, board_calls;
static int registration_result, finalization_result;
static bool sealed;

u8 apm_get_apmc(void) { return command; }
void fch_disable_power_button_smi(void) { }
void fch_enable_power_button_smi(void) { }
void acpi_clear_pm_gpe_status(void) { }
void acpi_enable_sci(void) { }
void acpi_disable_sci(void) { }
int mainboard_smi_apmc(u8 cmd)
{
	assert_int_equal(cmd, command);
	board_calls++;
	return 0;
}
int psp_notify_smm(void)
{
	registrations++;
	return registration_result;
}
bool psp_rom_armor_seal(void)
{
	seals++;
	return sealed;
}
int efi_variable_service_finalize(void)
{
	assert_true(sealed);
	assert_true(seals > finalizations);
	finalizations++;
	return finalization_result;
}
uint32_t efi_variable_service_communicate(void)
{
	communications++;
	return EFI_VARIABLE_SERVICE_RET_IO_ERROR;
}

static int setup(void **unused)
{
	memset(states, 0, sizeof(states));
	memset(current, 1, sizeof(current));
	cpu_count = CONFIG_MAX_CPUS;
	waits = arrival = late_cpu = 0;
	for (unsigned int cpu = 0; cpu < CONFIG_MAX_CPUS; cpu++) {
		states[cpu].rax = APM_CNT_ROM_ARMOR;
		states[cpu].smm_io_trap_offset = (0xb2 << 16) | SMM_IO_TRAP_VALID | SMM_IO_TRAP_SZ8;
	}
	return 0;
}

static void select_current_issuer(void **unused)
{
	/* CPU zero joins after the handler starts its lookup. */
	current[0] = false;
	late_cpu = 0;
	arrival = 2;
	for (unsigned int cpu = 1; cpu < CONFIG_MAX_CPUS; cpu++)
		if (cpu != 2)
			states[cpu].rax = 0;
	/* Only CPU two issued this command in the current generation. */
	if (CONFIG(ASS))
		states[0].rax = 0;
	assert_ptr_equal(find_save_state(APM_CNT_ROM_ARMOR), &states[CONFIG(ASS) ? 2 : 0]);
	assert_int_equal(waits, CONFIG(ASS) ? 2 : 0);
	states[2].rax = APM_CNT_SMMINFO;
	assert_ptr_equal(find_save_state(APM_CNT_SMMINFO), &states[2]);
}

static void reject_ambiguous_issuer(void **unused)
{
	states[0].rax = states[2].rax = 0;
	current[3] = false;
	late_cpu = 3;
	arrival = 3;
	if (CONFIG(ASS))
		assert_null(find_save_state(APM_CNT_ROM_ARMOR));
	else
		assert_ptr_equal(find_save_state(APM_CNT_ROM_ARMOR), &states[1]);
}

static void reject_wrong_io(void **unused)
{
	const uint32_t rejected[] = {
		0, SMM_IO_TRAP_VALID | SMM_IO_TRAP_RW | SMM_IO_TRAP_SZ8,
		SMM_IO_TRAP_VALID, SMM_IO_TRAP_VALID | SMM_IO_TRAP_SZ16,
		SMM_IO_TRAP_VALID | SMM_IO_TRAP_SZ32,
		SMM_IO_TRAP_VALID | SMM_IO_TRAP_SZ8 | SMM_IO_TRAP_SZ16,
		SMM_IO_TRAP_VALID | SMM_IO_TRAP_SZ8 | SMM_IO_TRAP_STR,
		SMM_IO_TRAP_VALID | SMM_IO_TRAP_SZ8 | SMM_IO_TRAP_REP,
	};

	current[0] = true;
	/* Other cores must not accidentally satisfy the legacy search. */
	for (unsigned int cpu = 1; cpu < CONFIG_MAX_CPUS; cpu++)
		states[cpu].rax = 0;
	for (size_t i = 0; i < ARRAY_SIZE(rejected); i++) {
		states[0].smm_io_trap_offset = (0xb2 << 16) | rejected[i];
		if (CONFIG(ASS) || i < 2)
			assert_null(find_save_state(APM_CNT_ROM_ARMOR));
		else
			assert_ptr_equal(find_save_state(APM_CNT_ROM_ARMOR), &states[0]);
	}
	states[0].smm_io_trap_offset = (0xb3 << 16) | SMM_IO_TRAP_VALID | SMM_IO_TRAP_SZ8;
	assert_null(find_save_state(APM_CNT_ROM_ARMOR));
	states[0].smm_io_trap_offset = (0xb2 << 16) | SMM_IO_TRAP_VALID | SMM_IO_TRAP_SZ8;
	states[0].rax = APM_CNT_SMMINFO;
	assert_null(find_save_state(APM_CNT_ROM_ARMOR));
}

static void incomplete_or_smaller_runtime(void **unused)
{
	if (!CONFIG(ASS))
		return;
	current[3] = false;
	assert_null(find_save_state(APM_CNT_ROM_ARMOR));
	assert_int_equal(waits, 1000);
	cpu_count = 2;
	states[1].rax = 0;
	waits = 0;
	assert_ptr_equal(find_save_state(APM_CNT_ROM_ARMOR), &states[0]);
	assert_int_equal(waits, 0);
	cpu_count = 0;
	assert_null(find_save_state(APM_CNT_ROM_ARMOR));
	cpu_count = CONFIG_MAX_CPUS + 1;
	assert_null(find_save_state(APM_CNT_ROM_ARMOR));
}

static void checked_service_dispatch(void **unused)
{
	for (unsigned int cpu = 0; cpu < CONFIG_MAX_CPUS; cpu++)
		states[cpu].rax = 0;
	registrations = seals = finalizations = communications = board_calls = 0;
	command = APM_CNT_SMMINFO;
	states[2].rax = command;
	registration_result = -1;
	fch_apmc_smi_handler();
	assert_int_equal(registrations, 1);
	assert_int_equal(states[2].rax, CONFIG(ASS) ? 1 : command);
	registration_result = 0;
	states[2].rax = command;
	fch_apmc_smi_handler();
	assert_int_equal(states[2].rax, CONFIG(ASS) ? 0 : command);

	command = APM_CNT_FINALIZE;
	states[2].rax = command;
	sealed = false;
	fch_apmc_smi_handler();
	assert_int_equal(finalizations, 0);
	assert_int_equal(states[2].rax, CONFIG(ASS) ? APM_FINALIZE_EFI_ERROR : command);
	sealed = true;
	const int results[] = { -1, EFI_VARIABLE_SERVICE_REBOOT_REQUIRED, 0 };
	const uint32_t expected[] = { APM_FINALIZE_EFI_ERROR, APM_FINALIZE_REBOOT_REQUIRED, 0 };
	for (size_t i = 0; i < ARRAY_SIZE(results); i++) {
		states[2].rax = command;
		finalization_result = results[i];
		fch_apmc_smi_handler();
		assert_int_equal(states[2].rax, CONFIG(ASS) ? expected[i] : command);
	}
	assert_int_equal(finalizations, CONFIG(ASS) ? 3 : 0);
	command = APM_CNT_EFI_VARIABLE_SERVICE;
	states[2].rax = command;
	fch_apmc_smi_handler();
	assert_int_equal(communications, CONFIG(ASS) ? 1 : 0);
	assert_int_equal(states[2].rax, CONFIG(ASS) ? EFI_VARIABLE_SERVICE_RET_IO_ERROR : command);
	assert_int_equal(board_calls, 7);

	if (CONFIG(ASS)) {
		/* Ambiguous requests must not enter any of the protected operations. */
		const uint8_t commands[] = { APM_CNT_SMMINFO, APM_CNT_FINALIZE,
			APM_CNT_EFI_VARIABLE_SERVICE };
		for (size_t i = 0; i < ARRAY_SIZE(commands); i++) {
			command = commands[i];
			states[1].rax = states[2].rax = command;
			fch_apmc_smi_handler();
			assert_int_equal(states[1].rax, command);
			assert_int_equal(states[2].rax, command);
		}
		assert_int_equal(registrations, 2);
		assert_int_equal(finalizations, 3);
		assert_int_equal(communications, 1);
	}
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(select_current_issuer, setup),
		cmocka_unit_test_setup(reject_ambiguous_issuer, setup),
		cmocka_unit_test_setup(reject_wrong_io, setup),
		cmocka_unit_test_setup(incomplete_or_smaller_runtime, setup),
		cmocka_unit_test_setup(checked_service_dispatch, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
