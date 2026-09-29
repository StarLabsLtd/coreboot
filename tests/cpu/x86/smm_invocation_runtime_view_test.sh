#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d "$root/../.runtime-view-test.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"

extract_fragment()
{
	awk '
		/^struct smm_invocation_runtime_view \{/ { copy = 1 }
		copy && /^static int smi_obtain_lock/ { exit }
		copy && /^#if/ { depth++; print; next }
		copy && /^#endif$/ {
			if (depth) { depth--; print }
			next
		}
		copy { print }
	' "$1" > "$2"
	grep -q '^enum cb_err smm_invocation_runtime_view_get' "$2"
}

extract_fragment "$root/src/cpu/x86/smm/smm_module_handler.c" \
	"$temporary/runtime-view-fragment.h"

run_variant()
{
	stm=$1
	reserved=$2
	printf '%s\n' \
		'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
		'#define CONFIG_MAX_CPUS 64' \
		'#define CONFIG_SMM_INVOCATION_RUNTIME_VIEW 1' \
		'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
		'#define CONFIG_SMM_INVOCATION_LOADER_COMPOSITION 1' \
		'#define CONFIG_SMM_INVOCATION_LOADER_INSTANCE 1' \
		'#define CONFIG_SMM_INVOCATION_TOPOLOGY 1' \
		'#define CONFIG_SMM_INVOCATION_EVIDENCE 1' \
		'#define CONFIG_SMM_INVOCATION_AUXILIARY_CHANNELS 1' \
		'#define CONFIG_SMM_MODULE_STACK_SIZE 0x2000' \
		'#define CONFIG_STM 0' \
		"#define TEST_RUNTIME_VIEW_RESERVED_SIZE $reserved" \
		> "$temporary/include/config.h"
	includes="-I$temporary -I$temporary/include -I$root/src/include -I$root/src \
-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include \
-I$root/src/arch/x86/include"
	flags="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -Wvla \
-fno-pie -no-pie -D__COREBOOT__ -D__TEST__ \
-include $root/src/include/kconfig.h -include $root/src/include/rules.h \
-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h"
	for optimization in 0 2; do
		${CC:-cc} $flags -O$optimization $includes \
			"$root/tests/cpu/x86/smm_invocation_runtime_view_test.c" \
			"$root/src/cpu/x86/smm/save_state_geometry.c" \
			"$root/src/cpu/x86/smm_invocation_loader_composition_gate.c" \
			-o "$temporary/runtime-view-$stm-O$optimization"
		"$temporary/runtime-view-$stm-O$optimization"
	done
	${CC:-cc} $flags -O1 -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer $includes \
		"$root/tests/cpu/x86/smm_invocation_runtime_view_test.c" \
		"$root/src/cpu/x86/smm/save_state_geometry.c" \
		"$root/src/cpu/x86/smm_invocation_loader_composition_gate.c" \
		-o "$temporary/runtime-view-$stm-san"
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/runtime-view-$stm-san"
	if ${CC:-cc} $flags -O2 -m32 -march=i686 -ffreestanding \
		-fno-builtin -fstack-usage $includes -c \
		"$root/tests/cpu/x86/smm_invocation_runtime_view_test.c" \
		-o "$temporary/runtime-view-$stm-i686.o" \
		>"$temporary/i686-$stm.log" 2>&1; then
		! nm -u "$temporary/runtime-view-$stm-i686.o" | grep -q '__atomic_'
		for symbol in smm_invocation_runtime_view_get \
			smm_invocation_runtime_cpu_count \
			smm_invocation_runtime_save_state_span \
			smm_invocation_runtime_range_is_protected \
			smm_invocation_runtime_binding_get \
			smm_invocation_runtime_auxiliary_binding_get; do
			frame=$(awk -F '\t' -v symbol="$symbol" \
				'$1 ~ symbol "$" { print $2 }' \
				"$temporary/runtime-view-$stm-i686.su")
			test -n "$frame" && test "$frame" -le 192
		done
	fi
}

run_variant 0 0
run_variant 1 256

