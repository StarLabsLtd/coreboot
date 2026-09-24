#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/include"
cat > "$tmp/include/config.h" <<EOF
#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0
#define CONFIG_MAX_CPUS 4
#define CONFIG_SMMSTORE 0
#define CONFIG_SMMSTORE_FULL_FLASH_ACCESS 0
#define CONFIG_SMMSTORE_BLOCK_SIZE 65536
#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 0
#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 0
#define CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION 0
EOF

build_test()
{
	name=$1
	optimization=$2
	source=$3
	shift 3
	cc -std=gnu11 -O"$optimization" -g -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes -fno-builtin \
		-fno-pie -no-pie "$@" -D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$tmp/include" \
		"$root/tests/lib/payload_mm_authvar_smm_bootstrap_test.c" \
		"$source" "$root/src/lib/payload_mm_authvar.c" -o "$tmp/$name"
}

for opt in 0 2; do
	build_test "test-$opt" "$opt" \
		"$root/src/lib/payload_mm_authvar_smm_bootstrap.c" \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-fno-omit-frame-pointer
	for mode in success repeat restricted short alias unaligned overflow \
		receipt-revision receipt-size receipt-reserved receipt-smram arena-empty \
		arena-outside store-failure flash-absent flash-size flash-sector \
		small-smram transport-alias context-overlap concurrent two-taker \
		pre-take post-take callback-retake state-mutation torn-publication \
		receipt-owner receipt-generation \
		mutate1 mutate2 mutate3 mutate4 mutate5; do
		ASAN_OPTIONS=detect_leaks=0 "$tmp/test-$opt" "$mode"
	done
done

objdump -d "$tmp/test-2" | awk '
	/<scrub>:/ { inside = 1 }
	inside { print }
	inside && /^$/ { exit }
' > "$tmp/scrub.disassembly"
if ! grep -Eq 'mov[bwlq].*\$0x0' "$tmp/scrub.disassembly"; then
	echo "optimized scrub has no explicit zero store" >&2
	exit 1
fi

build_test thread-sanitized 2 \
	"$root/src/lib/payload_mm_authvar_smm_bootstrap.c" \
	-fsanitize=thread -fno-sanitize-recover=all -fno-omit-frame-pointer
TSAN_OPTIONS=halt_on_error=1 "$tmp/thread-sanitized" concurrent
TSAN_OPTIONS=halt_on_error=1 "$tmp/thread-sanitized" two-taker

for mutation in 1 2 3 4 5; do
	occurrence=$((mutation + 1))
	mutant="$tmp/mutant-$mutation.c"
	awk -v target="$occurrence" '
		{
			if (index($0, "memcmp(&input, bootstrap")) {
				seen++
				if (seen == target)
					sub("memcmp\\(&input, bootstrap, sizeof\\(input\\)\\)",
						"false")
			}
			print
		}' "$root/src/lib/payload_mm_authvar_smm_bootstrap.c" > "$mutant"
	build_test "mutant-$mutation" 2 "$mutant"
	if "$tmp/mutant-$mutation" "mutate$mutation" >/dev/null 2>&1; then
		echo "mutation-boundary mutant $mutation survived" >&2
		exit 1
	fi
done

for scrubbed in receipt input frozen; do
	scrub_mutant="$tmp/scrub-mutant-$scrubbed.c"
	awk -v target="scrub(&$scrubbed, sizeof($scrubbed));" '
		index($0, target) { next }
		{ print }
	' "$root/src/lib/payload_mm_authvar_smm_bootstrap.c" > "$scrub_mutant"
	build_test "scrub-mutant-$scrubbed" 2 "$scrub_mutant"
	if "$tmp/scrub-mutant-$scrubbed" success >/dev/null 2>&1; then
		echo "$scrubbed scrub mutant survived" >&2
		exit 1
	fi
done

state_mutant="$tmp/state-mutant.c"
awk '
	{
		if (index($0, "smm_payload_mm_authvar_arena_receipt_consumed()")) {
			seen++
			if (seen == 2)
				sub("!smm_payload_mm_authvar_arena_receipt_consumed\\(\\)",
					"false")
		}
		print
	}' "$root/src/lib/payload_mm_authvar_smm_bootstrap.c" > "$state_mutant"
build_test state-mutant 2 "$state_mutant"
if "$tmp/state-mutant" state-mutation >/dev/null 2>&1; then
	echo "consumed-state boundary mutant survived" >&2
	exit 1
fi

publication_mutant="$tmp/publication-mutant.c"
awk '
	{
		if (index($0, "memcmp(receipt.owner, input.seal_channel.capability,")) {
			print "\t    false ||"
			getline
			next
		}
		print
	}' "$root/src/lib/payload_mm_authvar_smm_bootstrap.c" > "$publication_mutant"
build_test publication-mutant 2 "$publication_mutant"
if "$tmp/publication-mutant" torn-publication >/dev/null 2>&1; then
	echo "torn-publication mutant survived" >&2
	exit 1
fi
