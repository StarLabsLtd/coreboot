#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ulimit -c 0

compile()
{
	output=$1
	source=$2
	optimization=$3
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin \
		-fno-pie -no-pie "$optimization" -g -fno-omit-frame-pointer \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/capsule_broker_ram_window_test.c" "$source" \
		"$root/src/lib/capsule_broker_endpoint.c" \
		"$root/src/lib/capsule_write_layout.c" \
		"$root/src/lib/capsule_update_backend.c" -o "$output"
}

for mode in close admission metadata; do
	awk -v mode="$mode" '
		$0 == "bool capsule_broker_execution_ready(void)" { execution = 1 }
		$0 == "static bool metadata_guard(void)" { metadata = 1 }
		mode == "close" && $0 == "\t/* No public path can undo this denial or replace the installed policy. */" {
			print
			if (getline <= 0 || $0 != "\tbroker.ram_window_closed = true;") exit 2
			print "\t/* RAM_WINDOW_CLOSE_DISCARDED */"
			count++
			next
		}
		mode == "admission" && execution && $0 == "\tif (!broker.installed || broker.closed ||" {
			if (getline <= 0 || $0 != "\t    (broker.policy.revision == CAPSULE_BROKER_POLICY_RAM_REVISION &&") exit 2
			if (getline <= 0 || $0 != "\t     !capsule_broker_ram_window_open()))") exit 2
			print "\tif (!broker.installed || broker.closed)"
			count++
			next
		}
		mode == "metadata" && metadata && $0 == "\treturn proofs->communication_reserved(proofs->context, communication," {
			print "\treturn proofs->dma_protected(proofs->context, communication,"
			print "\t\tbroker.policy.endpoint.communication_size) &&"
			print "\t\tproofs->communication_reserved(proofs->context, communication,"
			count++
			next
		}
		{ print }
		$0 == "}" { execution = 0; metadata = 0 }
		END { if (count != 1) exit 3 }
	' "$root/src/lib/capsule_broker.c" > "$temporary/$mode.c"
	awk -v mode="$mode" '
		$0 == "bool capsule_broker_execution_ready(void)" { execution = 1 }
		$0 == "static bool metadata_guard(void)" { metadata = 1 }
		mode == "close" && $0 == "\t/* RAM_WINDOW_CLOSE_DISCARDED */" {
			print "\tbroker.ram_window_closed = true;"
			count++
			next
		}
		mode == "admission" && execution && $0 == "\tif (!broker.installed || broker.closed)" {
			print "\tif (!broker.installed || broker.closed ||"
			print "\t    (broker.policy.revision == CAPSULE_BROKER_POLICY_RAM_REVISION &&"
			print "\t     !capsule_broker_ram_window_open()))"
			count++
			next
		}
		mode == "metadata" && metadata && $0 == "\treturn proofs->dma_protected(proofs->context, communication," {
			if (getline <= 0 || $0 != "\t\tbroker.policy.endpoint.communication_size) &&") exit 2
			if (getline <= 0 || $0 != "\t\tproofs->communication_reserved(proofs->context, communication,") exit 2
			print "\treturn proofs->communication_reserved(proofs->context, communication,"
			count++
			next
		}
		{ print }
		$0 == "}" { execution = 0; metadata = 0 }
		END { if (count != 1) exit 3 }
	' "$temporary/$mode.c" > "$temporary/$mode.restored.c"
	cmp "$root/src/lib/capsule_broker.c" "$temporary/$mode.restored.c"
done

for optimization in -O0 -O2; do
	compile "$temporary/normal" "$root/src/lib/capsule_broker.c" "$optimization"
	for mode in close legacy mismatch grant; do
		"$temporary/normal" "$mode"
		printf '%s\n' "RAM broker window $optimization $mode: PASS"
	done
	for mode in close admission metadata; do
		compile "$temporary/mutant" "$temporary/$mode.c" "$optimization"
		status=0
		"$temporary/mutant" close > "$temporary/mutant.log" 2>&1 || status=$?
		test "$status" -eq 134
		case "$mode" in
			close) assertion='!capsule_broker_ram_window_open()' ;;
			admission) assertion='!capsule_broker_execution_ready()' ;;
			metadata) assertion='capsule_broker_transport_ready(CAPSULE_BROKER_TRANSPORT_RAM_REVISION)' ;;
		esac
		grep -F "RAM_WINDOW_ASSERT: $assertion" "$temporary/mutant.log" > /dev/null
		if grep -E 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' "$temporary/mutant.log"; then
			exit 1
		fi
		printf '%s\n' "RAM broker window $optimization $mode exact-source causal: PASS"
	done
done
