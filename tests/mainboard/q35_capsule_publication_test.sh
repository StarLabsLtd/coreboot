#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_Q35_SMM_CAPSULE_BROKER_SERVICE 1' > "$temporary/include/config.h"
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ulimit -c 0
source=$root/src/mainboard/emulation/qemu-q35/native_service_sender.c
receiver=$root/src/mainboard/emulation/qemu-q35/native_service_receiver.c
extract()
{
awk '
	/^static enum cb_err capsule_publication_ready\(/ { selected = 1; count++ }
	selected { print }
	selected && /^}$/ { selected = 0 }
	END { if (count != 1 || selected) exit 1 }
' "$1" > "$temporary/publication.c"
awk '
	$0 == "\tif (!claim_current() || memcmp(frame, &request, sizeof(request))) {" ||
	/REQUEST_COMPARISON_DISCARDED/ { selected = 1; count++ }
	selected && /^\tsnapshot.maximum_cpus/ { selected = 0 }
	selected { print }
	END { if (count != 1 || selected) exit 1 }
' "$2" > "$temporary/completion.c"
}
compile()
{
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror "$optimization" \
		-fno-pie -no-pie -g -fno-omit-frame-pointer \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$temporary/include" "-DQ35_PUBLICATION_SOURCE=\"$temporary/publication.c\"" \
		"-DQ35_COMPLETION_SOURCE=\"$temporary/completion.c\"" \
		"$root/tests/mainboard/q35_capsule_publication_test.c" -o "$temporary/test"
}
extract "$source" "$receiver"
for optimization in -O0 -O2; do
	compile
	"$temporary/test"
	printf 'HOST actual publication callback drift matrix %s: PASS\n' "$optimization"
done
for mutation in late request; do
	if test "$mutation" = late; then
		original=$source
		anchor='\t    (uintptr_t)publication.record != (uintptr_t)publication.header +'
		continuation='\t\tsizeof(*publication.header) + publication.header->table_bytes ||'
		printf '%b\n' "$anchor" "$continuation" > "$temporary/old-lines"
		printf '%b\n' '\t    false || /* LATE_RECORD_GUARD_DISCARDED */' > "$temporary/new-lines"
	else
		original=$receiver
		printf '%b\n' '\tif (!claim_current() || memcmp(frame, &request, sizeof(request))) {' > "$temporary/old-lines"
		printf '%b\n' '\tif (!claim_current()) { /* REQUEST_COMPARISON_DISCARDED */' > "$temporary/new-lines"
	fi
	# Exact byte replacement and inverse full-TU equality, not a broad regex edit.
	awk -v old="$temporary/old-lines" -v new="$temporary/new-lines" '
		BEGIN { while ((getline line < old) > 0) expected[++n] = line;
			while ((getline line < new) > 0) replacement[++m] = line }
		$0 == expected[1] {
			for (i = 2; i <= n; i++) if (getline <= 0 || $0 != expected[i]) exit 1;
			for (i = 1; i <= m; i++) print replacement[i]; count++; next
		}
		{ print }
		END { if (count != 1) exit 1 }
	' "$original" > "$temporary/mutant.c"
	awk -v old="$temporary/new-lines" -v new="$temporary/old-lines" '
		BEGIN { while ((getline line < old) > 0) expected[++n] = line;
			while ((getline line < new) > 0) replacement[++m] = line }
		$0 == expected[1] {
			for (i = 2; i <= n; i++) if (getline <= 0 || $0 != expected[i]) exit 1;
			for (i = 1; i <= m; i++) print replacement[i]; count++; next
		}
		{ print }
		END { if (count != 1) exit 1 }
	' "$temporary/mutant.c" > "$temporary/restored.c"
	cmp "$original" "$temporary/restored.c"
	if test "$mutation" = late; then
		extract "$temporary/mutant.c" "$receiver"
		assertion=late_record_refused
	else
		extract "$source" "$temporary/mutant.c"
		assertion=request_mutation_refused
	fi
	for optimization in -O0 -O2; do
		compile
		status=0
		"$temporary/test" > "$temporary/negative.log" 2>&1 || status=$?
		if test "$status" != 134 ||
		    ! grep -Fq "PUBLICATION_ASSERT: $assertion" "$temporary/negative.log" ||
		    grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' "$temporary/negative.log"; then
			cat "$temporary/negative.log" >&2
			exit 1
		fi
		printf 'HOST exact %s guard-discard %s targeted refusal: PASS\n' "$mutation" "$optimization"
	done
done
