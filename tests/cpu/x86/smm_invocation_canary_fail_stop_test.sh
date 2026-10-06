#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d "$root/../.canary-fail-stop-test.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
handler="$root/src/cpu/x86/smm/smm_module_handler.c"
kconfig="$root/src/cpu/x86/Kconfig"

mkdir -p "$temporary/include"
# HOST-modeled options, using the genuine configuration macro.
while IFS='=' read -r config_key config_value; do
	printf '#define %s %s\n' "$config_key" "$config_value"
done > "$temporary/include/config.h" <<'EOF'
CONFIG_SMM_INVOCATION_STACK_CANARY_FAIL_STOP=1
CONFIG_DEBUG_SMI=0
EOF

provider_is_first_statement()
{
	awk '
		/^\tif \(actual_canary != expected_canary\) \{/ { mismatch = 1; next }
		mismatch && /^#if CONFIG\(SMM_INVOCATION_STACK_CANARY_FAIL_STOP\)$/ {
			selected = 1
			next
		}
		selected && /^[[:space:]]*$/ { next }
		selected {
			gsub(/^[[:space:]]+|[[:space:]]+$/, "")
			exit $0 != "smm_invocation_platform_fail_stop();"
		}
		END { if (!selected) exit 1 }
	' "$1"
}

extract_fragment()
{
	source=$1
	output=$2
	awk '
		/^\tif \(actual_canary != expected_canary\) \{/ { copy = 1 }
		copy { print }
		copy && /^\t}/ { exit }
	' "$source" > "$output"
	grep -q 'smm_invocation_platform_fail_stop();' "$output" || return 1
}

run_runtime()
{
	source=$1
	name=$2
	extract_fragment "$source" "$temporary/canary-fragment.h"
	for optimization in 0 2; do
		${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -O"$optimization" \
			-include "$root/src/include/kconfig.h" -I"$temporary/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$temporary" \
			"$root/tests/cpu/x86/smm_invocation_canary_fail_stop_runtime_test.c" \
			-o "$temporary/$name-o$optimization"
		"$temporary/$name-o$optimization" || return 1
	done
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -O1 \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-include "$root/src/include/kconfig.h" -I"$temporary/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$temporary" \
		"$root/tests/cpu/x86/smm_invocation_canary_fail_stop_runtime_test.c" \
		-o "$temporary/$name-sanitized"
	ASAN_OPTIONS=detect_leaks=0 "$temporary/$name-sanitized" || return 1
	if ${CC:-cc} -m32 -std=gnu11 -Wall -Wextra -Werror -O2 \
		-include "$root/src/include/kconfig.h" -I"$temporary/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$temporary" \
		"$root/tests/cpu/x86/smm_invocation_canary_fail_stop_runtime_test.c" \
		-o "$temporary/$name-i686" >"$temporary/$name-i686.log" 2>&1; then
		"$temporary/$name-i686" || return 1
		! nm -u "$temporary/$name-i686" | grep -q '__atomic_' || return 1
	fi
}

# The capability stays hidden, default-off and requires the strong provider.
awk '
	/^config SMM_INVOCATION_STACK_CANARY_FAIL_STOP$/ { copy = 1 }
	copy && seen && /^config / { exit }
	copy { print; seen = 1 }
' "$kconfig" > "$temporary/kconfig.block"
grep -q '^[[:space:]]*default n$' "$temporary/kconfig.block"
grep -q '^[[:space:]]*depends on SMM_INVOCATION_FAIL_STOP_PLATFORM$' \
	"$temporary/kconfig.block"
! grep -q '^[[:space:]]*bool "' "$temporary/kconfig.block"
! rg -q '^[[:space:]]*(select|imply)[[:space:]]+SMM_INVOCATION_STACK_CANARY_FAIL_STOP([[:space:]]|$)' \
	"$root/src"

# The post-handler canary block has exactly one terminal provider call.
test "$(rg -n 'smm_invocation_fail_stop.h' "$handler" | wc -l)" -eq 1
extract_fragment "$handler" "$temporary/canary-fragment.h"
test "$(rg -n 'smm_invocation_platform_fail_stop\(\);' \
	"$temporary/canary-fragment.h" | wc -l)" -eq 1
provider_is_first_statement "$handler"
run_runtime "$handler" base

# Removal, replacement by the legacy local die, mismatch bypass and any action
# before the terminal provider must die.
sed 's/smm_invocation_platform_fail_stop();/return;/' "$handler" \
	> "$temporary/removal.c"
if run_runtime "$temporary/removal.c" removal >/dev/null 2>&1; then
	printf '%s\n' 'canary fail-stop removal mutant survived' >&2
	exit 1
fi
sed 's/smm_invocation_platform_fail_stop();/die("local stop");/' "$handler" \
	> "$temporary/replacement.c"
if run_runtime "$temporary/replacement.c" replacement >/dev/null 2>&1; then
	printf '%s\n' 'canary local-die replacement mutant survived' >&2
	exit 1
fi
sed 's/if (actual_canary != expected_canary)/if (actual_canary == expected_canary)/' \
	"$handler" > "$temporary/bypass.c"
if run_runtime "$temporary/bypass.c" bypass >/dev/null 2>&1; then
	printf '%s\n' 'canary mismatch-bypass mutant survived' >&2
	exit 1
fi
extract_fragment "$handler" "$temporary/base-canary-fragment.h"
awk '
	{ print }
	/^\tif \(actual_canary != expected_canary\) \{/ { mismatch = 1 }
	mismatch && /^#if CONFIG\(SMM_INVOCATION_STACK_CANARY_FAIL_STOP\)$/ {
		print "\t\tprintk(BIOS_DEBUG, \"premature\\n\");"
		mismatch = 0
	}
' "$handler" > "$temporary/pre-provider-call.c"
extract_fragment "$temporary/pre-provider-call.c" \
	"$temporary/pre-provider-call-fragment.h"
if cmp -s "$temporary/base-canary-fragment.h" \
	"$temporary/pre-provider-call-fragment.h"; then
	printf '%s\n' 'pre-provider call mutant changed no canary fragment' >&2
	exit 1
fi
if provider_is_first_statement "$temporary/pre-provider-call.c"; then
	printf '%s\n' 'pre-provider call mutant survived' >&2
	exit 1
fi
awk '
	{ print }
	/^\tif \(actual_canary != expected_canary\) \{/ { mismatch = 1 }
	mismatch && /^#if CONFIG\(SMM_INVOCATION_STACK_CANARY_FAIL_STOP\)$/ {
		print "\t\treturn;"
		mismatch = 0
	}
' "$handler" > "$temporary/pre-provider-return.c"
extract_fragment "$temporary/pre-provider-return.c" \
	"$temporary/pre-provider-return-fragment.h"
if cmp -s "$temporary/base-canary-fragment.h" \
	"$temporary/pre-provider-return-fragment.h"; then
	printf '%s\n' 'pre-provider return mutant changed no canary fragment' >&2
	exit 1
fi
if provider_is_first_statement "$temporary/pre-provider-return.c"; then
	printf '%s\n' 'pre-provider return mutant survived' >&2
	exit 1
fi

printf '%s\n' 'SMM invocation stack-canary fail-stop runtime: PASS'
