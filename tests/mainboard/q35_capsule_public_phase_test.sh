#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ulimit -c 0
source=$root/src/mainboard/emulation/qemu-q35/native_service_receiver.c
extract()
{
awk '
	/^enum service_phase / { selected = 1; enumeration = 1 }
	/^bool q35_capsule_service_current\(void\)$/ ||
	/^bool q35_capsule_ram_transaction_current\(void\)$/ ||
	/^bool platform_payload_mm_authvar_service_runtime_admitted\(void\)$/ {
		selected = 1; functions++
	}
	selected { print }
	selected && enumeration && /};$/ { selected = 0; enumeration = 0; enums++ }
	selected && /^}$/ { selected = 0 }
	END { if (functions != 3 || enums != 1) exit 1 }
' "$1" > "$2"
}

compile()
{
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror "$optimization" \
		-fno-pie -no-pie -g -fno-omit-frame-pointer \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$temporary/include" "-DQ35_PUBLIC_PHASE_SOURCE=\"$1\"" \
		"$root/tests/mainboard/q35_capsule_public_phase_test.c" -o "$2"
}

extract "$source" "$temporary/phase.c"
for profile in off owner service; do
	case "$profile" in
		off) route=0; contract=0;;
		owner) route=0; contract=1;;
		service) route=1; contract=1;;
	esac
	printf '#define CONFIG_SMM_APMC_ROUTE_CAPSULE_BROKER %s\n' "$route" > "$temporary/include/config.h"
	printf '#define CONFIG_CAPSULE_BROKER_CONTRACT %s\n' "$contract" >> "$temporary/include/config.h"
	printf '%s\n' \
		'#define CONFIG_Q35_SMM_INVOCATION_NATIVE_PUBLIC_SERVICE_COMPONENT 1' >> "$temporary/include/config.h"
	for optimization in -O0 -O2; do
		compile "$temporary/phase.c" "$temporary/test"
		"$temporary/test"
		printf 'HOST actual FC/E8 phase bodies %s %s: PASS\n' "$profile" "$optimization"
	done
done

for mutation in mailbox window; do
	awk -v mutation="$mutation" '
		$0 == "bool platform_payload_mm_authvar_service_runtime_admitted(void)" { runtime = 1 }
		mutation == "mailbox" && runtime && $0 == "\t\t q35_public_service_current(SMM_APMC_AUTHVAR_SERVICE));" {
			print "\t\t q35_public_service_current(SMM_APMC_AUTHVAR_SERVICE)) ||"
			print "\t\tq35_capsule_service_current(); /* PUBLIC_MAILBOX_SEPARATION_DISCARDED */"
			count++; next
		}
		mutation == "window" && $0 == "\treturn q35_capsule_service_current() && capsule_broker_ram_window_open();" {
			print "\treturn q35_capsule_service_current(); /* PUBLIC_WINDOW_GUARD_DISCARDED */"
			count++; next
		}
		{ print }
		/^}$/ { runtime = 0 }
		END { if (count != 1) exit 1 }
	' "$source" > "$temporary/$mutation.c"
	awk '
		$0 == "bool platform_payload_mm_authvar_service_runtime_admitted(void)" { runtime = 1 }
		runtime && $0 == "\t\t q35_public_service_current(SMM_APMC_AUTHVAR_SERVICE)) ||" {
			if (getline <= 0 || $0 != "\t\tq35_capsule_service_current(); /* PUBLIC_MAILBOX_SEPARATION_DISCARDED */") exit 1
			print "\t\t q35_public_service_current(SMM_APMC_AUTHVAR_SERVICE));"; count++; next
		}
		$0 == "\treturn q35_capsule_service_current(); /* PUBLIC_WINDOW_GUARD_DISCARDED */" {
			print "\treturn q35_capsule_service_current() && capsule_broker_ram_window_open();"; count++; next
		}
		{ print }
		/^}$/ { runtime = 0 }
		END { if (count != 1) exit 1 }
	' "$temporary/$mutation.c" > "$temporary/restored.c"
	cmp "$source" "$temporary/restored.c"
	extract "$temporary/$mutation.c" "$temporary/mutant-phase.c"
	for optimization in -O0 -O2; do
		compile "$temporary/mutant-phase.c" "$temporary/mutant"
		status=0
		"$temporary/mutant" > "$temporary/negative.log" 2>&1 || status=$?
		case "$mutation" in
			mailbox) assertion='!platform_payload_mm_authvar_service_runtime_admitted()';;
			window) assertion='!q35_capsule_ram_transaction_current()';;
		esac
		if [ "$status" -ne 134 ] ||
		   ! grep -F "PUBLIC_PHASE_ASSERT: $assertion" "$temporary/negative.log" ||
		   grep -E 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' "$temporary/negative.log"; then
			printf 'Wrong public phase causal refusal %s (%s)\n' "$mutation" "$status" >&2
			exit 1
		fi
		printf 'HOST actual FC/E8 phase %s causal %s: PASS\n' "$optimization" "$mutation"
	done
done
