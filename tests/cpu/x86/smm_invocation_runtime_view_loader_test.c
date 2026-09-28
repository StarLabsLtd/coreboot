/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define CONFIG_MAX_CPUS 64U

struct test_loader_params {
	size_t num_cpus;
	size_t cpu_save_state_size;
	size_t num_concurrent_save_states;
};

struct test_runtime {
	uintptr_t save_state_top[CONFIG_MAX_CPUS];
};

static int loader_guard(struct test_loader_params *params)
{
#include "runtime-view-loader-guard-fragment.h"
	return 0;
}

static void clear_unused(struct test_runtime *mod_params,
	const struct test_loader_params *loader_params)
{
#include "runtime-view-loader-clear-fragment.h"
}

int main(void)
{
	struct test_loader_params params = {
		.num_cpus = 4,
		.cpu_save_state_size = 1024,
		.num_concurrent_save_states = 4,
	};
	struct test_runtime runtime;

	assert(loader_guard(&params) == 0);
	assert(loader_guard(NULL) == -1);
	params.num_cpus = 0;
	params.num_concurrent_save_states = 0;
	assert(loader_guard(&params) == -1);
	params.num_cpus = 4;
	params.num_concurrent_save_states = 3;
	assert(loader_guard(&params) == -1);
	params.num_cpus = CONFIG_MAX_CPUS + 1U;
	params.num_concurrent_save_states = params.num_cpus;
	assert(loader_guard(&params) == -1);
	params.num_cpus = 4;
	params.num_concurrent_save_states = 4;
	params.cpu_save_state_size = 0;
	assert(loader_guard(&params) == -1);
#if SIZE_MAX > UINT32_MAX
	params.cpu_save_state_size = (size_t)UINT32_MAX + 1U;
	assert(loader_guard(&params) == -1);
#endif
	params.cpu_save_state_size = 1024;
	for (size_t cpu = 0; cpu < CONFIG_MAX_CPUS; cpu++)
		runtime.save_state_top[cpu] = cpu + 1U;
	clear_unused(&runtime, &params);
	for (size_t cpu = 0; cpu < params.num_cpus; cpu++)
		assert(runtime.save_state_top[cpu] == cpu + 1U);
	for (size_t cpu = params.num_cpus; cpu < CONFIG_MAX_CPUS; cpu++)
		assert(runtime.save_state_top[cpu] == 0);
	return 0;
}
