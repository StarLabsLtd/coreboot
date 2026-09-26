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
	'#define CONFIG_BOOTMEM_ALIGNED_RESERVATIONS 1' \
	'#define CONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT 1' \
	> "$temporary/include/config.h"

common_flags="-std=gnu11 -Wall -Wextra -Werror -Wshadow -fno-builtin -pthread"
includes="-include $root/src/include/kconfig.h -include $root/src/include/rules.h
-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
-I$temporary/include -I$root/src -I$root/src/lib -I$root/src/include
-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
-I$root/src/arch/x86/include"

build_test()
{
	name=$1
	flags=$2
	sender=${3:-$root/src/lib/payload_mm_authvar_presence_handoff_sender.c}
	receiver=${4:-$root/src/lib/payload_mm_authvar_presence_handoff_receiver.c}
	receipt=${5:-$root/src/lib/bootmem_reservation_receipt.c}
	objects=
	for source in \
		"$root/tests/lib/payload_mm_authvar_presence_handoff_test.c" \
		"$sender" "$receiver" \
		"$root/src/lib/payload_mm_authvar_presence.c"; do
		object="$temporary/$name-$(basename "$source").o"
		# Deliberate normal flag splitting for this host-only strict harness.
		# shellcheck disable=SC2086
		${CC:-cc} $common_flags -Wconversion $flags \
			-DBOOTMEM_RECEIPT_TEST -D__TEST__ -D__COREBOOT__ \
			$includes -c "$source" -o "$object"
		objects="$objects $object"
	done
	object="$temporary/$name-receipt.o"
	# The existing compact receipt SHA implementation predates -Wconversion.
	# shellcheck disable=SC2086
	${CC:-cc} $common_flags $flags -DBOOTMEM_RECEIPT_TEST \
		-D__TEST__ -D__COREBOOT__ $includes -c "$receipt" -o "$object"
	objects="$objects $object"
	# shellcheck disable=SC2086
	${CC:-cc} -pthread $flags $objects -o "$temporary/$name"
}

run_test()
{
	name=$1
	flags=$2
	sender=${3:-$root/src/lib/payload_mm_authvar_presence_handoff_sender.c}
	receiver=${4:-$root/src/lib/payload_mm_authvar_presence_handoff_receiver.c}
	receipt=${5:-$root/src/lib/bootmem_reservation_receipt.c}

	build_test "$name" "$flags" "$sender" "$receiver" "$receipt"
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1 "$temporary/$name"
}

run_test strict-O0 '-O0'
run_test strict-O2 '-O2'
run_test sanitized-O0 \
	'-O0 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all'
run_test sanitized-O2 \
	'-O2 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all'
run_test thread-sanitized \
	'-O1 -g -fno-omit-frame-pointer -fsanitize=thread -fno-sanitize-recover=all -Wno-error=tsan'

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
	if [ "$file" = payload_mm_authvar_presence_handoff_sender.c ]; then
		sender=$mutant
		receiver=$root/src/lib/payload_mm_authvar_presence_handoff_receiver.c
	else
		sender=$root/src/lib/payload_mm_authvar_presence_handoff_sender.c
		receiver=$mutant
	fi
	for optimization in 0 2; do
		flags="-O$optimization -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
		build_test "$name-O$optimization" "$flags" "$sender" "$receiver"
		if ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
			UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/$name-O$optimization" >/dev/null 2>&1; then
			printf 'mutation survived: %s O%s\n' "$name" "$optimization" >&2
			exit 1
		fi
	done
	if [ "$name" = early-terminal-publication ]; then
		flags='-O1 -g -fno-omit-frame-pointer -fsanitize=thread -fno-sanitize-recover=all -Wno-error=tsan'
		build_test "$name-tsan" "$flags" "$sender" "$receiver"
		if "$temporary/$name-tsan" >/dev/null 2>&1; then
			printf 'mutation survived: %s TSAN\n' "$name" >&2
			exit 1
		fi
	fi
}

mutation no-endpoint-generation-binding \
	payload_mm_authvar_presence_handoff_sender.c \
	's/sealed_seed.endpoint.generation != sender.generation/!sealed_seed.endpoint.generation/'
mutation abort-delivers \
	payload_mm_authvar_presence_handoff_sender.c \
	's/HANDOFF_ABORT_REQUESTED : HANDOFF_TERMINAL/HANDOFF_TAKING : HANDOFF_TERMINAL/'
mutation mutable-caller-source \
	payload_mm_authvar_presence_handoff_sender.c \
	's/memcmp(seed, \&sealed_seed, sizeof(sealed_seed))/false/g'
mutation early-terminal-publication \
	payload_mm_authvar_presence_handoff_receiver.c \
	's/HANDOFF_CLOSING, false/HANDOFF_TERMINAL, false/'
mutation no-runtime-cpu-check \
	payload_mm_authvar_presence_handoff_receiver.c \
	's/cpu != metadata.owner_cpu || cpu >=/(cpu == metadata.owner_cpu \&\& false) || cpu >=/'