kill_mutant()
{
	name=$1
	expression=$2
	sed "$expression" "$root/src/cpu/x86/smm/smm_module_handler.c" \
		> "$temporary/$name.c"
	if cmp -s "$temporary/$name.c" \
		"$root/src/cpu/x86/smm/smm_module_handler.c"; then
		printf '%s\n' "runtime-view mutant did not change source: $name" >&2
		exit 1
	fi
	extract_fragment "$temporary/$name.c" "$temporary/runtime-view-fragment.h"
	if ${CC:-cc} $flags -O2 $includes \
		"$root/tests/cpu/x86/smm_invocation_runtime_view_test.c" \
		"$root/src/cpu/x86/smm/save_state_geometry.c" \
		"$root/src/cpu/x86/smm_invocation_loader_composition_gate.c" \
		-o "$temporary/$name" >/dev/null 2>&1 && \
		"$temporary/$name" >/dev/null 2>&1; then
		printf '%s\n' "runtime-view mutant survived: $name" >&2
		exit 1
	fi
}

kill_mutant reserve \
	's/RUNTIME_VIEW_RESERVED_SIZE/0U/g'
kill_mutant range \
	's/return runtime_range_valid(outer/return true || runtime_range_valid(outer/'
kill_mutant overlap \
	's/if (first <= second)/if (false \&\& first <= second)/'
kill_mutant topology \
	's/!runtime_topology_matches(value.runtime_cpus)/false/g'
kill_mutant composition \
	's/!runtime_composition_matches()/false/g'
kill_mutant top-recheck \
	's/top != __atomic_load_n/false \&\& top != __atomic_load_n/'
kill_mutant count-recheck \
	's/before->runtime_cpus == after.runtime_cpus/true/'
kill_mutant allocation-recheck \
	's/before->allocation_size == after.allocation_size/true/'
kill_mutant smram-recheck \
	's/before->smram_base == after.smram_base \&\&/true \&\&/; s/before->smram_size == after.smram_size/true/'
kill_mutant unused-top \
	's/if (__atomic_load_n(\&smm_runtime.save_state_top\[cpu\],/if (false \&\& __atomic_load_n(\&smm_runtime.save_state_top[cpu],/'

# Restore the production fragment for the source probes below.
extract_fragment "$root/src/cpu/x86/smm/smm_module_handler.c" \
	"$temporary/runtime-view-fragment.h"

# The public API is opaque and exposes no array or element pointer authority.
header="$root/src/include/cpu/x86/smm_invocation_runtime.h"
! grep -q 'struct smm_runtime' "$header"
! grep -q 'save_state_top' "$header"
! rg -q '(^|[^a-z_])(memcpy|memset|in[bwl]|out[bwl]|apm_control)\(' \
	"$root/src/cpu/x86/smm/smm_module_handler.c"
! rg -q '^[[:space:]]*(select|imply)[[:space:]]+SMM_INVOCATION_RUNTIME_VIEW' \
	"$root/src"

stage_flags="-std=gnu11 -Werror -D__COREBOOT__ -I$temporary/include \
-include $root/src/include/kconfig.h -I$root/src/include \
-I$root/src -I$root/src/commonlib/include \
-I$root/src/commonlib/bsd/include -I$root/src/arch/x86/include"

# A consumer cannot inspect or size the opaque handle.
printf '%s\n' \
	'#define __SMM__ 1' \
	'#include <cpu/x86/smm_invocation_runtime.h>' \
	'int reject(void) { return sizeof(struct smm_invocation_runtime_view); }' \
	> "$temporary/opaque-probe.c"
if ${CC:-cc} $stage_flags -c "$temporary/opaque-probe.c" \
	-o "$temporary/opaque-probe.o" >/dev/null 2>&1; then
	printf '%s\n' 'runtime view lost opacity' >&2
	exit 1
fi

# A concrete raw-top mutant makes the opacity probe compile and is detected.
mkdir -p "$temporary/mutant/cpu/x86"
sed 's/^struct smm_invocation_runtime_view;$/struct smm_invocation_runtime_view { const volatile uintptr_t *save_state_top; };/' \
	"$header" > \
	"$temporary/mutant/cpu/x86/smm_invocation_runtime.h"
grep -q 'save_state_top' \
	"$temporary/mutant/cpu/x86/smm_invocation_runtime.h"
${CC:-cc} -I"$temporary/mutant" $stage_flags -c \
	"$temporary/opaque-probe.c" -o "$temporary/opaque-mutant.o"

