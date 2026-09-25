#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0\n' > "$temporary/include/config.h"

compile_and_run()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ "$@" \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$root/src/lib" \
		-I"$temporary/include" \
		"$root/tests/lib/payload_mm_authvar_mor_clear_executor_test.c" \
		"$root/src/lib/payload_mm_authvar_mor_clear_executor.c" \
		"$root/src/lib/payload_mm_authvar_mor_clear_plan.c" \
		"$root/src/lib/payload_mm_authvar_mor_clear.c" \
		"$root/src/lib/payload_mm_authvar_mor_grant.c" -o "$temporary/$name"
	for case_name in success failures hostile objects windows; do
		"$temporary/$name" "$case_name"
	done
}

compile_and_run o0 -O0
compile_and_run o2 -O2
compile_and_run asan -O1 -fsanitize=address -fno-omit-frame-pointer
compile_and_run ubsan -O1 -fsanitize=undefined -fno-omit-frame-pointer

mutant_test()
{
	name=$1
	expression=$2
	mutant="$temporary/$name.c"
	sed "$expression" "$root/src/lib/payload_mm_authvar_mor_clear_executor.c" > \
		"$mutant"
	if cmp -s "$root/src/lib/payload_mm_authvar_mor_clear_executor.c" "$mutant"; then
		echo "ERROR: $name mutation was not applied" >&2
		exit 1
	fi
	"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$root/src/lib" \
		-I"$temporary/include" \
		"$root/tests/lib/payload_mm_authvar_mor_clear_executor_test.c" "$mutant" \
		"$root/src/lib/payload_mm_authvar_mor_clear_plan.c" \
		"$root/src/lib/payload_mm_authvar_mor_clear.c" \
		"$root/src/lib/payload_mm_authvar_mor_grant.c" -o "$temporary/$name"
	if "$temporary/$name" success >/dev/null 2>&1 &&
	   "$temporary/$name" failures >/dev/null 2>&1 &&
	   "$temporary/$name" hostile >/dev/null 2>&1 &&
	   "$temporary/$name" objects >/dev/null 2>&1 &&
	   "$temporary/$name" windows >/dev/null 2>&1; then
		echo "ERROR: $name mutation survived" >&2
		exit 1
	fi
}

mutant_test plan-validation \
	's/payload_mm_authvar_mor_clear_plan_validate(&state->plan_snapshot)/CB_SUCCESS/'
mutant_test volatile-readback \
	's/if (((const volatile uint8_t \*)state->iteration.mapping)/if (false \&\& ((const volatile uint8_t *)state->iteration.mapping)/'
mutant_test cache-fence-order \
	's/call_fence(state, workspace, transcript, grant);/state->iteration.chunk_error == CB_SUCCESS ? call_fence(state, workspace, transcript, grant) : CB_SUCCESS;/'
mutant_test trusted-unmap-cleanup \
	's/if (map_active)/if (false \&\& map_active)/'
mutant_test output-recheck \
	's/bytes_zero(transcript, sizeof(\*transcript))/(bytes_zero(transcript, sizeof(*transcript)) || true)/'
mutant_test final-plan-recheck \
	's/!memcmp(&state->plan_snapshot, state->plan_input,/!memcmp(state->plan_input, state->plan_input,/'
mutant_test physical-window-boundary \
	's/return MIN(bounded_remaining, until_boundary);/return MIN(bounded_remaining, until_boundary | window_bytes);/'
mutant_test initial-inventory-validation \
	'0,/call_inventory(state,/ s/call_inventory(state, workspace, transcript, grant)/CB_SUCCESS/'
mutant_test late-inventory-validation \
	'/state->candidate.dma_after =/,/payload_mm_authvar_mor_clear_receipt_build/ s/call_inventory(state, workspace, transcript, grant)/CB_SUCCESS/'

"${CC:-cc}" -std=gnu11 -Os -m32 -Wall -Wextra -Werror -fno-builtin \
	-fstack-usage -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" -c \
	"$root/src/lib/payload_mm_authvar_mor_clear_executor.c" \
	-o "$temporary/executor-stack.o"
"${CC:-cc}" -std=gnu11 -Os -m32 -Wall -Wextra -Werror -fno-builtin \
	-fstack-usage -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" -c "$root/src/lib/payload_mm_authvar_mor_clear.c" \
	-o "$temporary/clear-stack.o"
"${CC:-cc}" -std=gnu11 -Os -m32 -Wall -Wextra -Werror -fno-builtin \
	-fstack-usage -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" -c "$root/src/lib/payload_mm_authvar_mor_grant.c" \
	-o "$temporary/grant-stack.o"

"${CC:-cc}" -std=gnu11 -Os -m32 -Wall -Wextra -Werror -fno-builtin \
	-fstack-usage -D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" -c "$root/src/lib/payload_mm_authvar_mor_linear.c" \
	-o "$temporary/linear-stack.o"
"${CC:-cc}" -std=gnu11 -Os -m32 -Wall -Wextra -Werror -fno-builtin \
	-fstack-usage -D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -D__ARCH_x86_32__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" -c "$root/src/lib/payload_mm_authvar_mor_clear_x86.c" \
	-o "$temporary/x86-stack.o"

