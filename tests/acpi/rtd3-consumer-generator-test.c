/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <acpi/acpigen.h>
#include <acpi/acpi_device.h>
#include <intelblocks/smm_gpio.h>
#include <soc/intel/common/block/pcie/rtd3/chip.h>
#include <drivers/usb/acpi/chip.h>
#include <stdio.h>
#include <stdlib.h>
#include <tests/test.h>

/* The coreboot compatibility headers intentionally expose only firmware APIs. */
typedef struct _IO_FILE FILE;
extern FILE *fopen(const char *path, const char *mode);
extern size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
extern int fclose(FILE *stream);
extern char *getenv(const char *name);

#define AML_BUFFER_SIZE (16 * KiB)
#define ENABLE_PAD      10
#define RESET_PAD       11
#define DIRECT_PAD      12
#define CNVI_ENABLE_PAD 13
#define CNVI_SELECT_PAD 14

void pcie_rtd3_acpi_test_emit(const struct soc_intel_common_block_pcie_rtd3_config *config,
			      bool trigger_opal_s3_unlock);

bool mainboard_acpi_gpio_uses_smm(gpio_t pad)
{
	return pad != DIRECT_PAD;
}

const char *acpi_device_path_join(const struct device *dev, const char *name)
{
	assert_non_null(dev);
	if (!strcmp(name, RP_MUTEX_NAME))
		return "\\_SB.PCI0.RP01.RPMX";
	if (!strcmp(name, "RTD3._STA"))
		return "\\_SB.PCI0.RP01.RTD3._STA";
	fail_msg("unexpected joined ACPI name: %s", name);
}

void pmc_ipc_acpi_set_pci_clock(unsigned int pcie_rp, unsigned int clk_pin, bool enable)
{
	fail_msg("unexpected PCI clock generation: RP%u pin%u enable=%d", pcie_rp, clk_pin,
		 enable);
}

void acpi_device_intel_bt_pldr_reset(void)
{
	fail_msg("unexpected CNVi PLDR generation");
}

static void write_test_get_method(const char *name, const char *counter)
{
	acpigen_write_method(name, 1);
	acpigen_emit_byte(INCREMENT_OP);
	acpigen_emit_namestring(counter);
	acpigen_emit_byte(RETURN_OP);
	acpigen_emit_namestring("TVAL");
	acpigen_pop_len();
}

static void write_event(unsigned int value)
{
	acpigen_emit_byte(MULTIPLY_OP);
	acpigen_emit_namestring("EVTS");
	acpigen_write_integer(256);
	acpigen_emit_namestring("EVTS");
	acpigen_emit_byte(MULTIPLY_OP);
	acpigen_emit_byte(ARG0_OP);
	acpigen_write_integer(2);
	acpigen_emit_byte(LOCAL0_OP);
	acpigen_emit_byte(ADD_OP);
	acpigen_emit_namestring("EVTS");
	acpigen_emit_byte(LOCAL0_OP);
	acpigen_emit_namestring("EVTS");
	acpigen_emit_byte(ADD_OP);
	acpigen_emit_namestring("EVTS");
	acpigen_write_integer(value);
	acpigen_emit_namestring("EVTS");
}

static void write_test_set_method(const char *name, unsigned int value, const char *counter)
{
	acpigen_write_method(name, 1);
	acpigen_emit_byte(INCREMENT_OP);
	acpigen_emit_namestring(counter);
	write_event(value);
	acpigen_write_store_int_to_namestr(value, "TVAL");
	acpigen_write_return_integer(0);
	acpigen_pop_len();
}

static void write_direct_set_method(const char *name, unsigned int value)
{
	acpigen_write_method(name, 1);
	acpigen_emit_byte(INCREMENT_OP);
	acpigen_emit_namestring("DCNT");
	write_event(value);
	acpigen_write_store_int_to_namestr(value, "TVAL");
	acpigen_pop_len();
}

static void write_test_namespace(void)
{
	acpigen_write_scope("\\_SB");
	acpigen_write_device("PCI0");
	acpigen_write_name_integer("TVAL", 0);
	acpigen_write_name_integer("PGCT", 0);
	acpigen_write_name_integer("DGCT", 0);
	acpigen_write_name_integer("PCNT", 0);
	acpigen_write_name_integer("DCNT", 0);
	acpigen_write_name_integer("EVTS", 0);
	write_test_get_method("PGTX", "PGCT");
	write_test_set_method("PSTX", 1, "PCNT");
	write_test_set_method("PCTX", 0, "PCNT");
	write_test_get_method("GTXS", "DGCT");
	write_direct_set_method("STXS", 1);
	write_direct_set_method("CTXS", 0);
	acpigen_write_device("RP01");
	acpigen_write_device("BT00");
	acpigen_pop_len();
	acpigen_pop_len();
	acpigen_pop_len();
	acpigen_pop_len();
}

static uint8_t checksum(const uint8_t *data, size_t length)
{
	uint8_t sum = 0;

	for (size_t i = 0; i < length; i++)
		sum += data[i];

	return -sum;
}

