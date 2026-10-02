#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

test "$#" -eq 2 || exit 2
root=$1
symbol=$2
case "$symbol" in
PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY|PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER|\
PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION|PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION|\
PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER) ;;
*) exit 2 ;;
esac

files=$(rg -l --glob Kconfig "select[[:space:]]+$symbol([[:space:]]|$)" "$root/src" ||
	test "$?" -eq 1)
printf '%s\n' "$files" | while IFS= read -r file; do
	test -n "$file" || continue
	if test "$file" != "$root/src/mainboard/emulation/qemu-q35/Kconfig"; then
		echo "unexpected $symbol selector in $file" >&2
		exit 1
	fi
	awk -v symbol="$symbol" '
		$1 == "config" || $1 == "menuconfig" {
			inside = $2 == "Q35_SMM_INVOCATION_NATIVE_SERVICE_COMPONENT"
			if (inside) blocks++
		}
		inside && $1 == "default" {
			if ($2 == "n" && NF == 2) disabled++
			else bad = 1
		}
		inside && $1 == "depends" && $2 == "on" &&
			$3 == "Q35_SMM_INVOCATION_NATIVE_CAUSE_COMPONENT" && NF == 3 {
			cause++
		}
		inside && $1 == "select" &&
			$2 == "PAYLOAD_MM_AUTHVAR_SERVICE_BOOTSTRAP_ONLY" && NF == 2 {
			service++
		}
		inside && $1 == "select" && $2 ~ /PRESENCE_(ARM|ROUTE_SESSION|LIFECYCLE)/ {
			bad = 1
		}
		$1 == "select" && $2 == symbol {
			if (!inside || NF != 2) bad = 1
			selected++
		}
		END {
			exit !(blocks == 1 && disabled == 1 && cause == 1 &&
				service == 1 && selected == 1 && !bad)
		}' "$file" || {
		echo "unbounded Q35 service-only $symbol selector" >&2
		exit 1
	}
done
