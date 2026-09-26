#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 1' \
	> "$temporary/include/config.h"

run_test()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wconversion \
		-Wshadow -fno-builtin -pthread "$@" -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_presence_authority_test.c" \
		"$root/src/lib/payload_mm_authvar_presence.c" \
		"$root/src/lib/payload_mm_authvar_presence_authority.c" \
		-o "$temporary/$name"
	ASAN_OPTIONS=detect_leaks=1 "$temporary/$name"
}

run_test strict-O0 -O0
run_test strict-O2 -O2
run_test sanitized-O0 -O0 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
run_test sanitized-O2 -O2 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all

mutation()
{
	name=$1
	expression=$2
	mutant="$temporary/$name.c"

	sed "$expression" \
		"$root/src/lib/payload_mm_authvar_presence_authority.c" > "$mutant"
	if cmp -s "$mutant" \
		"$root/src/lib/payload_mm_authvar_presence_authority.c"; then
		printf 'mutation changed nothing: %s\n' "$name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$temporary/$name-O$optimization"
		"${CC:-cc}" -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
			-Wconversion -Wshadow -fno-builtin -pthread \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-D__TEST__ -D__COREBOOT__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
			-I"$root/src/include" -I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_presence_authority_test.c" \
			"$root/src/lib/payload_mm_authvar_presence.c" "$mutant" \
			-o "$binary"
		if ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
			printf 'mutation survived: %s O%s\n' "$name" "$optimization" >&2
			exit 1
		fi
	done
}

mutation no-capability-check \
	's/return different == 0;/return true;/'
mutation no-dma \
	's/valid = snapshot.dma_protected/valid = true || snapshot.dma_protected/'
mutation no-rendezvous \
	's/valid = snapshot.cpu_rendezvous_active/valid = true || snapshot.cpu_rendezvous_active/'
mutation no-post-executor-proof \
	's/if (!execution_guard()) {/if (false) {/'
mutation no-context-seal \
	's/return !memcmp(presence.context, presence.sealed_context, size);/return true || !memcmp(presence.context, presence.sealed_context, size);/'
mutation protected-mailbox \
	's/return !proof(context,/return true || !proof(context,/'
mutation no-scrub \
	's/scrub(presence.capability, sizeof(presence.capability));/(void)presence.capability;/'
mutation no-failed-provision-mailbox-scrub \
	'0,/endpoint->communication_size);/s//0U);/'
mutation mutable-reset-context \
	's/policy->context_size ? reset_context : NULL/policy->context/'
mutation no-reset \
	's/policy->cold_reset(policy->context_size ? reset_context : NULL);/(void)policy;/'
mutation unbound-restriction \
	'/valid = generation &&/,/policy_equal();/c\
\tvalid = presence.generation \&\&\
\t\tpresence.generation == presence.sealed_generation \&\&\
\t\tpresence.policy.endpoint.generation ==\
\t\tpresence.sealed.endpoint.generation \&\& policy_equal();'
mutation no-context-restriction-scrub \
	's/scrub(presence.context, sizeof(presence.context));/(void)presence.context;/'
mutation closed-wrong-generation \
	's/generation && presence.closed_generation == generation &&/presence.closed_generation \&\& presence.closed_generation == presence.sealed_closed_generation \&\&/
s/presence.sealed_closed_generation == generation/presence.sealed_closed_generation == presence.closed_generation/
s/) == generation &&/) == presence.closed_generation \&\&/
s/presence.policy.endpoint.generation == generation/presence.policy.endpoint.generation == presence.closed_generation/
s/presence.sealed.endpoint.generation == generation/presence.sealed.endpoint.generation == presence.closed_generation/'
mutation reentry-not-terminal \
	'/if (phase == PRESENCE_RESTRICTING) {/,/^\t}/s/PRESENCE_POISONED/PRESENCE_RESTRICTING/'
mutation no-scrubbed-generation-check \
	's/return presence.generation == 0 && presence.sealed_generation == 0 &&/return true \&\&/'
mutation no-install-gate-binding \
	's/return __atomic_load_n(\&presence.install_attempted, __ATOMIC_ACQUIRE) == 1;/return true;/'
mutation no-install-phase-guard \
	's/return __atomic_load_n(\&presence.phase, __ATOMIC_ACQUIRE) == PRESENCE_EMPTY;/return true;/'

if grep -Eq '(^|[^A-Za-z0-9_])(variable_name|vendor_guid|data_size|SetVariable)([^A-Za-z0-9_]|$)' \
	"$root/src/include/boot/payload_mm_authvar_presence_authority.h"; then
	printf '%s\n' 'presence authority exposes a generic variable operation' >&2
	exit 1
fi

if grep -R -Eq 'select[[:space:]]+PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY' \
	"$root/src"/*/Kconfig "$root/src"/Kconfig 2>/dev/null || \
	awk '$1 == "config" { inside = $2 == \
		"PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY"; next }
		inside && (($1 == "bool" && NF > 1) || $1 == "prompt" || \
			($1 == "default" && $2 == "y")) \
			{ bad = 1 }
		END { exit !bad }' "$root/src/lib/Kconfig"; then
	printf '%s\n' 'presence authority became platform-selectable' >&2
	exit 1
fi

printf '%s\n' 'Payload-MM authenticated-variable presence authority tests: PASS'
