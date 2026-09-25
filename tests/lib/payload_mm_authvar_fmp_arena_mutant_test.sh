#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
cat > "$temporary/include/config.h" <<'EOF'
#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0
#define CONFIG_PAYLOAD_MM_FMP_OWNER_AUTHVAR 1
#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 1
#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 1
#define CONFIG_PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER 1
EOF

compile_and_kill()
{
	name=$1
	mutant=$2
	optimization=$3
	binary="$temporary/$name-O$optimization"

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
		"$root/tests/lib/payload_mm_authvar_fmp_executor_stubs.c" "$mutant" \
		"$root/src/lib/payload_mm_authvar_coordinator.c" \
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
	if ASAN_OPTIONS=detect_leaks=1 "$binary" fmp-layout \
		>"$temporary/$name-O$optimization.log" 2>&1; then
		echo "ERROR: $name O$optimization mutant survived" >&2
		exit 1
	fi
}

source_file="$root/src/lib/payload_mm_authvar_executor.c"
fields="identity current candidate observation"
for source in identity current candidate observation; do
	for target in snapshot candidate recovery_image_seal entries \
		candidate_entries copies record name data transfer session \
		mutation_data coordinator_context; do
		fields="$fields alias-$source-$target"
	done
done
for field in $fields; do
	case "$field" in
	alias-*)
		alias_spec=${field#alias-}
		source_field=${alias_spec%%-*}
		alias_name=${alias_spec#*-}
		case "$alias_name" in
		snapshot|candidate|recovery_image_seal|entries|candidate_entries|copies|record|name|data|transfer|session|mutation_data|coordinator_context)
			target_field=$alias_name
			;;
		*) echo "ERROR: unknown alias $alias_name" >&2; exit 1 ;;
		esac
		mutant="$temporary/executor-$field.c"
		awk -v source="$source_field" -v target="$target_field" '
			index($0, "&policy->fmp_" source "_offset) ||") {
				sub(/\) \|\|$/, ") || (policy->fmp_" source \
					"_offset = policy->" \
					target "_offset, false) ||")
				changed++
			}
			{ print }
			END { if (changed != 1) exit 2 }
		' "$source_file" > "$mutant"
		optimizations=2
		;;
	*)
		mutant="$temporary/executor-omit-$field.c"
		awk -v target="&policy->fmp_${field}_offset" '
		{
			if (pending) {
				if (index($0, target)) {
					pending = 0
					changed++
					next
				}
				print saved
				pending = 0
			}
			if ($0 ~ /!add_area\(&cursor, sizeof\(struct payload_mm_fmp_/ ) {
				saved = $0
				pending = 1
				next
			}
			print
		}
		END {
			if (pending) print saved
			if (changed != 1) exit 2
		}
		' "$source_file" > "$mutant"
		optimizations="0 2"
		;;
	esac
	if cmp -s "$source_file" "$mutant"; then
		echo "ERROR: $field mutant changed nothing" >&2
		exit 1
	fi
	for optimization in $optimizations; do
		compile_and_kill "$field" "$mutant" "$optimization"
	done
done

printf 'Payload-MM FMP arena-allocation mutants (%s): PASS\n' "$fields"
