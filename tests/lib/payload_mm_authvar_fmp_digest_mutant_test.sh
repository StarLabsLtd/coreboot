#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
# HOST-modeled options, serialized like the unit-test configuration overrides.
while IFS='=' read -r config_key config_value; do
	printf '#define %s %s\n' "$config_key" "$config_value"
done > "$temporary/include/config.h" <<'EOF'
CONFIG_DEFAULT_CONSOLE_LOGLEVEL=0
CONFIG_PAYLOAD_MM_FMP_OWNER_AUTHVAR=1
CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT=1
CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR=1
EOF

compile_mutant()
{
	name=$1
	mutant=$2
	optimization=$3
	test_case=$4
	binary="$temporary/$name-O$optimization"
	case "$name" in
	delete-digest|delete-digest-reclaim|size-zero|size-minus-one)
		anchor='^static bool control_snapshot('
		boundary='^static bool control_unchanged('
		;;
	early-clear|late-activation)
		anchor='^static uint64_t fmp_state_transaction('
		boundary='^uint64_t payload_mm_authvar_fmp_state_transaction('
		;;
	esac
	scope="/$anchor/,/$boundary/"
	for source in "$source_file" "$mutant"; do
		if [ "$(sed -n "/$anchor/p" "$source" | wc -l)" -ne 1 ] ||
		   [ "$(sed -n "/$boundary/p" "$source" | wc -l)" -ne 1 ]; then
			echo "ERROR: $name active source scope is not unique" >&2
			exit 1
		fi
	done
	sed -n "$scope p" "$source_file" > "$temporary/original-body"
	sed -n "$scope p" "$mutant" > "$temporary/mutant-body"
	sed "$scope d" "$source_file" > "$temporary/original-outside"
	sed "$scope d" "$mutant" > "$temporary/mutant-outside"
	if cmp -s "$temporary/original-body" "$temporary/mutant-body" ||
	   ! cmp -s "$temporary/original-outside" "$temporary/mutant-outside"; then
		echo "ERROR: $name did not change only its active source body" >&2
		exit 1
	fi

	"${HOSTCC:-cc}" -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wshadow -Wstrict-prototypes -fno-builtin \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_executor_test.c" \
		"$root/tests/lib/payload_mm_authvar_fmp_executor_stubs.c" \
		"$mutant" \
		"$root/src/lib/payload_mm_authvar_coordinator.c" \
		"$root/src/lib/payload_mm_authvar_service.c" \
		"$root/src/lib/payload_mm_authvar_set_preflight.c" \
		"$root/src/lib/payload_mm_authvar_controlled_mode.c" \
		"$root/src/lib/payload_mm_authvar_view.c" \
		"$root/src/lib/payload_mm_authvar_authority.c" \
		"$root/src/lib/payload_mm_authvar_candidate.c" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
		"$root/src/lib/payload_mm_authvar_mode.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_authvar_route.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" \
		"$root/src/lib/payload_mm_fmp_state.c" \
		"$root/src/lib/payload_mm_fmp_owner.c" -o "$binary"
	if ASAN_OPTIONS=detect_leaks=1 "$binary" "$test_case" \
		>"$temporary/$name-O$optimization.log" 2>&1; then
		echo "ERROR: $name O$optimization mutant survived" >&2
		cat "$temporary/$name-O$optimization.log" >&2
		exit 1
	fi
}

source_file="$root/src/lib/payload_mm_authvar_executor.c"
delete_digest="$temporary/executor-delete-digest.c"
size_zero="$temporary/executor-size-zero.c"
size_minus_one="$temporary/executor-size-minus-one.c"
early_clear="$temporary/executor-early-clear.c"
late_activation="$temporary/executor-late-activation.c"

awk '
	/^static bool control_snapshot\(/ { in_control = 1 }
	/^static bool control_unchanged\(/ { in_control = 0 }
	in_control && index($0, "if (state->fmp_record_active &&") {
		print "\tif (false)"
		skip = 1
		changed++
		next
	}
	skip && $0 ~ /^[[:space:]]*return false;/ {
		print
		skip = 0
		next
	}
	!skip { print }
	END { if (changed != 1 || skip) exit 2 }
' "$source_file" > "$delete_digest"
if cmp -s "$source_file" "$delete_digest"; then
	echo "ERROR: delete-digest mutant changed nothing" >&2
	exit 1
fi
awk '
	/^static bool control_snapshot\(/ { in_control = 1 }
	/^static bool control_unchanged\(/ { in_control = 0 }
	in_control && index($0, "state->write.record_size, seal->fmp_record_digest)") {
		sub("state->write.record_size,", "0U,")
		changed++
	}
	{ print }
	END { if (changed != 1) exit 2 }
' "$source_file" > "$size_zero"
if cmp -s "$source_file" "$size_zero"; then
	echo "ERROR: size-zero mutant changed nothing" >&2
	exit 1
fi
awk '
	/^static bool control_snapshot\(/ { in_control = 1 }
	/^static bool control_unchanged\(/ { in_control = 0 }
	in_control && index($0, "state->write.record_size, seal->fmp_record_digest)") {
		sub("state->write.record_size,", "state->write.record_size - 1U,")
		changed++
	}
	{ print }
	END { if (changed != 1) exit 2 }
' "$source_file" > "$size_minus_one"
if cmp -s "$source_file" "$size_minus_one"; then
	echo "ERROR: size-minus-one mutant changed nothing" >&2
	exit 1
fi
awk '
	/^static uint64_t fmp_state_transaction\(/ {
		in_fmp = 1
		seen_function++
	}
	/^uint64_t payload_mm_authvar_fmp_state_transaction\(/ { in_fmp = 0 }
	in_fmp && index($0, "end_result = media_end(state);") {
		print "\tstate->fmp_record_active = false;"
		changed++
	}
	{ print }
	END { if (seen_function != 1 || changed != 1) exit 2 }
' "$source_file" > "$early_clear"
if cmp -s "$source_file" "$early_clear"; then
	echo "ERROR: early-clear mutant changed nothing" >&2
	exit 1
fi
awk '
	/^static uint64_t fmp_state_transaction\(/ {
		in_fmp = 1
		seen_function++
	}
	/^uint64_t payload_mm_authvar_fmp_state_transaction\(/ { in_fmp = 0 }
	in_fmp && $0 == "\tstate->fmp_record_active = true;" {
		activation = $0
		removed++
		next
	}
	in_fmp && index($0, "media_result = execute_direct(state);") {
		print
		print activation
		inserted++
		next
	}
	{ print }
	END {
		if (seen_function != 1 || removed != 1 || inserted != 1)
			exit 2
	}
' "$source_file" > "$late_activation"
if cmp -s "$source_file" "$late_activation"; then
	echo "ERROR: late-activation mutant changed nothing" >&2
	exit 1
fi
for optimization in 0 2; do
	compile_mutant delete-digest "$delete_digest" "$optimization" \
		fmp-tandem-end
	compile_mutant delete-digest-reclaim "$delete_digest" "$optimization" \
		fmp-reclaim-tandem-padding-program
	compile_mutant size-zero "$size_zero" "$optimization" \
		fmp-tandem-end
	compile_mutant size-minus-one "$size_minus_one" "$optimization" \
		fmp-tandem-tail-end
	compile_mutant early-clear "$early_clear" "$optimization" \
		fmp-tandem-end
	compile_mutant late-activation "$late_activation" "$optimization" \
		fmp-active-first-persist
done

printf '%s\n' \
	'Payload-MM FMP canonical digest delete/size-0/size-1/early-clear/late-activation mutants: PASS'
