/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <smmstore.h>
#include <stdint.h>

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static void ordinary_commands_are_unchanged(void)
{
	const uint8_t commands[] = {
		SMMSTORE_CMD_RAW_READ,
		SMMSTORE_CMD_RAW_WRITE,
		SMMSTORE_CMD_RAW_CLEAR,
	};

	for (size_t i = 0; i < ARRAY_SIZE(commands); i++) {
		uint8_t command = commands[i];

		assert(smmstore_preprocess_cmd(&command,
			(void *)(uintptr_t)1) == 0);
		assert(command == commands[i]);
	}
}

int main(void)
{
	uint8_t command;

	ordinary_commands_are_unchanged();

	command = SMMSTORE_CMD_USE_FULL_FLASH;
#if CONFIG(SMMSTORE_FULL_FLASH_ACCESS)
	assert(smmstore_preprocess_cmd(&command, (void *)(uintptr_t)1) == 1);
	assert(command == SMMSTORE_CMD_USE_FULL_FLASH);

	command = SMMSTORE_CMD_RAW_WRITE | SMMSTORE_CMD_USE_FULL_FLASH;
	assert(smmstore_preprocess_cmd(&command, (void *)(uintptr_t)1) == 0);
	assert(command == SMMSTORE_CMD_RAW_WRITE);
#else
	assert(smmstore_preprocess_cmd(&command, (void *)(uintptr_t)1) == 0);
	assert(command == SMMSTORE_CMD_USE_FULL_FLASH);

	command = SMMSTORE_CMD_RAW_WRITE | SMMSTORE_CMD_USE_FULL_FLASH;
	assert(smmstore_preprocess_cmd(&command, (void *)(uintptr_t)1) == 0);
	assert(command ==
		(SMMSTORE_CMD_RAW_WRITE | SMMSTORE_CMD_USE_FULL_FLASH));
#endif

	return 0;
}
