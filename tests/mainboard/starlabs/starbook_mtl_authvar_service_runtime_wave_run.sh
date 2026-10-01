#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
binary=$1
temporary=$2
authentication=$3
test "$authentication" = auth2
export ASAN_OPTIONS=detect_leaks=0
for initiator in 0 2; do
	export RUNTIME_INITIATOR=$initiator
	for profile in ordinary auth2; do
		printf 'Runtime initiator=%s profile=%s\n' "$initiator" "$profile"
		unset RUNTIME_FAULT
		"$binary" capacity-edge
		set --
		if [ "$profile" = auth2 ]; then
			set -- "$temporary/auth2.bin" "$temporary/expected-binding.bin"
		fi
		for mode in normal mailbox-drift; do
			printf 'Runtime mode=%s\n' "$mode"
			"$binary" "$mode" "$@"
		done
		if [ "$profile" = auth2 ]; then
			"$binary" normal "$@" wrong-content
			result=0
			"$binary" normal "$temporary/auth2.bin" "$temporary/wrong-binding.bin" \
				> "$temporary/wrong-runtime-binding.log" 2>&1 || result=$?
			[ "$result" -eq 134 ]
			grep -q 'memcmp(binding.data, expected_binding' "$temporary/wrong-runtime-binding.log"
		fi
		for fault in 0 7; do
			result=0
			RUNTIME_FAULT=$fault "$binary" proof-drift "$@" \
				> "$temporary/runtime-proof-drift.log" 2>&1 || result=$?
			cat "$temporary/runtime-proof-drift.log"
			[ "$result" -eq 77 ]
			! grep -q 'provider stack line\|Assertion.*failed\|runtime error:\|Sanitizer' \
				"$temporary/runtime-proof-drift.log"
		done
		for fault in 1 2 3 4 5 6 8 9 10 11 12 13 14 15 16 17 18; do
			printf 'Runtime preexecution fault=%s\n' "$fault"
			result=0
			RUNTIME_FAULT=$fault "$binary" normal > "$temporary/runtime-fault.log" 2>&1 || result=$?
			cat "$temporary/runtime-fault.log"
			[ "$result" -eq 78 ]
			! grep -q 'provider stack line\|Assertion.*failed\|runtime error:\|Sanitizer' \
				"$temporary/runtime-fault.log"
		done
	done
done
echo 'Actual fc runtime wave and provider transaction: PASS (host hardware models)'
