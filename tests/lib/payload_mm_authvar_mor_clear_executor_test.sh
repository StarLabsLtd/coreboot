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
	's/payload_mm_authvar_mor_clear_plan_validate(&state.plan_snapshot)/CB_SUCCESS/'
mutant_test volatile-readback \
	's/if (((const volatile uint8_t \*)state.iteration.mapping)/if (false \&\& ((const volatile uint8_t *)state.iteration.mapping)/'
mutant_test cache-fence-order \
	's/state.ops_snapshot.fence(state.ops_snapshot.context);/state.iteration.chunk_error == CB_SUCCESS ? state.ops_snapshot.fence(state.ops_snapshot.context) : CB_SUCCESS;/'
mutant_test dma-equality \
	'/memcmp(&state.facts.dma_before/,/goto fail;/ s/goto fail;/state.facts.dma_after = state.facts.dma_before;/'
mutant_test unmap-failure \
	's/return unmap_error;/(void)unmap_error; return CB_SUCCESS;/'
mutant_test output-recheck \
	'/bytes_zero(state->transcript_output,/,+1c\
\t\ttrue \&\&'
mutant_test final-plan-recheck \
	's/!memcmp(&state->plan_snapshot, state->plan_input,/!memcmp(state->plan_input, state->plan_input,/'
mutant_test physical-window-boundary \
	's/return MIN(bounded_remaining, until_boundary);/return MIN(bounded_remaining, until_boundary | window_bytes);/'
mutant_test initial-inventory-validation \
	'0,/live_inventory_validate(&state)/ s/live_inventory_validate(&state)/CB_SUCCESS/'
mutant_test late-inventory-validation \
	'/state.candidate.dma_after =/,/payload_mm_authvar_mor_clear_receipt_build/ s/live_inventory_validate(&state)/CB_SUCCESS/'

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
stack_total=$(awk -F '\t' '
	$1 ~ /:payload_mm_authvar_mor_clear_execute$/ { executor = $2 + 0 }
	$1 ~ /:payload_mm_authvar_mor_clear_receipt_build$/ { receipt = $2 + 0 }
	$1 ~ /:payload_mm_authvar_mor_grant_validate$/ { grant = $2 + 0 }
	END {
		if (!executor || !receipt || !grant) exit 1
		print executor + receipt + grant
	}
' "$temporary/executor-stack.su" "$temporary/clear-stack.su" \
	"$temporary/grant-stack.su")
test "$stack_total" -le 6144

mkdir -p "$temporary/config" "$temporary/build"
cat > "$temporary/config/.config" <<'EOF'
CONFIG_VENDOR_EMULATION=y
CONFIG_BOARD_EMULATION_QEMU_X86_Q35=y
CONFIG_ANY_TOOLCHAIN=y
EOF
make -C "$root" obj="$temporary/build" DOTCONFIG="$temporary/config/.config" \
	olddefconfig >/dev/null
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR=y$' \
	"$temporary/config/.config"

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
