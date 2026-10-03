#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
task_root=/home/sean/capsule-report-local-floor-cold-native.2qHfAW
task_local=$task_root/codec-local/cold-report-store.9XUJH2
task_signature=$task_root/codec-signature/cold-report-store.deef4n
task_guid=$(sed -n 's/^CONFIG_DRIVERS_EFI_MAIN_FW_GUID="\(.*\)"$/\1/p' "$task_local/producer.config")
test -n "$task_guid"
test "$task_guid" = "$(sed -n 's/^CONFIG_DRIVERS_EFI_MAIN_FW_GUID="\(.*\)"$/\1/p' "$task_signature/producer.config")"
test "$(wc -c < "$task_root/codec-local.bin")" -eq 94
test "$(wc -c < "$task_root/codec-signature.bin")" -eq 94
for mode in local signature; do
	if test "$mode" = local; then
		task_check=$task_local/check
		task_store=$task_signature/store.bin
	else
		task_check=$task_signature/check
		task_store=$task_local/store.bin
	fi
	if ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$task_check" "$task_store" "$task_guid" > "$task_root/opposite-$mode.log" 2>&1; then
		exit 2
	else
		task_status=$?
	fi
	test "$task_status" -eq 1
	test ! -s "$task_root/opposite-$mode.log"
	printf '%s rejects the opposite real report with exact status %s, no sanitizer trace\n' \
		"$mode" "$task_status"
done
