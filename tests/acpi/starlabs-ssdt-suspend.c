/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <commonlib/bsd/compiler.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define AML_BUFFER_SIZE (16 * 1024)
#define EC_PATH "\\_SB_.PCI0.LPCB.EC__."
#define SNAPSHOT_EVALUATIONS \
	"evaluate " EC_PATH "EOBV; evaluate " EC_PATH "EOBF; " \
	"evaluate " EC_PATH "EOCT; evaluate " EC_PATH "EBTP; " \
	"evaluate " EC_PATH "EBFL; evaluate " EC_PATH "EBKS; " \
	"evaluate " EC_PATH "EBKB"
#define LIVE_EVALUATIONS \
	"evaluate " EC_PATH "EOBV; evaluate " EC_PATH "TPLE; " \
	"evaluate " EC_PATH "FLKE; evaluate " EC_PATH "KLSE; " \
	"evaluate " EC_PATH "KLBE"

struct acpi_header {
	char signature[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	char oem_id[6];
	char oem_table_id[8];
	uint32_t oem_revision;
	char asl_compiler_id[4];
	uint32_t asl_compiler_revision;
} __packed;

void starlabs_write_efi_option_sleep_aml_for_test(void);
void acpigen_set_current(char *current);
char *acpigen_get_current(void);

static uint8_t aml_buffer[AML_BUFFER_SIZE];

static uint8_t checksum(const uint8_t *data, size_t size)
{
	uint8_t sum = 0;

	while (size--)
		sum += *data++;
	return -sum;
}

static char *run_acpiexec(const char *aml_path, unsigned int region_fill,
			  const char *batch)
{
	char output_path[] = "/tmp/starlabs-acpiexec-XXXXXX";
	char command[4096];
	char *output;
	struct stat stat;
	int fd = mkstemp(output_path);

	assert_true(fd >= 0);
	assert_int_equal(close(fd), 0);
	snprintf(command, sizeof(command), "acpiexec -di -fv %u -b '%s' %s >%s 2>&1",
		 region_fill, batch, aml_path, output_path);
	assert_int_equal(system(command), 0);
	fd = open(output_path, O_RDONLY);
	assert_true(fd >= 0);
	assert_int_equal(fstat(fd, &stat), 0);
	output = malloc(stat.st_size + 1);
	assert_non_null(output);
	assert_int_equal(read(fd, output, stat.st_size), stat.st_size);
	output[stat.st_size] = '\0';
	assert_int_equal(close(fd), 0);
	assert_int_equal(unlink(output_path), 0);
	return output;
}

static void assert_ec_integer(const char *output, const char *name, uint64_t expected)
{
	char evaluation[96];
	const char *integer;
	unsigned long long actual;
	int fields;

	snprintf(evaluation, sizeof(evaluation), "Evaluation of %s%s returned", EC_PATH, name);
	integer = strstr(output, evaluation);
	assert_non_null(integer);
	integer = strstr(integer, "[Integer] = ");
	assert_non_null(integer);
	fields = sscanf(integer, "[Integer] = %llx", &actual);
	assert_int_equal(fields, 1);
	assert_int_equal(actual, expected);
}

static void assert_clean_execution(const char *output)
{
	assert_null(strstr(output, "ACPI Error"));
	assert_null(strstr(output, "failed with status"));
}

static void generated_suspend_aml_preserves_snapshot_validity(void **state)
{
	char path[] = "/tmp/starlabs-suspend-aml-XXXXXX";
	char command[256];
	struct acpi_header *header = (void *)aml_buffer;
	char *output;
	int fd;

	memset(aml_buffer, 0, sizeof(aml_buffer));
	memcpy(header->signature, "SSDT", sizeof(header->signature));
	header->revision = 2;
	memcpy(header->oem_id, "COREBT", sizeof(header->oem_id));
	memcpy(header->oem_table_id, "STARSUSP", sizeof(header->oem_table_id));
	memcpy(header->asl_compiler_id, "INTL", sizeof(header->asl_compiler_id));
	acpigen_set_current((void *)(header + 1));
	starlabs_write_efi_option_sleep_aml_for_test();
	header->length = acpigen_get_current() - (char *)header;
	header->checksum = checksum(aml_buffer, header->length);

	fd = mkstemp(path);
	assert_true(fd >= 0);
	assert_int_equal(write(fd, aml_buffer, header->length), header->length);
	assert_int_equal(close(fd), 0);

	snprintf(command, sizeof(command), "iasl -d %s >/dev/null 2>&1", path);
	assert_int_equal(system(command), 0);

	/* A successfully read zero is a valid setting and all four saves run. */
	output = run_acpiexec(path, 0,
		"set N " EC_PATH "TPLE 0; set N " EC_PATH "FLKE 1; "
		"set N " EC_PATH "KLSE DD; set N " EC_PATH "KLBE AA; "
		"execute " EC_PATH "RPTS 3; " SNAPSHOT_EVALUATIONS);
	assert_clean_execution(output);
	assert_ec_integer(output, "EOBV", 0xf);
	assert_ec_integer(output, "EOBF", 0);
	assert_ec_integer(output, "EOCT", 4);
	assert_ec_integer(output, "EBTP", 0);
	assert_ec_integer(output, "EBFL", 1);
	assert_ec_integer(output, "EBKS", 0xdd);
	assert_ec_integer(output, "EBKB", 0xaa);
	free(output);

	/* A failed read is neither persisted nor marked as a valid snapshot. */
	output = run_acpiexec(path, 0,
		"set N " EC_PATH "TPLE FFFFFFFF; set N " EC_PATH "FLKE 1; "
		"set N " EC_PATH "KLSE DD; set N " EC_PATH "KLBE AA; "
		"execute " EC_PATH "RPTS 3; " SNAPSHOT_EVALUATIONS);
	assert_clean_execution(output);
	assert_ec_integer(output, "EOBV", 0xe);
	assert_ec_integer(output, "EOBF", 1);
	assert_ec_integer(output, "EOCT", 3);
	free(output);

	/* Persistence failure retains each successfully captured live value. */
	output = run_acpiexec(path, 0,
		"set N " EC_PATH "EOTR 1; set N " EC_PATH "TPLE 22; "
		"set N " EC_PATH "FLKE 1; set N " EC_PATH "KLSE DD; "
		"set N " EC_PATH "KLBE AA; execute " EC_PATH "RPTS 3; "
		SNAPSHOT_EVALUATIONS);
	assert_clean_execution(output);
	assert_ec_integer(output, "EOBV", 0xf);
	assert_ec_integer(output, "EOBF", 1);
	assert_ec_integer(output, "EOCT", 4);
	assert_ec_integer(output, "EBTP", 0x22);
	assert_ec_integer(output, "EBFL", 1);
	assert_ec_integer(output, "EBKS", 0xdd);
	assert_ec_integer(output, "EBKB", 0xaa);
	free(output);

	/* Resume restores valid snapshots but leaves a failed field untouched. */
	output = run_acpiexec(path, 0,
		"set N " EC_PATH "EOTR 1; set N " EC_PATH "TPLE FFFFFFFF; "
		"set N " EC_PATH "FLKE 1; set N " EC_PATH "KLSE DD; "
		"set N " EC_PATH "KLBE AA; execute " EC_PATH "RPTS 3; "
		"set N " EC_PATH "TPLE 22; set N " EC_PATH "FLKE 0; "
		"set N " EC_PATH "KLSE 0; set N " EC_PATH "KLBE 0; "
		"execute " EC_PATH "RWAK 3; " LIVE_EVALUATIONS);
	assert_clean_execution(output);
	assert_ec_integer(output, "EOBV", 0);
	assert_ec_integer(output, "TPLE", 0x22);
	assert_ec_integer(output, "FLKE", 1);
	assert_ec_integer(output, "KLSE", 0xdd);
	assert_ec_integer(output, "KLBE", 0xaa);
	free(output);

	/* A new sleep cycle clears validity, so stale snapshots cannot restore. */
	output = run_acpiexec(path, 0,
		"set N " EC_PATH "EOTR 1; set N " EC_PATH "TPLE 0; "
		"set N " EC_PATH "FLKE 1; set N " EC_PATH "KLSE DD; "
		"set N " EC_PATH "KLBE AA; execute " EC_PATH "RPTS 3; "
		"set N " EC_PATH "TPLE FFFFFFFF; set N " EC_PATH "FLKE FFFFFFFF; "
		"set N " EC_PATH "KLSE FFFFFFFF; set N " EC_PATH "KLBE FFFFFFFF; "
		"execute " EC_PATH "RPTS 3; set N " EC_PATH "TPLE 22; "
		"set N " EC_PATH "FLKE 0; set N " EC_PATH "KLSE 0; "
		"set N " EC_PATH "KLBE 0; execute " EC_PATH "RWAK 3; "
		LIVE_EVALUATIONS);
	assert_clean_execution(output);
	assert_ec_integer(output, "EOBV", 0);
	assert_ec_integer(output, "TPLE", 0x22);
	assert_ec_integer(output, "FLKE", 0);
	assert_ec_integer(output, "KLSE", 0);
	assert_ec_integer(output, "KLBE", 0);
	free(output);

	assert_int_equal(unlink(path), 0);
	snprintf(command, sizeof(command), "%s.dsl", path);
	unlink(command);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(generated_suspend_aml_preserves_snapshot_validity),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
