#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_INTEL_ADAPTER 1' \
	'#define CONFIG_SMM_SAVE_STATE_GEOMETRY 1' \
	> "$temporary/include/config.h"

build()
{
	name=$1
	flags=$2
	adapter=${3:-$root/src/soc/intel/common/block/smm/invocation_adapter.c}
	# Deliberate normal flag splitting for this strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		$flags -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/cpu/x86/smm_invocation_adapter_spans_test.c" \
		"$root/src/cpu/x86/smm/save_state_geometry.c" "$adapter" \
		-o "$temporary/$name"
}

expect_dead_mutant()
{
	name=$1
	mutant=$2
	flags=${3:--O2}
	# A mutant that does not compile is invalid evidence.
	if cmp -s "$mutant" \
		"$root/src/soc/intel/common/block/smm/invocation_adapter.c"; then
		printf 'unchanged mutant: %s\n' "$name" >&2
		exit 1
	fi
	build "$name" "$flags" "$mutant"
	if "$temporary/$name" >/dev/null 2>&1; then
		printf 'surviving mutant: %s\n' "$name" >&2
		exit 1
	fi
}

for optimization in 0 2; do
	build "spans-O$optimization" "-O$optimization"
	"$temporary/spans-O$optimization"
done
build spans-sanitize \
	'-O2 -fsanitize=address,undefined -fno-omit-frame-pointer'
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
	"$temporary/spans-sanitize"

${CC:-cc} -std=gnu11 -O2 -fstack-usage -fcallgraph-info=su \
	-save-temps=obj -D__COREBOOT__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" -c \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" \
	-o "$temporary/adapter.o"
span_stack=$(awk \
	'$0 ~ /intel_smm_invocation_adapter_init_spans/ { print $(NF-1) }' \
	"$temporary/adapter.su")
if [ -z "$span_stack" ] || [ "$span_stack" -gt 160 ]; then
	printf 'native-span stack bound failed: root=%s\n' \
		"${span_stack:-missing}" >&2
	exit 1
