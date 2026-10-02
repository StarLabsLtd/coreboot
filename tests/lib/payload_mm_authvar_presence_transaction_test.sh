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

# Exercise the receiver against the real one-shot verifier, whose consumed
# terminal marker deliberately differs from the fault-injection mock.
# Compile the inherited receipt implementation with its own serialization
# warning policy without weakening the receiver's strict warnings.
for source in payload_mm_authvar_presence_transaction \
	payload_mm_authvar_presence_transaction_receiver; do
	# Deliberate host harness flag splitting.
	# shellcheck disable=SC2086
	${CC:-cc} $flags -O2 -D__TEST__ -D__COREBOOT__ -DREAL_RECEIPT \
		$includes -c "$root/src/lib/$source.c" \
		-o "$temporary/real-$source.o"
done
# Deliberate host harness flag splitting.
# shellcheck disable=SC2086
${CC:-cc} $flags -Wno-unused-function -O2 -D__TEST__ -D__COREBOOT__ \
	-DREAL_RECEIPT $includes -c \
	"$root/tests/lib/payload_mm_authvar_presence_transaction_test.c" \
	-o "$temporary/real-test.o"
# shellcheck disable=SC2086
${CC:-cc} $flags -Wno-conversion -O2 -D__TEST__ -D__COREBOOT__ \
	-DREAL_RECEIPT $includes -c \
	"$root/src/lib/bootmem_reservation_receipt.c" \
	-o "$temporary/real-receipt-impl.o"
${CC:-cc} -pthread "$temporary"/real-*.o -o "$temporary/real-receipt"
"$temporary/real-receipt"

# The generic transaction is gated on a 16 KiB SMM stack. Bound the actual
# 32-bit receipt-verification call chain below 4 KiB, leaving at least 12 KiB
# for the future entry/provider chain. That live slice must independently keep
# at least 4 KiB of emergency margin.
# Deliberate host harness flag splitting.
# shellcheck disable=SC2086
for source in payload_mm_authvar_presence_transaction \
	payload_mm_authvar_presence_transaction_receiver; do
	${CC:-cc} $flags -Os -m32 -ffreestanding -fstack-usage \
		-D__TEST__ -D__COREBOOT__ $includes -c \
		"$root/src/lib/$source.c" -o "$temporary/$source-32.o"
done
# The receipt implementation has inherited explicit integer serialization;
# its own gate covers those conversions. Compile its production 32-bit shape
# here so stack and linkage changes cannot escape this transaction budget.
# shellcheck disable=SC2086
${CC:-cc} $flags -Wno-conversion -Os -m32 -ffreestanding -fstack-usage \
	-fno-inline \
	-D__TEST__ -D__COREBOOT__ $includes -c \
	"$root/src/lib/bootmem_reservation_receipt.c" \
	-o "$temporary/bootmem_reservation_receipt-32.o"
