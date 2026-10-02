#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/smm-dma-owned.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_VIEW 1' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_COMPOSITION 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_INSTANCE 1' \
	'#define CONFIG_SMM_INVOCATION_TOPOLOGY 1' \
	'#define CONFIG_SMM_INVOCATION_EVIDENCE 1' \
	'#define CONFIG_Q35_SMM_CAPSULE_BROKER_BUFFERS 1' \
	'#define CONFIG_CAPSULE_BROKER_FIXED_BUFFERS 1' \
	'#define CONFIG_STM 0' \
	'#define TEST_RUNTIME_VIEW_RESERVED_SIZE 0' > "$temporary/include/config.h"

awk '
	/save_state\[TEST_CPUS\]\[TEST_ALLOCATION\];/ {
		print; print "\tconst struct smm_dma_owned_memory *dma_output;"; count++; next
	}
	{ print }
	END { if (count != 1) exit 1 }
' "$root/tests/cpu/x86/smm_invocation_runtime_view_test.c" \
	> "$temporary/owned-runtime-fixture.h"

extract()
{
	awk '
		/^struct smm_invocation_runtime_view \{/ { copy = 1 }
		copy && /^#endif$/ { ends++; if (ends == 1) next; print; exit }
		copy { print }
	' "$1" > "$temporary/runtime-view-fragment.h"
	awk '
		/^static bool spans_overlap\(/ { copy = 1 }
		copy { print }
		copy && /^}$/ { copy = 0; exit }
	' "$1" > "$temporary/owned-memory-fragment.h"
	awk '
		/^bool smm_get_dma_owned_memory\(/ { copy = 1 }
		copy { print }
		copy && /^}$/ { copy = 0; exit }
	' "$1" >> "$temporary/owned-memory-fragment.h"
	awk '
		/^void smm_get_capsule_broker_buffers\(/ { copy = 1 }
		copy { print }
		copy && /^}$/ { copy = 0; exit }
	' "$1" >> "$temporary/owned-memory-fragment.h"
	grep -q '^bool smm_get_dma_owned_memory' "$temporary/owned-memory-fragment.h"
	grep -q '^void smm_get_capsule_broker_buffers' "$temporary/owned-memory-fragment.h"
}

compile()
{
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -Wvla \
		-fno-pie -no-pie -D__COREBOOT__ -D__TEST__ -O"$optimization" \
		-g -fsanitize=address,undefined -fno-omit-frame-pointer \
		-I"$temporary" -I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		"$root/tests/cpu/x86/smm_dma_owned_memory_test.c" \
		"$root/src/cpu/x86/smm/save_state_geometry.c" \
		"$root/src/cpu/x86/smm_invocation_loader_composition_gate.c" -o "$temporary/test"
}

export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
source="$root/src/cpu/x86/smm/smm_module_handler.c"
for optimization in 0 2; do
	extract "$source"
	compile
	"$temporary/test"
	printf 'PASS owned DMA snapshot and hostile outputs O%s\n' "$optimization"
	for mutant in output geometry; do
		awk -v mutant="$mutant" '
			mutant == "output" &&
			$0 == "\tif (!runtime_output_valid((uintptr_t)memory, sizeof(*memory)," {
				if (getline <= 0 || $0 != "\t\t_Alignof(*memory)) ||") exit 1
				print "\tif (false ||"; count++; next
			}
			mutant == "geometry" &&
			($0 == "\t    !runtime_geometry_snapshot((uintptr_t)memory, sizeof(*memory), &geometry) ||" ||
			 $0 == "\t    !runtime_geometry_unchanged(&geometry, (uintptr_t)memory, sizeof(*memory)) ||") {
				print "\t    ((void)&geometry, false) ||"; count++; next
			}
			{ print }
			END { if (count != (mutant == "output" ? 1 : 2)) exit 1 }
		' "$source" > "$temporary/mutant.c"
		awk -v mutant="$mutant" '
			mutant == "output" && $0 == "\tif (false ||" {
				print "\tif (!runtime_output_valid((uintptr_t)memory, sizeof(*memory),"
				print "\t\t_Alignof(*memory)) ||"; count++; next
			}
			mutant == "geometry" && $0 == "\t    ((void)&geometry, false) ||" {
				count++
				if (count == 1)
					print "\t    !runtime_geometry_snapshot((uintptr_t)memory, sizeof(*memory), &geometry) ||"
				else
					print "\t    !runtime_geometry_unchanged(&geometry, (uintptr_t)memory, sizeof(*memory)) ||"
				next
			}
			{ print }
			END { if (count != (mutant == "output" ? 1 : 2)) exit 1 }
		' "$temporary/mutant.c" > "$temporary/restored.c"
		cmp "$source" "$temporary/restored.c"
		extract "$temporary/mutant.c"
		compile
		status=0
		(ulimit -c 0; "$temporary/test") > "$temporary/mutant.log" 2>&1 || status=$?
		test "$status" -eq 134
		if test "$mutant" = output; then
			grep -Fxq 'OWNED_DMA_ASSERT: !smm_get_dma_owned_memory((void *)&test_memory.runtime)' \
				"$temporary/mutant.log"
		else
			grep -Fxq 'OWNED_DMA_ASSERT: !smm_get_dma_owned_memory((void *)&test_memory.save_state[0][0])' \
				"$temporary/mutant.log"
		fi
		! grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
			"$temporary/mutant.log"
		printf 'PASS owned DMA %s guard mutant O%s targeted134/noSAN\n' \
			"$mutant" "$optimization"
	done
done
