#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER 1' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	> "$temporary/include/config.h"

flags='-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin'
includes="-include $root/src/include/kconfig.h -include $root/src/include/rules.h
-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
-I$temporary/include -I$root/src -I$root/src/include
-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
-I$root/src/arch/x86/include"

build()
{
	name=$1
	extra=$2
	sender_source=${3:-$root/src/lib/payload_mm_authvar_presence_tuple_sender.c}
	# Deliberate host harness flag splitting.
	# shellcheck disable=SC2086
	${CC:-cc} $flags $extra -D__TEST__ -D__COREBOOT__ $includes \
		"$root/tests/lib/payload_mm_authvar_presence_tuple_sender_test.c" \
		"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
		"$sender_source" -o "$temporary/$name"
}

run()
{
	build "$1" "$2"
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1 "$temporary/$1"
}

run strict-O0 '-O0'
run strict-O2 '-O2'
run sanitized-O0 \
	'-O0 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all'
run sanitized-O2 \
	'-O2 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all'

# Insert a test-only callback at the exact publication-to-trigger boundary.
# This executes the production rechecks while independently mutating each
# caller-owned input before the privileged trigger could run.
pre_trigger_source="$temporary/pre-trigger-hook.c"
sed -e '/static enum cb_err send/i void tuple_sender_before_trigger(void);' \
	-e '/^[[:space:]]*if (memcmp(context, &sender, sizeof(sender))/i\
\ttuple_sender_before_trigger();' \
	"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c" \
	> "$pre_trigger_source"
test "$(grep -c 'tuple_sender_before_trigger();' "$pre_trigger_source")" -eq 1
build pre-trigger-rechecks \
	'-O2 -DTEST_PRE_TRIGGER_HOOK_ENABLED=1' "$pre_trigger_source"
"$temporary/pre-trigger-rechecks"

for optimization in 0 2; do
	object="$temporary/sender-O$optimization.o"
	# Deliberate freestanding compiler flag splitting.
	# shellcheck disable=SC2086
	${CC:-cc} $flags -O$optimization -m32 -ffreestanding -fstack-usage \
		-D__TEST__ -D__COREBOOT__ $includes -c \
		"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c" \
		-o "$object"
	test "$(objdump -dr "$object" | grep -c \
		'R_386_.*smm_invocation_tuple_trigger')" -eq 1
	if objdump -dr "$object" | grep -Eq \
		'(^|[[:space:]])(out|outs)[bwl]?[[:space:]]'; then
		printf '%s\n' 'tuple sender gained a direct OUT instruction' >&2
		exit 1
	fi
	if nm -u "$object" | grep -Eq '__atomic|libatomic'; then
		printf '%s\n' 'tuple sender gained a libatomic dependency' >&2
		exit 1
	fi
	stack_file=${object%.o}.su
	stack=$(awk -F '\t' '$1 ~ /:send$/ { print $2 }' "$stack_file")
	test -n "$stack"
	test "$stack" -le 1536
done

mutation()
{
	name=$1
	expression=$2
	mutant="$temporary/$name.c"
	sed "$expression" \
		"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c" > "$mutant"
	if cmp -s "$mutant" \
		"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c"; then
		printf 'mutation changed nothing: %s\n' "$name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		build "$name-O$optimization" "-O$optimization" "$mutant"
		if "$temporary/$name-O$optimization" >/dev/null 2>&1; then
			printf 'mutation survived: %s O%s\n' "$name" \
				"$optimization" >&2
			exit 1
		fi
	done
}

mutation no-page-scrub 's/scrub(page, sizeof(\*page));/(void)page;/g'
mutation wrong-decision \
	's/request.decision = decision;/request.decision = decision + 1U;/'
mutation no-prepare-seed \
	's/request.seed = seed_snapshot;/(void)seed_snapshot;/'
mutation no-request-zero \
	's/zero(\&response_request, sizeof(response_request))/true/'
mutation no-reserved-zero \
	's/zero(page->reserved, sizeof(page->reserved))/true/g'
mutation no-binding-recheck \
	's/!memcmp(binding, \&binding_snapshot, sizeof(binding_snapshot))/true/g'
mutation no-seed-recheck \
	's/!memcmp(seed, \&seed_snapshot, sizeof(seed_snapshot))/true/g'
mutation no-context-recheck \
	's/!memcmp(context, \&sender, sizeof(sender))/true/g'

# Compiler barriers are a source-level transport contract on i386. Require
# each compiled mutant to differ, then let the exact source oracle reject it.
fence_mutant="$temporary/no-release-fence.c"
sed '0,/__atomic_thread_fence(__ATOMIC_RELEASE)/s/__atomic_thread_fence(__ATOMIC_RELEASE)/__atomic_thread_fence(__ATOMIC_ACQUIRE)/' \
	"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c" > "$fence_mutant"
test ! "$fence_mutant" -ef \
	"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c"
build no-release-fence-O2 '-O2' "$fence_mutant"
test "$(grep -c '__atomic_thread_fence(__ATOMIC_RELEASE)' "$fence_mutant")" -eq 0
test "$(grep -c '__atomic_thread_fence(__ATOMIC_RELEASE)' \
	"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c")" -eq 1

test "$(grep -c '__atomic_thread_fence(__ATOMIC_ACQUIRE)' \
	"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c")" -eq 2
test "$(grep -c 'value = smm_invocation_tuple_trigger();' \
	"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c")" -eq 1
test "$(grep -c 'memcmp(context, &sender, sizeof(sender))' \
	"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c")" -eq 2
test "$(grep -c 'memcmp(binding, &binding_snapshot, sizeof(binding_snapshot))' \
	"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c")" -eq 2
test "$(grep -c 'memcmp(seed, &seed_snapshot, sizeof(seed_snapshot))' \
	"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c")" -eq 2
if rg -n 'call_smm|out[bwl]?\(|lb_new_record|coreboot_table' \
	"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c" \
	"$root/src/include/boot/payload_mm_authvar_presence_tuple_sender.h"; then
	printf '%s\n' 'tuple sender gained a route, direct OUT or public record' >&2
	exit 1
fi
if ! sh "$root/tests/lib/payload_mm_authvar_presence_selector_check.sh" \
	"$root" PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER; then
	printf '%s\n' 'tuple sender gained a selector' >&2
	exit 1
fi

printf '%s\n' 'Payload-MM authenticated-variable tuple sender tests: PASS'
