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
	'#define CONFIG_SMM_INVOCATION_INTEL_ADAPTER_PROVIDER 1' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_VIEW 1' \
	'#define CONFIG_SMM_SAVE_STATE_GEOMETRY 1' \
	> "$temporary/include/config.h"

build()
{
	name=$1
	flags=$2
	provider=${3:-}
	provider_flag=
	if [ -n "$provider" ]; then
		provider_flag="-DPROVIDER_SOURCE=\"$provider\""
	fi
	# Deliberate normal flag splitting for this strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		$flags $provider_flag -pthread -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/soc/intel/common/block/include" \
		-I"$root/src/soc/intel/common/block/smm" \
		"$root/tests/cpu/x86/smm_invocation_adapter_provider_test.c" \
		"$root/src/cpu/x86/smm/save_state_geometry.c" \
		"$root/src/soc/intel/common/block/smm/invocation_adapter.c" \
		-o "$temporary/$name"
}

expect_dead_mutant()
{
	name=$1
	mutant=$2
	if cmp -s "$mutant" \
		"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c"; then
		printf 'unchanged mutant: %s\n' "$name" >&2
		exit 1
	fi
	build "$name" -O2 "$mutant"
	if "$temporary/$name" >/dev/null 2>&1; then
		printf 'surviving mutant: %s\n' "$name" >&2
		exit 1
	fi
}

for optimization in 0 2; do
	build "provider-O$optimization" "-O$optimization"
	"$temporary/provider-O$optimization"
done
build provider-sanitize \
	'-O2 -fsanitize=address,undefined -fno-omit-frame-pointer'
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
	"$temporary/provider-sanitize"
build provider-thread '-O1 -fsanitize=thread -fno-omit-frame-pointer'
TSAN_OPTIONS=halt_on_error=1 "$temporary/provider-thread"

mutant="$temporary/no-poison-scrub.c"
sed 's/sizeof(provider) - sizeof(provider.state));/0);/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-poison-scrub "$mutant"

mutant="$temporary/no-adapter-end.c"
sed 's/intel_smm_invocation_adapter_end(\&provider.adapter)/CB_SUCCESS/g' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-adapter-end "$mutant"

mutant="$temporary/no-ownership-cas.c"
sed 's/if (!__atomic_compare_exchange_n/if (false \&\& !__atomic_compare_exchange_n/g' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-ownership-cas "$mutant"

mutant="$temporary/no-busy-retry.c"
sed 's/return SMM_INVOCATION_TRY_RETRY;/return SMM_INVOCATION_TRY_ERROR;/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-busy-retry "$mutant"

mutant="$temporary/no-canonical-identity.c"
sed 's/if (native_ops != \&em64t100_smm_ops \&\& native_ops != \&em64t101_smm_ops)/if (!native_ops)/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-canonical-identity "$mutant"

mutant="$temporary/no-revision-table.c"
sed -e '/#include "invocation_adapter_internal.h"/a static const uint32_t mutant_revision_table[] = { 0x30101U, SMM_REV_INVALID };' \
	-e '/revision_table = PROVIDER_REVISION_TABLE(native_ops);/a \	if (!revision_table) revision_table = mutant_revision_table;' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-revision-table "$mutant"

mutant="$temporary/no-revision-terminator.c"
sed 's/revision_table\[1\] != SMM_REV_INVALID/false/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-revision-terminator "$mutant"

mutant="$temporary/no-supported-revision.c"
sed -e 's/intel_smm_invocation_adapter_revision_supported/mutant_revision_supported/' \
	-e '/#include "invocation_adapter_internal.h"/a static bool mutant_revision_supported(uint32_t revision) { (void)revision; return true; }' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-supported-revision "$mutant"

mutant="$temporary/no-runtime-view-error.c"
sed 's/smm_invocation_runtime_view_get(\&view) != CB_SUCCESS/(smm_invocation_runtime_view_get(\&view) != CB_SUCCESS \&\& false)/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-runtime-view-error "$mutant"

mutant="$temporary/no-runtime-count-error.c"
sed 's/smm_invocation_runtime_cpu_count(view, \&active_cpus) != CB_SUCCESS/(smm_invocation_runtime_cpu_count(view, \&active_cpus) != CB_SUCCESS \&\& false)/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-runtime-count-error "$mutant"

mutant="$temporary/no-descriptor-recheck.c"
sed -e 's/memcmp(\&sealed_ops,/mutant_memcmp(\&sealed_ops,/g' \
	-e '/#include "invocation_adapter_internal.h"/a static int mutant_memcmp(const void *a, const void *b, size_t n) { (void)a; (void)b; (void)n; return 0; }' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-descriptor-recheck "$mutant"

mutant="$temporary/no-native-output-alias.c"
sed -e 's/intel_smm_invocation_adapter_range_disjoint/mutant_range_disjoint/g' \
	-e '/#include "invocation_adapter_internal.h"/a static bool mutant_range_disjoint(const struct intel_smm_invocation_adapter *a, const void *r, size_t n) { (void)a; (void)r; (void)n; return true; }' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-native-output-alias "$mutant"

mutant="$temporary/no-generation-exhaustion.c"
sed -e 's/provider.last_generation == UINT64_MAX/false/' \
	-e 's/if (!next)/if (false)/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-generation-exhaustion "$mutant"

mutant="$temporary/no-exact-retire.c"
sed 's/provider.active_generation != generation/false/' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" > "$mutant"
expect_dead_mutant no-exact-retire "$mutant"

${CC:-cc} -m32 -march=i686 -std=gnu11 -Wall -Werror -Wshadow \
	-ffreestanding -fno-builtin -D__TEST__ -D__COREBOOT__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" \
	-I"$root/src/soc/intel/common/block/include" -c \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" \
	-o "$temporary/provider-i686.o"
if nm -u "$temporary/provider-i686.o" | rg -q '__atomic|__sync'; then
	printf '%s\n' 'i686 provider gained an out-of-line atomic dependency' >&2
	exit 1
fi

${CC:-cc} -std=gnu11 -O2 -fstack-usage -fcallgraph-info=su -save-temps=obj \
	-D__TEST__ -D__COREBOOT__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" \
	-I"$root/src/soc/intel/common/block/include" \
	-I"$root/src/soc/intel/common/block/smm" -c \
	"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" \
	-o "$temporary/provider-stack.o"
for function_limit in \
	intel_smm_invocation_adapter_provider_provision:128 \
	intel_smm_invocation_adapter_provider_arm:80 \
	intel_smm_invocation_adapter_provider_retire:16; do
	symbol=${function_limit%:*}
	limit=${function_limit#*:}
	usage=$(awk -v symbol="$symbol" '$0 ~ symbol { print $(NF-1) }' \
		"$temporary/provider-stack.su")
	case "$usage" in ''|*[!0-9]*) exit 1 ;; esac
	[ "$usage" -le "$limit" ]
done
printf '%s\n' 'Intel SMM invocation adapter provider tests passed'