mutation no-range-proof \
	payload_mm_authvar_presence_handoff_receiver.c \
	's/!platform_payload_mm_authvar_presence_handoff_range_valid(/false \& !platform_payload_mm_authvar_presence_handoff_range_valid(/g'
mutation partial-transfer-scrub \
	payload_mm_authvar_presence_handoff_receiver.c \
	's/scrub(request, PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE);/scrub(request, sizeof(candidate));/'
mutation mutable-request \
	payload_mm_authvar_presence_handoff_receiver.c \
	's/memcmp(\&observed, request, sizeof(observed))/false/g'

for verifier in transfer mailbox; do
	legacy="$temporary/legacy-$verifier-receiver.c"
	if [ "$verifier" = transfer ]; then
		range='/candidate.transfer_receipt.bytes/,/BM_MEM_RESERVED)/'
	else
		range='/candidate.mailbox_receipt.bytes/,/BM_MEM_RESERVED)/'
	fi
	sed \
	-e '/#include <string.h>/a\
static enum cb_err legacy_presence_verify(\
\tstruct bootmem_reservation_receipt_authority *verifier,\
\tstruct bootmem_reservation_receipt *receipt, enum bootmem_type tag)\
{\
\t(void)tag;\
\treturn bootmem_reservation_receipt_verify_consume(verifier, receipt);\
}' \
	-e "$range s/bootmem_reservation_receipt_verify_consume_exact_tag(/legacy_presence_verify(/" \
		"$root/src/lib/payload_mm_authvar_presence_handoff_receiver.c" > "$legacy"
	for optimization in 0 2; do
		build_test "legacy-$verifier-O$optimization" "-O$optimization" \
			"$root/src/lib/payload_mm_authvar_presence_handoff_sender.c" \
			"$legacy"
		if "$temporary/legacy-$verifier-O$optimization" >/dev/null 2>&1; then
			printf 'mutation survived: legacy %s receipt wrapper O%s\n' \
				"$verifier" "$optimization" >&2
			exit 1
		fi
	done
done

# Compile once with stack reports and reject unexpectedly large host frames.
# Deliberate normal flag splitting.
# shellcheck disable=SC2086
${CC:-cc} $common_flags -O2 -fstack-usage -D__TEST__ -D__COREBOOT__ \
	$includes -c "$root/src/lib/payload_mm_authvar_presence_handoff_sender.c" \
	-o "$temporary/sender-stack.o"
# shellcheck disable=SC2086
${CC:-cc} $common_flags -O2 -fstack-usage -D__TEST__ -D__COREBOOT__ \
	$includes -c "$root/src/lib/payload_mm_authvar_presence_handoff_receiver.c" \
	-o "$temporary/receiver-stack.o"
awk '$2 > 2048 { print "oversized handoff stack frame: " $0; bad = 1 }
	END { exit bad }' "$temporary"/*.su
if nm -u "$temporary/sender-stack.o" "$temporary/receiver-stack.o" |
	grep -Eiq '(^|[^[:alnum:]_])(mor|outb|apm|lb_new_record|coreboot_table)([^[:alnum:]_]|$)'; then
	printf '%s\n' 'presence handoff artifact gained a forbidden dependency' >&2
	exit 1
fi

grep -qx 'ramstage-$(CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF) += \\' \
	"$root/src/lib/Makefile.mk"
grep -qx 'smm-$(CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF) += \\' \
	"$root/src/lib/Makefile.mk"
grep -qx 'test-authvar-presence-handoff:' "$root/util/testing/Makefile.mk"
if grep -R -Eiq '(^|[^[:alnum:]_])(mor|outb|apm|lb_new_record|coreboot_table)([^[:alnum:]_]|$)|select[[:space:]]+PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF' \
	"$root/src/include/boot/payload_mm_authvar_presence_handoff.h" \
	"$root/src/lib/payload_mm_authvar_presence_handoff_sender.c" \
	"$root/src/lib/payload_mm_authvar_presence_handoff_receiver.c"; then
	printf '%s\n' 'presence handoff gained a forbidden route or dependency' >&2
	exit 1
fi
if rg -q 'payload_mm_authvar_presence_handoff' "$root/src" \
	--glob '!**/include/boot/payload_mm_authvar_presence_handoff.h' \
	--glob '!**/lib/payload_mm_authvar_presence_handoff_sender.c' \
	--glob '!**/lib/payload_mm_authvar_presence_handoff_receiver.c' \
	--glob '!**/lib/Kconfig' --glob '!**/lib/Makefile.mk'; then
	printf '%s\n' 'dormant handoff became production-integrated' >&2
	exit 1
fi

"$root/tests/lib/payload_mm_authvar_presence_composed_test.sh"
printf '%s\n' 'Payload-MM authenticated-variable presence handoff tests: PASS'
