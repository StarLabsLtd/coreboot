#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_BOOTMEM_ALIGNED_RESERVATIONS 1' \
	> "$temporary/include/config.h"

run_test()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-fno-builtin -no-pie -pthread "$@" -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_presence_transaction_producer_test.c" \
		"$root/src/lib/payload_mm_authvar_presence.c" \
		"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
		"$root/src/lib/payload_mm_authvar_presence_producer.c" \
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
		"$root/src/lib/payload_mm_authvar_presence_producer.c" > "$mutant"
	if cmp -s "$mutant" \
		"$root/src/lib/payload_mm_authvar_presence_producer.c"; then
		printf 'mutation changed nothing: %s\n' "$name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$temporary/$name-O$optimization"
		"${CC:-cc}" -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
			-Wconversion -Wshadow -fno-builtin -no-pie \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-D__TEST__ -D__COREBOOT__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
			-I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_presence_transaction_producer_test.c" \
			"$root/src/lib/payload_mm_authvar_presence.c" \
			"$root/src/lib/payload_mm_authvar_presence_transaction.c" "$mutant" \
			-o "$binary"
		if ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
			printf 'mutation survived: %s O%s\n' "$name" "$optimization" >&2
			exit 1
		fi
	done
}

mutation wrong-commit-ack-decision \
	's/PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT, \&ack/PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT, \&ack/'

if grep -R -Eq --include=Kconfig \
	'select[[:space:]]+PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER' "$root/src" || \
	awk '$1 == "config" { inside = $2 == \
		"PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER"; next }
		inside && (($1 == "bool" && NF > 1) || $1 == "prompt" ||
			($1 == "default" && $2 == "y")) { bad = 1 }
		END { exit !bad }' "$root/src/lib/Kconfig"; then
	printf '%s\n' 'presence producer became selectable or selected' >&2
	exit 1
fi

if grep -Eq 'lb_new_record|outb|LB_TAG_AUTHVAR_PRESENCE_ENDPOINT.*=' \
	"$root/src/lib/payload_mm_authvar_presence_producer.c"; then
	printf '%s\n' 'presence producer gained a table writer, route, or private tag' >&2
	exit 1
fi

for local_secret in seed sealed_seed message random; do
	grep -Eq "scrub\\(&?$local_secret, sizeof\\($local_secret\\)\\);" \
		"$root/src/lib/payload_mm_authvar_presence_producer.c" || {
		printf 'missing local secret scrub: %s\n' "$local_secret" >&2
		exit 1
	}
done
grep -Fq 'static __noinline void scrub' \
	"$root/src/lib/payload_mm_authvar_presence_producer.c"
grep -Fq '__asm__ __volatile__' \
	"$root/src/lib/payload_mm_authvar_presence_producer.c"

# Exercise the production constants through the real bootmem validator and
# allocator rather than relying only on the producer's strict unit stub.
bootmem_config="$root/build/tests/tests/lib/bootmem-aligned-reservation-test"
make -C "$root" build-tests/lib/bootmem-aligned-reservation-test >/dev/null
for profile in o0 o2 asan ubsan; do
	case "$profile" in
	o0) bootmem_flags='-O0' ;;
	o2) bootmem_flags='-O2' ;;
	asan) bootmem_flags='-O1 -fsanitize=address -fno-omit-frame-pointer' ;;
	ubsan) bootmem_flags='-O1 -fsanitize=undefined -fno-omit-frame-pointer' ;;
	esac
	# Flags deliberately undergo normal compiler argument splitting.
	# shellcheck disable=SC2086
	"${CC:-cc}" -std=gnu23 -Wall -Wextra -Werror -Wundef \
		-Wno-unused-parameter -Wno-sign-compare -Wstrict-prototypes \
		-fno-builtin -fno-pie -fno-pic $bootmem_flags \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -D__TEST_SRCOBJ__ \
		-DBOOTMEM_RECEIPT_TEST -include "$root/src/include/kconfig.h" \
		-include "$root/tests/lib/bootmem_reservation_receipt_config.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$bootmem_config" -I"$root/tests/include/mocks" \
		-I"$root/tests/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$root/build/tests" \
		"$root/tests/lib/bootmem_aligned_reservation_test.c" \
		"$root/src/lib/bootmem.c" \
		"$root/src/lib/bootmem_reservation_receipt.c" \
		"$root/src/lib/memrange.c" "$root/src/device/device_util.c" \
		-no-pie -o "$temporary/bootmem-$profile"
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/bootmem-$profile" presence-producer-contract
done

printf '%s\n' 'Payload-MM authenticated-variable presence producer tests: PASS'
