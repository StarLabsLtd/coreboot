#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
check="$root/tests/lib/payload_mm_authvar_presence_selector_check.sh"
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/src/mainboard/emulation/qemu-q35" "$temporary/src/lib"
config="$temporary/src/mainboard/emulation/qemu-q35/Kconfig"

for suffix in AUTHORITY PRODUCER PUBLICATION TRANSACTION TUPLE_SENDER; do
	symbol=PAYLOAD_MM_AUTHVAR_PRESENCE_$suffix
	printf '%s\n' \
		'config Q35_SMM_INVOCATION_NATIVE_SERVICE_COMPONENT' \
		' bool "Controlled component"' \
		' default n' \
		' depends on Q35_SMM_INVOCATION_NATIVE_CAUSE_COMPONENT' \
		' select PAYLOAD_MM_AUTHVAR_SERVICE_BOOTSTRAP_ONLY' \
		" select $symbol" > "$temporary/bounded"
	cp "$temporary/bounded" "$config"
	sh "$check" "$temporary" "$symbol"
	for mutant in default missing_service missing_cause owner menuconfig grant duplicate wrong_file; do
		cp "$temporary/bounded" "$config"
		case "$mutant" in
		default) sed 's/default n/default y/' "$temporary/bounded" > "$config" ;;
		missing_service) sed '/select PAYLOAD_MM_AUTHVAR_SERVICE_BOOTSTRAP_ONLY/d' \
			"$temporary/bounded" > "$config" ;;
		missing_cause) sed '/depends on Q35_SMM_INVOCATION_NATIVE_CAUSE_COMPONENT/d' \
			"$temporary/bounded" > "$config" ;;
		owner) sed 's/config Q35_SMM_INVOCATION_NATIVE_SERVICE_COMPONENT/config OTHER/' \
			"$temporary/bounded" > "$config" ;;
		menuconfig) sed "/select $symbol/d" "$temporary/bounded" > "$config"
			printf 'menuconfig OTHER\n bool "Other"\n select %s\n' "$symbol" >> "$config" ;;
		grant) printf '%s\n' ' select PAYLOAD_MM_AUTHVAR_PRESENCE_ARM' >> "$config" ;;
		duplicate) printf ' select %s\n' "$symbol" >> "$config" ;;
		wrong_file) cp "$config" "$temporary/src/lib/Kconfig" ;;
		esac
		status=0
		sh "$check" "$temporary" "$symbol" >/dev/null 2>&1 || status=$?
		test "$status" -eq 1 || {
			echo "$suffix $mutant guard did not reject: $status" >&2
			exit 1
		}
		: > "$temporary/src/lib/Kconfig"
	done
done
printf '%s\n' 'Service-only selector guard tests: PASS'