static void write_table(const char *directory, const char *name,
			const struct soc_intel_common_block_pcie_rtd3_config *config,
			bool optional_cnvi)
{
	uint8_t buffer[AML_BUFFER_SIZE] = {0};
	acpi_header_t *header = (acpi_header_t *)buffer;
	char path[256];
	FILE *file;
	const struct acpi_gpio cnvi_enable = {
		.pin_count = optional_cnvi,
		.pins = {CNVI_ENABLE_PAD},
	};
	const struct acpi_gpio cnvi_select = {
		.pin_count = optional_cnvi,
		.pins = {CNVI_SELECT_PAD},
	};

	acpigen_set_current((char *)buffer + sizeof(*header));
	write_test_namespace();
	pcie_rtd3_acpi_test_emit(config, true);
	acpi_device_intel_bt_common(&config->enable_gpio, &config->reset_gpio);
	acpigen_write_scope("\\_SB.PCI0.RP01.BT00");
	acpi_device_intel_bt(&config->enable_gpio, &config->reset_gpio,
			     optional_cnvi ? &cnvi_enable : NULL,
			     optional_cnvi ? &cnvi_select : NULL, false, false);
	acpigen_write_scope_end();

	memcpy(header->signature, "SSDT", 4);
	header->length = acpigen_get_current() - (char *)buffer;
	header->revision = 2;
	memcpy(header->oem_id, "COREv4", 6);
	memcpy(header->oem_table_id, "RTD3TEST", 8);
	header->oem_revision = 1;
	memcpy(header->asl_compiler_id, "CORE", 4);
	header->asl_compiler_revision = 1;
	header->checksum = checksum(buffer, header->length);

	assert_true(snprintf(path, sizeof(path), "%s/%s.aml", directory, name) > 0);
	file = fopen(path, "wb");
	assert_non_null(file);
	assert_int_equal(fwrite(buffer, 1, header->length, file), header->length);
	assert_int_equal(fclose(file), 0);
}

static void write_gpio_get_method(const char *name, bool active_low)
{
	const struct acpi_gpio gpio = {
		.pin_count = 1,
		.pins = {ENABLE_PAD},
		.active_low = active_low,
	};

	acpigen_write_method(name, 0);
	acpigen_get_tx_gpio(&gpio);
	acpigen_write_return_op(LOCAL0_OP);
	acpigen_pop_len();
}

static void write_gpio_helper_table(const char *path)
{
	uint8_t buffer[AML_BUFFER_SIZE] = {0};
	acpi_header_t *header = (acpi_header_t *)buffer;
	FILE *file;

	acpigen_set_current((char *)buffer + sizeof(*header));
	acpigen_write_scope("\\_SB.PCI0");
	write_gpio_get_method("TGHI", false);
	write_gpio_get_method("TGLO", true);
	acpigen_pop_len();

	memcpy(header->signature, "SSDT", 4);
	header->length = acpigen_get_current() - (char *)buffer;
	header->revision = 2;
	memcpy(header->oem_id, "COREv4", 6);
	memcpy(header->oem_table_id, "GPIOHELP", 8);
	header->oem_revision = 1;
	memcpy(header->asl_compiler_id, "CORE", 4);
	header->asl_compiler_revision = 1;
	header->checksum = checksum(buffer, header->length);

	file = fopen(path, "wb");
	assert_non_null(file);
	assert_int_equal(fwrite(buffer, 1, header->length, file), header->length);
	assert_int_equal(fclose(file), 0);
}

static void test_generate_rtd3_aml(void **state)
{
	const char *directory = getenv("RTD3_AML_DIR");
	struct soc_intel_common_block_pcie_rtd3_config config = {
		.srcclk_pin = -1,
		.disable_l23 = true,
		.use_rp_mutex = true,
	};

	(void)state;
	if (!directory)
		skip();

	config.enable_gpio = (struct acpi_gpio){
		.pin_count = 1,
		.pins = {ENABLE_PAD},
	};
	config.reset_gpio = (struct acpi_gpio){
		.pin_count = 1,
		.pins = {RESET_PAD},
	};
	write_table(directory, "enable-high", &config, false);
	write_table(directory, "bt-optional", &config, true);

	config.enable_gpio.active_low = true;
	write_table(directory, "enable-low", &config, false);

	config.enable_gpio.pin_count = 0;
	config.reset_gpio.active_low = false;
	write_table(directory, "reset-high", &config, false);

	config.reset_gpio.active_low = true;
	write_table(directory, "reset-low", &config, false);

	config.enable_gpio = (struct acpi_gpio){
		.pin_count = 1,
		.pins = {DIRECT_PAD},
	};
	config.reset_gpio = (struct acpi_gpio){0};
	write_table(directory, "direct", &config, false);
}

static void test_generate_gpio_helper_aml(void **state)
{
	const char *path = getenv("SMM_GPIO_HELPER_AML");

	(void)state;
	if (!path)
		skip();

	write_gpio_helper_table(path);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_generate_rtd3_aml),
		cmocka_unit_test(test_generate_gpio_helper_aml),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
