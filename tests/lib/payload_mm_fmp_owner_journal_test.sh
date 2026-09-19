#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

cases='absent success checkpoint reentry wrong-identity candidate-toctou
wrong-digest duplicate newer wrong-domain wrong-key exhaustion hash-input
program-context source-mutation factory-twice factory-toctou'

build_and_run()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/payload_mm_fmp_owner_journal_test.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_fmp_state.c" \
		"$root/src/lib/payload_mm_fmp_owner.c" \
		"$root/src/lib/payload_mm_fmp_owner_layout.c" \
		"$root/src/lib/payload_mm_fmp_owner_journal.c" \
		"$root/src/lib/payload_mm_fmp_checkpoint.c" \
		-o "$temporary/$name"
	for case_name in $cases; do
		"$temporary/$name" "$case_name"
	done
	for cut in 1 2; do
		for kind in before partial after input context; do
			"$temporary/$name" "cut-program-$kind" "$cut"
		done
		for kind in before partial after context; do
			"$temporary/$name" "cut-erase-$kind" "$cut"
		done
	done
	for cut in 1 2 3 4; do
		"$temporary/$name" cut-sync-before "$cut"
		"$temporary/$name" cut-sync-context "$cut"
	done
	"$temporary/$name" cut-advance-before 1
	"$temporary/$name" cut-advance-after 1
	"$temporary/$name" cut-advance-input 1
	"$temporary/$name" cut-advance-context 1
	if "$temporary/$name" count-read; then
		echo "read callback count unexpectedly zero" >&2
		exit 1
	else
		read_count=$?
	fi
	for cut in $(seq 1 "$read_count"); do
		for kind in before after output context; do
			"$temporary/$name" "cut-read-$kind" "$cut"
		done
	done
	if "$temporary/$name" count-anchor-read; then
		echo "anchor-read callback count unexpectedly zero" >&2
		exit 1
	else
		anchor_read_count=$?
	fi
	for cut in $(seq 1 "$anchor_read_count"); do
		for kind in before after output context; do
			"$temporary/$name" "cut-anchor-read-$kind" "$cut"
		done
	done
	if "$temporary/$name" count-hash; then
		echo "hash callback count unexpectedly zero" >&2
		exit 1
	else
		hash_count=$?
	fi
	for cut in $(seq 1 "$hash_count"); do
		for kind in before after digest input context; do
			"$temporary/$name" "cut-hash-$kind" "$cut"
		done
	done
	if "$temporary/$name" factory-count-read; then
		echo "factory read callback count unexpectedly zero" >&2
		exit 1
	else
		factory_read_count=$?
	fi
	for cut in $(seq 1 "$factory_read_count"); do
		for kind in before after output context; do
			"$temporary/$name" "factory-cut-read-$kind" "$cut"
		done
	done
	if "$temporary/$name" factory-count-erase; then
		echo "factory erase callback count unexpectedly zero" >&2
		exit 1
	else
		factory_erase_count=$?
	fi
	for cut in $(seq 1 "$factory_erase_count"); do
		for kind in before partial after context; do
			"$temporary/$name" "factory-cut-erase-$kind" "$cut"
		done
	done
	if "$temporary/$name" factory-count-program; then
		echo "factory program callback count unexpectedly zero" >&2
		exit 1
	else
		factory_program_count=$?
	fi
	for cut in $(seq 1 "$factory_program_count"); do
		for kind in before partial after input context; do
			"$temporary/$name" "factory-cut-program-$kind" "$cut"
		done
	done
	if "$temporary/$name" factory-count-sync; then
		echo "factory sync callback count unexpectedly zero" >&2
		exit 1
	else
		factory_sync_count=$?
	fi
	for cut in $(seq 1 "$factory_sync_count"); do
		for kind in before after context; do
			"$temporary/$name" "factory-cut-sync-$kind" "$cut"
		done
	done
	if "$temporary/$name" factory-count-hash; then
		echo "factory hash callback count unexpectedly zero" >&2
		exit 1
	else
		factory_hash_count=$?
	fi
	for cut in $(seq 1 "$factory_hash_count"); do
		for kind in before after digest input context; do
			"$temporary/$name" "factory-cut-hash-$kind" "$cut"
		done
	done
	if "$temporary/$name" factory-count-anchor-read; then
		echo "factory anchor-read callback count unexpectedly zero" >&2
		exit 1
	else
		factory_anchor_read_count=$?
	fi
	for cut in $(seq 1 "$factory_anchor_read_count"); do
		for kind in before after output context; do
			"$temporary/$name" "factory-cut-anchor-read-$kind" "$cut"
		done
	done
	if "$temporary/$name" factory-count-provision; then
		echo "factory provision callback count unexpectedly zero" >&2
		exit 1
	else
		factory_provision_count=$?
	fi
	for cut in $(seq 1 "$factory_provision_count"); do
		for kind in before after input context; do
			"$temporary/$name" "factory-cut-provision-$kind" "$cut"
		done
	done
}

build_and_run owner-journal-o0 -O0
build_and_run owner-journal-o2 -O2
build_and_run owner-journal-strict -O2 -Wconversion -Wsign-conversion
build_and_run owner-journal-asan -O1 -g -fsanitize=address
ASAN_OPTIONS=detect_leaks=0 build_and_run owner-journal-ubsan -O1 -g \
	-fsanitize=undefined
echo "Payload-MM FMP owner journal O0/O2/strict/ASan+UBSan cuts: PASS" \
	"($read_count read, $anchor_read_count anchor, $hash_count hash;" \
	"factory $factory_read_count read, $factory_erase_count erase," \
	"$factory_program_count program, $factory_sync_count sync," \
	"$factory_hash_count hash, $factory_anchor_read_count anchor," \
	"$factory_provision_count provision)"
