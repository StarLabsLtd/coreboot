#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_MAX_CPUS 8' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION 1' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	> "$temporary/include/config.h"

flags='-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin -pthread'
includes="-include $root/src/include/kconfig.h -include $root/src/include/rules.h
-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
-I$temporary/include -I$root/src -I$root/src/include
-I$root/src/lib
-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
-I$root/src/arch/x86/include"

build()
{
	name=$1
	extra=$2
	common=${3:-$root/src/lib/payload_mm_authvar_presence_transaction.c}
	receiver=${4:-$root/src/lib/payload_mm_authvar_presence_transaction_receiver.c}
	# Deliberate host harness flag splitting.
	# shellcheck disable=SC2086
	${CC:-cc} $flags $extra -D__TEST__ -D__COREBOOT__ $includes \
		"$root/tests/lib/payload_mm_authvar_presence_transaction_test.c" \
		"$common" "$receiver" -o "$temporary/$name"
}

run()
{
	build "$1" "$2"
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1 "$temporary/$1"
}

run strict-O0 '-O0'
run strict-O2 '-O2'
run sanitized-O0 '-O0 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all'
run sanitized-O2 '-O2 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all'
run thread-sanitized '-O1 -g -fno-omit-frame-pointer -fsanitize=thread -fno-sanitize-recover=all -Wno-error=tsan'

mutation()
{
	name=$1
	file=$2
	expression=$3
	mutant="$temporary/$name.c"
	sed "$expression" "$root/src/lib/$file" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/$file"; then
		printf 'mutation changed nothing: %s\n' "$name" >&2
		exit 1
	fi
	if [ "$file" = payload_mm_authvar_presence_transaction.c ]; then
		common=$mutant
		receiver=$root/src/lib/payload_mm_authvar_presence_transaction_receiver.c
	else
		common=$root/src/lib/payload_mm_authvar_presence_transaction.c
		receiver=$mutant
	fi
	for optimization in 0 2; do
		extra="-O$optimization -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
		build "$name-O$optimization" "$extra" "$common" "$receiver"
		if "$temporary/$name-O$optimization" >/dev/null 2>&1; then
			printf 'mutation survived: %s O%s\n' "$name" "$optimization" >&2
			exit 1
		fi
	done
}

mutation dispatch-before-commit \
	payload_mm_authvar_presence_transaction_receiver.c \
	'/payload_mm_authvar_presence_transaction_dispatch_enabled/,$s/TRANSACTION_COMMITTED/TRANSACTION_PREPARED/'
mutation no-page-provenance \
	payload_mm_authvar_presence_transaction_receiver.c \
	's/, BM_MEM_RESERVED) != CB_SUCCESS/, BM_MEM_TABLE) != CB_SUCCESS/'
mutation wrong-owner-word \
	payload_mm_authvar_presence_transaction_receiver.c \
	'/payload_mm_authvar_presence_transaction_dispatch(/,$s/\&slot->dispatch_owner/\&slot->reserved[0]/'
mutation launder-abort-state \
	payload_mm_authvar_presence_transaction_receiver.c \
	's/abort_prepared(slot, \&binding, TRANSACTION_PREPARING)/abort_prepared(slot, \&binding, __atomic_load_n(\&slot->state, __ATOMIC_ACQUIRE))/'
mutation no-context-tail-scrub \
	payload_mm_authvar_presence_transaction_receiver.c \
	's/scrub(slot->context, sizeof(slot->context));/(void)slot->context;/'
mutation no-rendezvous-generation \
	payload_mm_authvar_presence_transaction_receiver.c \
	's/invocation->rendezvous_generation == invocation->smi_generation/true/'
mutation no-rendezvous-proof \
	payload_mm_authvar_presence_transaction_receiver.c \
	'/nonzero(invocation->rendezvous_proof/,+1c\
\t\ttrue \&\&'
mutation terminal-stale-ack \
	payload_mm_authvar_presence_transaction_receiver.c \
	'/static void terminal_publish/,/^}/s/__atomic_store_n(\&slot->ack_published, 0U, __ATOMIC_RELEASE);/(void)slot->ack_published;/'
mutation dirty-provision-fail-stop \
	payload_mm_authvar_presence_transaction_receiver.c \
	'/static __noreturn void provisioning_fail_stop/,/^}/s/callback(context);/(void)callback; (void)context; slot->policy.fail_stop(slot->policy.context);/'
mutation no-ack-generation \
	payload_mm_authvar_presence_transaction.c \
	's/ack->binding.generation == binding->generation/true/'
mutation no-ack-transaction \
	payload_mm_authvar_presence_transaction.c \
	's/ack->binding.transaction_id == binding->transaction_id/true/'
mutation no-ack-capability \
	payload_mm_authvar_presence_transaction.c \
	's/!memcmp(ack->binding.capability, binding->capability,/!memcmp(binding->capability, binding->capability,/'
mutation no-backing-status \
	payload_mm_authvar_presence_transaction.c \
	's/ack->backing_status ==/(ack->backing_status == ack->backing_status || ack->backing_status ==/;
	 s/PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_TRANSFERRED) \&\&/PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_TRANSFERRED)) \&\&/'
mutation no-saved-rax \
	payload_mm_authvar_presence_transaction.c \
	'/saved_rax == payload_mm_authvar_presence_transaction_rax/,+1c\
\t\tsizeof(saved_rax) == sizeof(uint64_t);'

if grep -R -Eiq '(^|[^[:alnum:]_])(mor|outb|apm|lb_new_record|coreboot_table)([^[:alnum:]_]|$)' \
	"$root/src/include/boot/payload_mm_authvar_presence_transaction.h" \
	"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
	"$root/src/lib/payload_mm_authvar_presence_transaction_receiver.c"; then
	printf '%s\n' 'presence transaction gained a public route or dependency' >&2
	exit 1
fi
if grep -Eq 'CONFIG_MAX_CPUS|authority_install|authority_close|transaction_prepared|authority_installed' \
	"$root/src/include/boot/payload_mm_authvar_presence_transaction.h" \
	"$root/src/include/boot/payload_mm_authvar_presence_producer.h" \
	"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
	"$root/src/lib/payload_mm_authvar_presence_transaction_receiver.c" \
	"$root/src/lib/payload_mm_authvar_presence_producer.c"; then
	printf '%s\n' 'presence transaction regained a legacy path or CPU assumption' >&2
	exit 1
fi
if grep -R -Eq --include=Kconfig \
	'select[[:space:]]+PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION' "$root/src"; then
	printf '%s\n' 'presence transaction gained a selector' >&2
	exit 1
fi
if ! awk '$1 == "config" { inside = $2 == \
		"PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION"; next }
	inside && $1 == "default" && $2 == "n" { found = 1 }
	inside && (($1 == "bool" && NF > 1) || $1 == "prompt" ||
		($1 == "default" && $2 == "y")) { bad = 1 }
	END { exit !(found && !bad) }' "$root/src/lib/Kconfig"; then
	printf '%s\n' 'presence transaction is not hidden and default-off' >&2
	exit 1
fi

printf '%s\n' 'Payload-MM authenticated-variable presence transaction tests: PASS'