stack_total=$(awk -F '\t' '
	$1 ~ /:payload_mm_authvar_mor_linear_after_bootmem$/ { linear = $2 + 0 }
	$1 ~ /:payload_mm_authvar_mor_clear_execute$/ { executor = $2 + 0 }
	$1 ~ /:payload_mm_authvar_mor_clear_receipt_build_owned$/ { receipt = $2 + 0 }
	$1 ~ /:payload_mm_authvar_mor_grant_validate$/ { grant = $2 + 0 }
	$1 ~ /:map_window$/ || $1 ~ /:cache_writeback_invalidate$/ ||
	$1 ~ /:fence$/ || $1 ~ /:unmap_window$/ {
		if ($2 + 0 > backend) backend = $2 + 0
	}
	$1 ~ /:prepare_with_ops$/ { prepare = $2 + 0 }
	END {
		if (!linear || !executor || !receipt || !grant || !backend || !prepare)
			exit 1
		clear = linear + executor + backend + receipt + grant
		resolve = linear + prepare
		if (resolve > clear) clear = resolve
		print clear
	}
' "$temporary/linear-stack.su" "$temporary/executor-stack.su" \
	"$temporary/clear-stack.su" "$temporary/grant-stack.su" \
	"$temporary/x86-stack.su")
if test "$stack_total" -gt 4096; then
	echo "ERROR: generic MOR clear path stack bound exceeded: $stack_total" >&2
	exit 1
fi
linear_stack=$(awk -F '\t' \
	'$1 ~ /:payload_mm_authvar_mor_linear_after_bootmem$/ { print $2 }' \
	"$temporary/linear-stack.su")
sed '/payload_mm_authvar_mor_linear_after_bootmem(/,/^{$/ {
	/^{$/a\
\tvolatile uint8_t stack_bound_mutant[4096];\
\t__asm__ __volatile__("" : : "r" (stack_bound_mutant) : "memory");
}' "$root/src/lib/payload_mm_authvar_mor_linear.c" > \
	"$temporary/linear-stack-mutant.c"
if cmp -s "$root/src/lib/payload_mm_authvar_mor_linear.c" \
	"$temporary/linear-stack-mutant.c"; then
	echo "ERROR: stack-bound mutation was not applied" >&2
	exit 1
fi
"${CC:-cc}" -std=gnu11 -Os -m32 -Wall -Wextra -Werror -fno-builtin \
	-fstack-usage -D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" -c "$temporary/linear-stack-mutant.c" \
	-o "$temporary/linear-stack-mutant.o"
mutant_linear_stack=$(awk -F '\t' \
	'$1 ~ /:payload_mm_authvar_mor_linear_after_bootmem$/ { print $2 }' \
	"$temporary/linear-stack-mutant.su")
if test -z "$linear_stack" || test -z "$mutant_linear_stack" ||
	test "$((stack_total - linear_stack + mutant_linear_stack))" -le 4096; then
	echo "ERROR: stack-bound mutant was not rejected" >&2
	exit 1
fi

mkdir -p "$temporary/config" "$temporary/build"
cat > "$temporary/config/.config" <<'EOF'
CONFIG_VENDOR_EMULATION=y
CONFIG_BOARD_EMULATION_QEMU_X86_Q35=y
CONFIG_ANY_TOOLCHAIN=y
EOF
make -C "$root" obj="$temporary/build" DOTCONFIG="$temporary/config/.config" \
	olddefconfig >/dev/null
if grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR=y$' \
	"$temporary/config/.config"; then
	echo "ERROR: dormant executor enabled by default" >&2
	exit 1
fi

cp "$root/src/Kconfig" "$temporary/Kconfig"
cat >> "$temporary/Kconfig" <<'EOF'

config TEST_MOR_CLEAR_EXECUTOR_SELECTOR
	bool
	default y
	select HAVE_SMI_HANDLER
	select PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT
	select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_PLAN
	select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RECEIPT
	select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR
EOF
cp "$temporary/config/.config" "$temporary/config-selected"
make -C "$root" obj="$temporary/build-selected" KBUILD_KCONFIG="$temporary/Kconfig" \
	DOTCONFIG="$temporary/config-selected" olddefconfig >/dev/null
grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR=y' \
	"$temporary/config-selected"
grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RECEIPT=y' \
	"$temporary/config-selected"
grep -qx 'CONFIG_HAVE_SMI_HANDLER=y' "$temporary/config-selected"
grep -qx 'ramstage-$(CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR) += payload_mm_authvar_mor_clear_executor.c' \
	"$root/src/lib/Makefile.mk"
make -C "$root" obj="$temporary/build-selected" KBUILD_KCONFIG="$temporary/Kconfig" \
	DOTCONFIG="$temporary/config-selected" -j"$(getconf _NPROCESSORS_ONLN)" >/dev/null

echo 'payload_mm_authvar_mor_clear_executor validation: PASS'
