#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_FAIL_STOP_PLATFORM 1' \
	'#define CONFIG_SMM_INVOCATION_EVIDENCE 1' \
	'#define CONFIG_SMM_INVOCATION_ENTRY 1' \
	'#define CONFIG_SMM_INVOCATION_INTEL_ADAPTER 1' \
	> "$temporary/include/config.h"

build()
{
	output=$1
	flags=$2
	entry=${3:-$root/src/cpu/x86/smm_invocation_entry.c}
	# Deliberate normal flag splitting for this strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-pthread $flags -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/cpu/x86/smm_invocation_entry_adapter_test.c" \
		"$entry" "$root/src/cpu/x86/smm_invocation_evidence.c" \
		"$root/src/cpu/x86/smm_invocation_evidence_loader.c" \
		"$root/src/cpu/x86/smm/save_state_geometry.c" \
		"$root/src/soc/intel/common/block/smm/invocation_adapter.c" \
		-o "$temporary/$output"
}

run_focused()
{
	"$@" --ticket-focused
}

for optimization in 0 2; do
	build "plain-O$optimization" "-O$optimization"
	run_focused "$temporary/plain-O$optimization"
	build "sanitize-O$optimization" \
		"-O$optimization -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
	run_focused env ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/sanitize-O$optimization"
done

for mutant_name in command-publication reserved-validation; do
	mutant="$temporary/$mutant_name.c"
	case "$mutant_name" in
	command-publication)
		sed 's/ticket_snapshot.command = snapshot.command;/ticket_snapshot.command = 0U;/' \
			"$root/src/cpu/x86/smm_invocation_entry.c" > "$mutant"
		;;
	reserved-validation)
		sed 's/!memcmp(ticket->reserved, zero, sizeof(zero))/(!memcmp(ticket->reserved, zero, sizeof(zero)) || true)/' \
			"$root/src/cpu/x86/smm_invocation_entry.c" > "$mutant"
		;;
	esac
	for optimization in 0 2; do
		build "$mutant_name-O$optimization" "-O$optimization" "$mutant"
		if run_focused "$temporary/$mutant_name-O$optimization" >/dev/null 2>&1; then
			printf 'surviving mutant: %s O%s\n' "$mutant_name" \
				"$optimization" >&2
			exit 1
		fi
	done
done

${CC:-cc} -m32 -march=i686 -std=gnu11 -Wall -Wextra -Werror -Wshadow \
	-ffreestanding -fno-builtin -D__TEST__ -D__COREBOOT__ \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" \
	-c "$root/src/cpu/x86/smm_invocation_entry.c" -o "$temporary/entry-32.o"
if nm -u "$temporary/entry-32.o" | grep -q '__atomic_'; then
	printf '%s\n' 'ticket change gained a 32-bit libatomic dependency' >&2
	exit 1
fi

printf '%s\n' 'SMM invocation entry ticket tests passed'
