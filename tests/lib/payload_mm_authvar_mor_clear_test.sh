#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '#define %s %s\n' CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0 > \
	"$temporary/include/config.h"

cases='plan plan-reject plan-mutation receipt receipt-reject alias-range
alias-pairs receipt-mutation dma-mismatch written-count'

compile_binary()
{
	name=$1
	clear_source=$2
	plan_source=$3
	shift 3
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin \
		-ffunction-sections -fdata-sections -Wl,--gc-sections "$@" \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/payload_mm_authvar_mor_clear_test.c" \
		"$clear_source" "$plan_source" \
		"$root/src/lib/payload_mm_authvar_mor_grant.c" \
		-o "$temporary/$name"
}

build_and_run()
{
	name=$1
	source=$2
	plan_source=$3
	shift 3
	compile_binary "$name" "$source" "$plan_source" "$@"
	for case_name in $cases; do
		"$temporary/$name" "$case_name"
	done
}

source_file="$root/src/lib/payload_mm_authvar_mor_clear.c"
plan_source="$root/src/lib/payload_mm_authvar_mor_clear_plan.c"
build_and_run o0 "$source_file" "$plan_source" -O0
build_and_run o2 "$source_file" "$plan_source" -O2
build_and_run asan "$source_file" "$plan_source" -O1 \
	-fsanitize=address -fno-omit-frame-pointer
build_and_run ubsan "$source_file" "$plan_source" -O1 \
	-fsanitize=undefined -fno-omit-frame-pointer

"${CC:-cc}" -std=gnu11 -Os -m32 -Wall -Wextra -Werror -fno-builtin \
	-fstack-usage -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$temporary/include" -c "$source_file" -o "$temporary/clear-stack.o"
"${CC:-cc}" -std=gnu11 -Os -m32 -Wall -Wextra -Werror -fno-builtin \
	-fstack-usage -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" -c "$plan_source" -o "$temporary/plan-stack.o"
"${CC:-cc}" -std=gnu11 -Os -m32 -Wall -Wextra -Werror -fno-builtin \
	-fstack-usage -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" -c "$root/src/lib/payload_mm_authvar_mor_grant.c" \
	-o "$temporary/grant-stack.o"
awk -F '\t' '
	$1 ~ /:payload_mm_authvar_mor_clear_plan_build$/ ||
	$1 ~ /:payload_mm_authvar_mor_clear_receipt_build$/ {
		seen++
		if ($1 ~ /:payload_mm_authvar_mor_clear_receipt_build$/)
			receipt = $2 + 0
		if ($2 + 0 > 4096) {
			print "ERROR: ramstage stack bound exceeded by " $1 ": " $2 > "/dev/stderr"
			exit 1
		}
	}
	END {
		if (seen != 2 || !receipt) {
			print "ERROR: incomplete ramstage stack evidence" > "/dev/stderr"
			exit 1
		}
	}
	' "$temporary/plan-stack.su" "$temporary/clear-stack.su" > \
		"$temporary/clear-stack-result"
awk -F '\t' '
	$1 ~ /:payload_mm_authvar_mor_grant_validate$/ { validate = $2 + 0 }
	END {
		if (!validate) {
			print "ERROR: incomplete nested validator stack evidence" > "/dev/stderr"
			exit 1
		}
		print validate
	}
' "$temporary/grant-stack.su" > "$temporary/grant-stack-result"
receipt_stack=$(awk -F '\t' \
	'$1 ~ /:payload_mm_authvar_mor_clear_receipt_build$/ { print $2 }' \
	"$temporary/clear-stack.su")
grant_stack=$(cat "$temporary/grant-stack-result")
if test "$((receipt_stack + grant_stack))" -gt 4096; then
	echo "ERROR: nested receipt/validator stack bound exceeded" >&2
	exit 1
fi

mutant_test()
{
	name=$1
	target=$2
	old=$3
	new=$4
	case_name=$5
	mutant="$temporary/$name.c"
	sed "s#$old#$new#" "$target" > "$mutant"
	if cmp -s "$target" "$mutant"; then
		echo "ERROR: $name mutation was not applied" >&2
		exit 1
	fi
	mutant_clear=$source_file
	mutant_plan=$plan_source
	if test "$target" = "$source_file"; then mutant_clear=$mutant; else mutant_plan=$mutant; fi
	if ! compile_binary "$name" "$mutant_clear" "$mutant_plan" -O2; then
		echo "ERROR: $name mutant did not compile" >&2
		exit 1
	fi
	if "$temporary/$name" "$case_name" >/dev/null 2>&1; then
		echo "ERROR: $name mutation survived ($case_name)" >&2
		exit 1
	fi
}

