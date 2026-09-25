#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
cat > "$temporary/include/config.h" <<'EOF'
#define CONFIG_STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_GUARD 1
#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0
EOF

build_and_run()
{
	name=$1
	shift
	source=${MOR_PLATFORM_SOURCE:-$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c}
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin -pthread "$@" \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
		"$root/tests/lib/starbook_mtl_mor_platform_test.c" \
		"$source" \
		-o "$temporary/$name"
	if [ "${MOR_TEST_TIMEOUT:-0}" = 1 ]; then
		timeout 5 "$temporary/$name"
	else
		"$temporary/$name"
	fi
}

check_stack_usage()
{
	"${CC:-cc}" -m32 -std=gnu11 -Os -Wall -Wextra -Werror -fno-builtin \
		-fstack-usage -D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
		-c "$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c" \
		-o "$temporary/mor-platform-stack.o"
	usage=$temporary/mor-platform-stack.su
	awk -F '\t' '
		$1 ~ /:(ensure_seed|reservations_register|resolve_binding|private_complete|private_close)$/ {
			seen++
			if ($2 + 0 > 640) exit 1
		}
		END { if (seen != 5) exit 1 }
	' "$usage"
}

reject_mutant()
{
	name=$1
	mutant=$temporary/$name.c
	shift
	"$@" > "$mutant"
	stats=$(git diff --no-index --numstat -- \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c" \
		"$mutant" || true)
	[ "$(printf '%s\n' "$stats" | awk \
		'NF >= 3 && $1 == 1 && $2 == 1 { matches++ }
		 END { print matches == 1 ? "yes" : "no" }')" = yes ] || {
		printf '%s\n' "$name did not make exactly one source-line edit" >&2
		exit 1
	}
	if MOR_TEST_TIMEOUT=1 MOR_PLATFORM_SOURCE=$mutant \
		build_and_run "$name" -O2 >/dev/null 2>&1; then
		printf '%s\n' "$name unexpectedly survived" >&2
		exit 1
	fi
}

build_and_run o0 -O0
build_and_run o2 -O2
build_and_run asan -O1 -fsanitize=address -fno-omit-frame-pointer
build_and_run ubsan -O1 -fsanitize=undefined -fno-omit-frame-pointer
build_and_run tsan -O1 -fsanitize=thread -fno-omit-frame-pointer
check_stack_usage
reject_mutant reentry-without-seeding sed \
	'0,/MTL_MOR_SEEDING, false, __ATOMIC_ACQ_REL/s//MTL_MOR_SEEDED, false, __ATOMIC_ACQ_REL/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"

reject_mutant provider-boundary-failure-ignored sed \
	'0,/if (load_private() != CB_SUCCESS ||/s//if (false ||/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant provider-zero-stack-budget-accepted sed \
	'0,/ops->callback_stack_bytes &&/s//true \&\&/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant provider-excess-stack-budget-accepted sed \
	'0,/ops->callback_stack_bytes <=/s//ops->callback_stack_bytes >=/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant provider-reserved-field-accepted sed \
	'0,/!ops->reserved &&/s//true \&\&/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant classification-failure-ignored sed \
	'0,/if (classify_retained() != CB_SUCCESS)/s//if (false)/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant terminal-seed-not-poisoned sed \
	'/static void provider_terminal_poison/,/^}/s/MTL_MOR_SEED_POISONED/MTL_MOR_SEED_EMPTY/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant descriptor-context-alias sed \
	'0,/!ranges_overlap(ops->context, ops->context_size, ops,/s//!ranges_overlap(ops->context, ops->context_size, \&platform,/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant classify-boot-context-alias sed \
	'0,/!seeded_and_disjoint(boot, sizeof(\*boot)) ||/s//!seeded_and_disjoint(NULL, 0) ||/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant resolve-plan-context-alias sed \
	'0,/!private_context_disjoint(plan, sizeof(\*plan)) ||/s//false ||/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant resolve-transient-plan sed \
	'/&platform.guard, false, plan, &platform.binding/s/plan, &platform.binding/\&frozen->binding.authority.plan, \&platform.binding/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant resolve-transient-binding sed \
	'/&platform.guard, false, plan, &platform.binding/s/&platform.binding/\&frozen->binding/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant completion-grant-context-alias sed \
	'0,/!seeded_and_disjoint(grant, sizeof(\*grant)) ||/s//!seeded_and_disjoint(NULL, 0) ||/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant reservation-without-operation-claim sed \
	'/static enum cb_err reservations_register/,/static enum cb_err resolve_binding/s/if (!private_callback_enter())/if (false \&\& !private_callback_enter())/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant resolution-without-operation-claim sed \
	'/static enum cb_err resolve_binding/,/static enum cb_err private_complete/s/if (!private_callback_enter())/if (false \&\& !private_callback_enter())/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant resolution-preclaim-output-read sed \
	'/static enum cb_err resolve_binding/,/static enum cb_err private_complete/s/if (!private_callback_enter())/if ((memcpy(plan_original, plan, sizeof(plan_original)), false) || !private_callback_enter())/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant reservation-without-final-release sed \
	'/static enum cb_err reservations_register/,/static enum cb_err resolve_binding/s/if (!private_callback_leave(true)) {/if (false) {/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant resolution-without-final-release sed \
	'/static enum cb_err resolve_binding/,/static enum cb_err private_complete/s/if (!private_callback_leave(true))/if (false)/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant terminal-owner-not-scrubbed sed \
	'/static void provider_terminal_poison/,/^}/s/scrub(platform.owner, sizeof(platform.owner));/scrub(platform.owner, 0);/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant conflict-without-atomic-poison sed \
	'/static bool private_callback_enter/,/static bool private_callback_leave/s/MTL_MOR_CALLBACK_ACTIVE : MTL_MOR_CALLBACK_POISONED/MTL_MOR_CALLBACK_ACTIVE : MTL_MOR_CALLBACK_ACTIVE/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant constructor-candidate-context-alias sed \
	'0,/!private_context_disjoint(candidate, candidate_size) ||/s//false ||/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant constructor-original-context-alias sed \
	'0,/!private_context_disjoint(original, original_size) ||/s//false ||/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant snapshot-without-scratch-claim sed \
	'0,/frozen = scratch_claim(MTL_MOR_SCRATCH_SEED);/s//frozen = \&scratch.frozen;/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant snapshot-without-scrub sed \
	'/static bool scratch_release/,/static void scratch_abort/s/scrub(\&scratch.frozen, sizeof(scratch.frozen));/scrub(\&scratch.frozen, 0);/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
reject_mutant provider-object-allows-scratch sed \
	'/static bool provider_object_valid/,/^}/s/!ranges_overlap(object, size, \&scratch, sizeof(scratch))/true/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c"
printf '%s\n' 'StarBook MTL MOR platform provider tests: PASS'
