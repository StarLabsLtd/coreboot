#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_MAX_CPUS 8' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ARM 1' \
	> "$temporary/include/config.h"

flags='-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin -pthread'
includes="-include $root/src/include/kconfig.h -include $root/src/include/rules.h
-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
-I$temporary/include -I$root/src -I$root/src/include
-I$root/src/lib
-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
-I$root/src/arch/x86/include"

build_and_run()
{
	name=$1
	extra=$2
	# Deliberate host harness flag splitting.
	# shellcheck disable=SC2086
	${CC:-cc} $flags $extra -D__TEST__ -D__COREBOOT__ $includes \
		"$root/tests/lib/payload_mm_authvar_presence_arm_test.c" \
		"$root/src/lib/payload_mm_authvar_presence_arm.c" \
		"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
		-o "$temporary/$name"
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1 "$temporary/$name"
}

build_and_run strict-O0 '-O0'
build_and_run strict-O2 '-O2'
build_and_run sanitized-O0 \
	'-O0 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all'
build_and_run sanitized-O2 \
	'-O2 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all'
build_and_run thread-sanitized \
	'-O1 -g -fno-omit-frame-pointer -fsanitize=thread -fno-sanitize-recover=all -Wno-error=tsan'

# Run the actual generic receiver through PREPARE and COMMIT with the arm's
# private wrapped policy; the focused harness above deliberately mocks only
# provisioning so it can isolate hostile adapter transitions.
# Deliberate host harness flag splitting.
# shellcheck disable=SC2086
${CC:-cc} $flags -Wno-conversion -O2 -D__TEST__ -D__COREBOOT__ $includes \
	-c "$root/src/lib/bootmem_reservation_receipt.c" \
	-o "$temporary/real-receipt.o"
# Deliberate host harness flag splitting.
# shellcheck disable=SC2086
${CC:-cc} $flags -O2 -D__TEST__ -D__COREBOOT__ $includes \
	"$root/tests/lib/payload_mm_authvar_presence_arm_receiver_test.c" \
	"$root/src/lib/payload_mm_authvar_presence_arm.c" \
	"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
	"$root/src/lib/payload_mm_authvar_presence_transaction_receiver.c" \
	"$temporary/real-receipt.o" \
	-o "$temporary/real-receiver"
"$temporary/real-receiver"

mutation()
{
	name=$1
	expression=$2
	mutant="$temporary/$name.c"
	sed "$expression" "$root/src/lib/payload_mm_authvar_presence_arm.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_presence_arm.c"; then
		printf 'mutation changed nothing: %s\n' "$name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		# Deliberate host harness flag splitting.
		# shellcheck disable=SC2086
		${CC:-cc} $flags -O$optimization -D__TEST__ -D__COREBOOT__ $includes \
			"$root/tests/lib/payload_mm_authvar_presence_arm_test.c" \
			"$mutant" \
			"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
			-o "$temporary/$name-O$optimization"
		if "$temporary/$name-O$optimization" >/dev/null 2>&1; then
			printf 'mutation survived: %s O%s\n' "$name" \
				"$optimization" >&2
			exit 1
		fi
	done
}

mutation wrong-trigger \
	's/seed->endpoint.trigger_value ==/seed->endpoint.trigger_value !=/'
mutation no-maximum-cpus-binding \
	's/b.maximum_cpus != arm->active_cpus/false/;
	 s/arm->active_cpus == arm->binding.maximum_cpus/true/'
mutation pre-rejection-enters-abort \
	's/audit == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_PREPARE_REJECTED/audit != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_PREPARE_REJECTED/'
mutation no-ready-release \
	'/__atomic_compare_exchange_n(\&arm->state, \&expected/,+1s/PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_READY/PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND/'
mutation no-exact-invocation \
	's/memcmp(invocation, \&arm->invocation, sizeof(\*invocation))/false/g'
mutation allow-decision-generation-replay \
	's/invocation->smi_generation <= arm->completed_prepare_generation/false/'
mutation incomplete-terminal-scrub \
	's/arm->composition = NULL;/(void)arm;/'
mutation ready-before-completion \
	'/PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_COMMIT_PENDING, false,/,/return CB_SUCCESS;/s/return CB_SUCCESS;/__atomic_store_n(\&arm->state, PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_READY, __ATOMIC_RELEASE); return CB_SUCCESS;/'
mutation no-commit-audit \
	's/audit == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_COMMITTED/true/'
mutation no-commit-generation \
	's/arm->completed_prepare_generation \&\&/true \&\&/'
mutation no-post-proof-image-gate \
	'/static bool wrapped_protected_storage/,/return protected;/s/if (memcmp(\&before, arm, sizeof(before)))/if (false)/'
mutation trust-mutated-fail-stop \
	'/static enum cb_err wrapped_commit/,/return CB_SUCCESS;/s/arm_fail_stop_snapshot(arm, \&before)/arm_fail_stop(arm)/'

# Deliberate host harness flag splitting.
# shellcheck disable=SC2086
${CC:-cc} $flags -Os -m32 -ffreestanding -D__TEST__ -D__COREBOOT__ $includes \
	-c "$root/src/lib/payload_mm_authvar_presence_arm.c" \
	-o "$temporary/arm-32.o"
if nm -u "$temporary/arm-32.o" | grep -Eq '__atomic|libatomic'; then
	printf '%s\n' 'presence arm gained a libatomic dependency' >&2
	exit 1
fi

kconfig_default_n()
{
	symbol=$1
	awk -v symbol="$symbol" '
		$1 == "config" { active = $2 == symbol; next }
		active && $1 == "default" && $2 == "n" { found = 1 }
		END { exit !found }
	' "$root/src/lib/Kconfig"
}

kconfig_default_n PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PLATFORM
kconfig_default_n PAYLOAD_MM_AUTHVAR_PRESENCE_ARM
if grep -R -Eq 'select[[:space:]]+PAYLOAD_MM_AUTHVAR_PRESENCE_ARM(_PLATFORM)?' \
	"$root/src"; then
	printf '%s\n' 'presence arm gained a production selector' >&2
	exit 1
fi
if rg -q 'payload_mm_authvar_presence_arm_provision' \
	"$root/src" \
	-g '!src/include/boot/payload_mm_authvar_presence_arm.h' \
	-g '!src/lib/payload_mm_authvar_presence_arm.c'; then
	printf '%s\n' 'presence arm gained a production callsite' >&2
	exit 1
fi
if rg -q 'payload_mm_authvar_presence_arm_transaction_provision' \
	"$root/src" \
	-g '!src/include/boot/payload_mm_authvar_presence_arm.h' \
	-g '!src/lib/payload_mm_authvar_presence_arm.c' \
	-g '!src/lib/payload_mm_authvar_presence_route_session.c'; then
	printf '%s\n' 'presence arm transaction composition escaped route session' >&2
	exit 1
fi
if rg -q 'platform_payload_mm_authvar_presence_arm\(' "$root/src" \
	-g '!src/include/boot/payload_mm_authvar_presence_arm.h' \
	-g '!src/lib/payload_mm_authvar_presence_arm.c'; then
	printf '%s\n' 'presence arm gained a production platform provider' >&2
	exit 1
fi
if rg -q 'presence_arm_receipt|presence_arm_.*ready\(' \
	"$root/src/include/boot/payload_mm_authvar_presence_arm.h" \
	"$root/src/lib/payload_mm_authvar_presence_arm.c"; then
	printf '%s\n' 'presence arm gained a second receipt or readiness API' >&2
	exit 1
fi

printf '%s\n' 'presence publication-bound arm O0/O2/sanitizers/TSan/i686: PASS'