fi
# The sole indirect target is the bounded borrower supplied by PR263. It is
# deliberately not retained; every direct helper in this root remains bounded.
indirect_edges=$(awk '/^edge:/ &&
	/sourcename: "intel_smm_invocation_adapter_init_spans"/ &&
	/targetname: "__indirect_call"/ { count++ }
	END { print count + 0 }' "$temporary/adapter.ci")
if [ "$indirect_edges" -lt 1 ]; then
	printf '%s\n' 'native-span rooted callgraph proof failed' >&2
	exit 1
fi
if sed -n '/intel_smm_invocation_adapter_init_spans(/,/^}/p' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" |
	rg -q '\[[^]]*SMM_INVOCATION_EVIDENCE_MAX_CPUS[^]]*\]|\[[^]]*CONFIG_MAX_CPUS[^]]*\]'; then
	printf '%s\n' 'native-span initializer regained a CPU-sized local array' >&2
	exit 1
fi
if sed -n '/intel_smm_invocation_adapter_init_spans(/,/^}/p' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" |
	rg -q 'static |span_for_cpu[[:space:]]*=|context[[:space:]]*='; then
	printf '%s\n' 'native-span initializer retained borrower state' >&2
	exit 1
fi

${CC:-cc} -m32 -march=i686 -std=gnu11 -Wall -Werror -Wshadow \
	-ffreestanding -fno-builtin -D__COREBOOT__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" -c \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" \
	-o "$temporary/adapter-i686.o"
if nm -u "$temporary/adapter-i686.o" | rg -q '__atomic|__sync'; then
	printf '%s\n' 'i686 adapter gained an out-of-line atomic dependency' >&2
	exit 1
fi

mutant="$temporary/no-exact-size.c"
sed 's/span.size != expected_size ||/(span.size != expected_size \&\& false) ||/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant no-exact-size-mutant "$mutant"

mutant="$temporary/end-overflow.c"
sed 's/UINTPTR_MAX - (size - 1U)/UINTPTR_MAX - size/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant end-overflow-mutant "$mutant"

mutant="$temporary/revision-layout.c"
sed 's/ADAPTER_REVISION_SIZE(EM64T100_REVISION,/ADAPTER_REVISION_SIZE(EM64T101_REVISION,/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant revision-layout-mutant "$mutant"

mutant="$temporary/no-revision.c"
sed '/static size_t revision_size/,/^}/{s/return 0;/return sizeof(em64t101_smm_state_save_area_t);/}' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant no-revision-mutant "$mutant"

mutant="$temporary/no-count-bound.c"
sed '/intel_smm_invocation_adapter_init_spans(/,/^}/{s/active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS/(active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS \&\& false)/}' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant no-count-bound-mutant "$mutant" \
	'-O2 -fsanitize=address,undefined -fno-omit-frame-pointer'

mutant="$temporary/no-zero-destination.c"
sed '/intel_smm_invocation_adapter_init_spans(/,/^}/{s/if (adapter_bytes\[index\])/if (false \&\& adapter_bytes[index])/}' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant no-zero-destination-mutant "$mutant"

mutant="$temporary/no-pair-overlap.c"
sed '/if (ranges_overlap((const void \*)span.base, span.size,/s/if (/if (false \&\& /' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant no-pair-overlap-mutant "$mutant"

mutant="$temporary/no-adapter-context-alias.c"
sed '/ranges_overlap(adapter, sizeof(\*adapter), context, context_size)/s/ranges_overlap(adapter, sizeof(\*adapter), context, context_size)/(false \&\& ranges_overlap(adapter, sizeof(*adapter), context, context_size))/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant no-adapter-context-alias-mutant "$mutant"

mutant="$temporary/no-adapter-state-alias.c"
perl -0pe 's/ranges_overlap\(adapter, sizeof\(\*adapter\),\n\s+\(const void \*\)span.base, span.size\) \|\|/(ranges_overlap(adapter, sizeof(*adapter),\n\t\t\t(const void *)span.base, span.size) \&\& false) ||/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant no-adapter-state-alias-mutant "$mutant"

mutant="$temporary/no-output-state-alias.c"
perl -0pe 's/ranges_overlap\(\&span, sizeof\(span\),\n\s+\(const void \*\)span.base, span.size\) \|\|/(ranges_overlap(\&span, sizeof(span),\n\t\t\t(const void *)span.base, span.size) \&\& false) ||/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant no-output-state-alias-mutant "$mutant"

mutant="$temporary/no-context-state-alias.c"
perl -0pe 's/ranges_overlap\(context, context_size,\n\s+\(const void \*\)span.base, span.size\)\)/(ranges_overlap(context, context_size,\n\t\t\t(const void *)span.base, span.size) \&\& false))/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant no-context-state-alias-mutant "$mutant"

mutant="$temporary/no-callback-failure.c"
sed '/intel_smm_invocation_adapter_init_spans(/,/^fail:/{s/span_for_cpu(context, cpu, \&span) != CB_SUCCESS ||/(span_for_cpu(context, cpu, \&span) != CB_SUCCESS \&\& false) ||/}' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant no-callback-failure-mutant "$mutant"

mutant="$temporary/no-reentrancy-marker.c"
sed '/intel_smm_invocation_adapter_init_spans(/,/^fail:/{s/adapter->reserved = UINT32_MAX;/adapter->reserved = 0;/}' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant no-reentrancy-marker-mutant "$mutant"

mutant="$temporary/no-failure-scrub.c"
sed '/^fail:/,/return CB_ERR;/{s/memset(adapter, 0, sizeof(\*adapter));/(void)adapter;/}' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
expect_dead_mutant no-failure-scrub-mutant "$mutant"

git -C "$root" diff --check
printf '%s\n' 'SMM invocation adapter native-span tests passed'