# The declarations exist in SMM/test only and are absent in ramstage.
printf '%s\n' \
	'#include <cpu/x86/smm_invocation_runtime.h>' \
	'enum cb_err (*probe)(const struct smm_invocation_runtime_view **) =' \
	'  smm_invocation_runtime_view_get;' > "$temporary/stage-probe.c"
${CC:-cc} $stage_flags -D__SMM__ -c "$temporary/stage-probe.c" \
	-o "$temporary/smm-probe.o"
if ${CC:-cc} $stage_flags -D__RAMSTAGE__ -c "$temporary/stage-probe.c" \
	-o "$temporary/ramstage-probe.o" >/dev/null 2>&1; then
	printf '%s\n' 'runtime view API leaked outside SMM' >&2
	exit 1
fi

# Required source guards must be mutation-sensitive.
production="$root/src/cpu/x86/smm/smm_module_handler.c"
for expression in \
	'allocation_size <= RUNTIME_VIEW_RESERVED_SIZE' \
	'!runtime_topology_matches(value.runtime_cpus)' \
	'!runtime_composition_matches()' \
	'runtime_range_contains(value.smram_base, value.smram_size, runtime,'; do
	grep -Fq "$expression" "$production"
done
grep -q 'params->num_cpus != params->num_concurrent_save_states' \
	"$root/src/cpu/x86/smm/smm_module_loader.c"

extract_loader()
{
	source=$1
	awk '
		/^[[:space:]]*if \((!params|0) \|\| (!params->num_cpus|0) \|\|/ { copy = 1 }
		copy { print }
		copy && /^[[:space:]]*return -1;/ { exit }
	' "$source" > "$temporary/runtime-view-loader-guard-fragment.h"
	awk '
		/for \(size_t i = (loader_params->num_cpus|CONFIG_MAX_CPUS);/ { copy = 1 }
		copy { print }
		copy && /mod_params->save_state_top\[i\] =/ { exit }
	' "$source" > "$temporary/runtime-view-loader-clear-fragment.h"
	grep -q 'return -1' \
		"$temporary/runtime-view-loader-guard-fragment.h"
	grep -q 'save_state_top' "$temporary/runtime-view-loader-clear-fragment.h"
}

compile_loader()
{
	output=$1
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-O2 -I"$temporary" \
		"$root/tests/cpu/x86/smm_invocation_runtime_view_loader_test.c" \
		-o "$output"
}

loader="$root/src/cpu/x86/smm/smm_module_loader.c"
extract_loader "$loader"
compile_loader "$temporary/loader"
"$temporary/loader"

kill_loader_mutant()
{
	name=$1
	expression=$2
	sed "$expression" "$loader" > "$temporary/loader-$name.c"
	if cmp -s "$loader" "$temporary/loader-$name.c"; then
		printf '%s\n' "loader mutant did not change source: $name" >&2
		exit 1
	fi
	extract_loader "$temporary/loader-$name.c"
	if ! compile_loader "$temporary/loader-$name" \
		>"$temporary/loader-$name.log" 2>&1; then
		printf '%s\n' "loader mutant did not compile: $name" >&2
		cat "$temporary/loader-$name.log" >&2
		exit 1
	fi
	if "$temporary/loader-$name" >/dev/null 2>&1; then
		printf '%s\n' "loader mutant survived: $name" >&2
		exit 1
	fi
}

kill_loader_mutant null-params \
	's/!params || !params->num_cpus/0 || !params->num_cpus/'
kill_loader_mutant zero-cpu \
	's/!params->num_cpus ||/0 ||/'
kill_loader_mutant concurrency \
	's/params->num_cpus != params->num_concurrent_save_states/0/'
kill_loader_mutant config-cpu-bound \
	's/params->num_cpus > CONFIG_MAX_CPUS/0/'
kill_loader_mutant zero-save-size \
	's/!params->cpu_save_state_size/0/'
kill_loader_mutant width-save-size \
	's/params->cpu_save_state_size > UINT32_MAX/0/'
kill_loader_mutant unused-top-zero \
	's/mod_params->save_state_top\[i\] = 0;/mod_params->save_state_top[i] = loader_params->num_cpus;/'

printf '%s\n' 'SMM invocation runtime view: PASS'