if nm -u "$temporary"/*-32.o | grep -Eq '__atomic|libatomic'; then
	printf '%s\n' 'presence transaction chain gained a libatomic dependency' >&2
	exit 1
fi
stack_value()
{
	awk -F '\t' -v name="$2" '$1 ~ (name "$") { print $2 }' "$1"
}
receiver_su="$temporary/payload_mm_authvar_presence_transaction_receiver-32.su"
receipt_su="$temporary/bootmem_reservation_receipt-32.su"
dispatch_stack=$(stack_value "$receiver_su" \
	payload_mm_authvar_presence_transaction_dispatch)
verify_stack=$(stack_value "$receipt_su" \
	bootmem_reservation_receipt_verify_consume_exact_tag)
mac_stack=$(stack_value "$receipt_su" bootmem_reservation_receipt_mac)
finish_stack=$(stack_value "$receipt_su" sha_finish)
update_stack=$(stack_value "$receipt_su" sha_update)
transform_stack=$(stack_value "$receipt_su" transform)
for value in "$dispatch_stack" "$verify_stack" "$mac_stack" \
	"$finish_stack" "$update_stack" "$transform_stack"; do
	test -n "$value"
done
transaction_chain_stack=$((dispatch_stack + verify_stack + mac_stack +
	finish_stack + update_stack + transform_stack))
test "$transaction_chain_stack" -le 4096
grep -q 'depends on SMM_MODULE_STACK_SIZE >= 0x4000' "$root/src/lib/Kconfig"

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
	'/bootmem_reservation_receipt_verify_consume_exact_tag/,+1s/BM_MEM_RESERVED/BM_MEM_TABLE/'
mutation stale-policy-revision \
	payload_mm_authvar_presence_transaction_receiver.c \
	's/policy->revision ==/policy->revision !=/'
mutation no-verifier-canonical-scrub \
	payload_mm_authvar_presence_transaction_receiver.c \
	'/status = bootmem_reservation_receipt_verify_consume_exact_tag/,+4s/scrub(\&slot->page_verifier, sizeof(slot->page_verifier));/(void)slot->page_verifier;/'
mutation empty-dispatch-takes-owner \
	payload_mm_authvar_presence_transaction_receiver.c \
	'0,/state == TRANSACTION_EMPTY || state == TRANSACTION_PROVISIONING ||/s//false || false ||/'
mutation wrong-owner-word \
	payload_mm_authvar_presence_transaction_receiver.c \
	'/payload_mm_authvar_presence_transaction_dispatch(/,$s/\&slot->dispatch_owner/\&slot->reserved[0]/'
mutation no-owner-loss-guard \
	payload_mm_authvar_presence_transaction_receiver.c \
	'/static __noreturn void terminal_fail_stop/,$s/if (__atomic_load_n(\&slot->dispatch_owner, __ATOMIC_ACQUIRE) != 1U)/if (false)/'
mutation no-prepare-complete-state \
	payload_mm_authvar_presence_transaction_receiver.c \
	'/static bool dispatch_snapshot_unchanged/,/^}/s/__atomic_load_n(\&slot->state, __ATOMIC_ACQUIRE) == state/((void)state, true)/'
mutation no-terminal-complete-state \
	payload_mm_authvar_presence_transaction_receiver.c \
	'/static bool terminal_snapshot_unchanged/,/^}/s/__atomic_load_n(\&slot->state, __ATOMIC_ACQUIRE) == state/((void)state, true)/'
mutation launder-abort-state \
	payload_mm_authvar_presence_transaction_receiver.c \
	'/static __always_inline enum cb_err terminal_abort/,/^}/s/TRANSACTION_ABORTING, false/TRANSACTION_ABORTED, false/'
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
	'/static void publish_ack/,/^}/s/__atomic_store_n(\&slot->ack_published, 1U, __ATOMIC_RELEASE);/(void)slot->ack_published;/'
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
mutation no-saved-value \
	payload_mm_authvar_presence_transaction.c \
	'/saved_value == payload_mm_authvar_presence_transaction_result/,+1c\
\t\tsizeof(saved_value) == sizeof(uint64_t);'

mutation no-proof-callback-protection \
	payload_mm_authvar_presence_transaction_receiver.c \
	'/payload_mm_authvar_presence_transaction_provision/,/TRANSACTION_PROVISIONING/s/(const void \*)(uintptr_t)storage_is_protected/(const void *)(uintptr_t)p.prepare/'
for callback in prepare commit abort dma_protected claim_invocation \
	complete_invocation fail_stop; do
	other=prepare
	if [ "$callback" = prepare ]; then
		other=commit
	fi
	mutation "no-$callback-protection" \
		payload_mm_authvar_presence_transaction_receiver.c \
		"/payload_mm_authvar_presence_transaction_provision/,/TRANSACTION_PROVISIONING/s/(const void \*)(uintptr_t)p.$callback)/(const void *)(uintptr_t)p.$other)/"
done

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
if ! sh "$root/tests/lib/payload_mm_authvar_presence_selector_check.sh" \
	"$root" PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION; then
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