mutant_test merge-adjacent "$plan_source" 'previous && span->base == previous_end &&' \
	'previous \&\& true \&\&' plan
mutant_test inventory-recheck "$plan_source" 'memcmp(\&snapshot, inventory, sizeof(snapshot))' \
	'memcmp(inventory, inventory, sizeof(snapshot))' plan-mutation
mutant_test dma-revalidation "$source_file" \
	'memcmp(\&facts_snapshot.dma_before, \&facts_snapshot.dma_after,' \
	'memcmp(\&facts_snapshot.dma_before, \&facts_snapshot.dma_before,' \
	dma-mismatch
mutant_test written-count "$source_file" 'record->written_bytes != span->size' \
	'false' written-count
mutant_test final-validator "$source_file" \
	'payload_mm_authvar_mor_grant_validate(\&candidate) != CB_SUCCESS' \
	'true' receipt
mutant_test receipt-recheck "$source_file" 'memcmp(\&plan_snapshot, plan, sizeof(plan_snapshot))' \
	'memcmp(plan, plan, sizeof(plan_snapshot))' receipt-mutation

mkdir -p "$temporary/config-default" "$temporary/build-default" \
	"$temporary/config-plan" "$temporary/build-plan" \
	"$temporary/config-clear" "$temporary/build-clear"
cat > "$temporary/config-default/.config" <<'EOF'
CONFIG_VENDOR_EMULATION=y
CONFIG_BOARD_EMULATION_QEMU_X86_Q35=y
EOF
make -C "$root" obj="$temporary/build-default" \
	DOTCONFIG="$temporary/config-default/.config" olddefconfig >/dev/null
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RECEIPT=y$' \
	"$temporary/config-default/.config"

cp "$root/src/Kconfig" "$temporary/Kconfig-plan"
cat >> "$temporary/Kconfig-plan" <<'EOF'

config TEST_MOR_CLEAR_PLAN_SELECTOR
	bool
	default y
	select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_PLAN
EOF
cp "$temporary/config-default/.config" "$temporary/config-plan/.config"
make -C "$root" obj="$temporary/build-plan" KBUILD_KCONFIG="$temporary/Kconfig-plan" \
	DOTCONFIG="$temporary/config-plan/.config" olddefconfig >/dev/null
grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_PLAN=y' \
	"$temporary/config-plan/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RECEIPT=y$' \
	"$temporary/config-plan/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT=y$' \
	"$temporary/config-plan/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR=y$' \
	"$temporary/config-plan/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_POLICY=y$' \
	"$temporary/config-plan/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_CONTRACT=y$' \
	"$temporary/config-plan/.config"
! grep -q '^CONFIG_SMMSTORE=y$' "$temporary/config-plan/.config"

cp "$root/src/Kconfig" "$temporary/Kconfig"
cat >> "$temporary/Kconfig" <<'EOF'

config TEST_MOR_CLEAR_SELECTOR
	bool
	default y
	select PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT
	select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_PLAN
	select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RECEIPT
EOF
cp "$temporary/config-default/.config" "$temporary/config-clear/.config"
make -C "$root" obj="$temporary/build-clear" KBUILD_KCONFIG="$temporary/Kconfig" \
	DOTCONFIG="$temporary/config-clear/.config" olddefconfig >/dev/null
grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT=y' \
	"$temporary/config-clear/.config"
grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_PLAN=y' \
	"$temporary/config-clear/.config"
grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RECEIPT=y' \
	"$temporary/config-clear/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_ENTRY_PROBE=y$' \
	"$temporary/config-clear/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_POLICY=y$' \
	"$temporary/config-clear/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_CONTRACT=y$' \
	"$temporary/config-clear/.config"
! grep -q '^CONFIG_SMMSTORE=y$' "$temporary/config-clear/.config"

grep -qx 'ramstage-$(CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RECEIPT) += payload_mm_authvar_mor_clear.c' \
	"$root/src/lib/Makefile.mk"
grep -qx 'ramstage-$(CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_PLAN) += payload_mm_authvar_mor_clear_plan.c' \
	"$root/src/lib/Makefile.mk"
test "$(rg -n 'payload_mm_authvar_mor_clear_plan[.]c' "$root/src" \
	-g Makefile.mk | wc -l)" -eq 1
test "$(rg -n 'payload_mm_authvar_mor_clear[.]c' "$root/src" \
	-g Makefile.mk | wc -l)" -eq 1

echo 'payload_mm_authvar_mor_clear tests: PASS'
