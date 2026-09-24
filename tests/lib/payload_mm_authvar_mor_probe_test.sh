#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-authvar-mor-probe.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '#define %s %s\n' \
	CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0 \
	CONFIG_SMMSTORE_BLOCK_SIZE 4096 \
	> "$temporary/include/config.h"

for optimization in 0 2; do
	output="$temporary/probe-O$optimization"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_mor_probe_orchestration_test.c" \
		"$root/src/lib/payload_mm_authvar_mor_identity.c" \
		"$root/src/lib/payload_mm_authvar_mor_probe.c" -o "$output"
	ASAN_OPTIONS=detect_leaks=1 "$output"
done

mutation_test()
{
	name="$1"
	expression="$2"
	mutant="$temporary/probe-$name.c"
	sed "$expression" "$root/src/lib/payload_mm_authvar_mor_probe.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_mor_probe.c"; then
		echo "ERROR: $name mutation was not applied" >&2
		exit 1
	fi
	for optimization in 0 2; do
		output="$temporary/mutant-$name-O$optimization"
		${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
			-Wconversion -Wshadow -Wstrict-prototypes \
			-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
			-D__TEST__ -D__COREBOOT__ -include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
			-I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_mor_probe_orchestration_test.c" \
			"$root/src/lib/payload_mm_authvar_mor_identity.c" "$mutant" \
			-o "$output"
		if ASAN_OPTIONS=detect_leaks=1 "$output" >/dev/null 2>&1; then
			echo "ERROR: $name O$optimization mutation survived" >&2
			exit 1
		fi
	done
}

mutation_test ftw-clean \
	's/plan.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN/false/'
mutation_test attributes \
	's/found_entry.attributes != PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES/false/'
mutation_test data-size 's/found_entry.data_size != 1U/false/'
mutation_test unmap 's/if (rdev_munmap/if (false \&\& rdev_munmap/'
mutation_test canonical-geometry \
	's/memcmp(\&plan.geometry, \&expected, sizeof(expected))/false/'
mutation_test output-alignment \
	's/!((uintptr_t)entry % _Alignof(\*entry))/true/'
mutation_test output-bounds \
	's/!((uintptr_t)entry % _Alignof(\*entry))/true/;s/(uintptr_t)entry <= UINTPTR_MAX - (sizeof(\*entry) - 1U)/true/'
mutation_test store-base \
	's/region + plan.geometry.variable_offset + plan.fv_header_size/region + plan.geometry.variable_offset/'
mutation_test absence 's/if (!found)/if (false \&\& !found)/'
mutation_test data-offset \
	's/found_entry.data_offset > plan.variable_store_size - 1U/false/'
mutation_test upper-bits \
	's/store\[found_entry.data_offset\]/store[found_entry.data_offset] \& 1U/'

for optimization in 0 2; do
	output="$temporary/media-O$optimization"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_mor_probe_media_test.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_mor_identity.c" \
		"$root/src/lib/payload_mm_authvar_mor_probe.c" -o "$output"
	ASAN_OPTIONS=detect_leaks=1 "$output"
done

if rg -n 'smmstore_lookup_region|boot_device_rw|rdev_(write|erase)' \
	"$root/src/lib/payload_mm_authvar_mor_probe.c"; then
	echo 'ERROR: MOR probe acquired a read-write dependency' >&2
	exit 1
fi

mkdir -p "$temporary/config-probe" "$temporary/build-probe"
cat > "$temporary/config-probe/.config" <<'EOF'
CONFIG_VENDOR_EMULATION=y
CONFIG_BOARD_EMULATION_QEMU_X86_I440FX=y
EOF
cp "$root/src/Kconfig" "$temporary/Kconfig"
cat >> "$temporary/Kconfig" <<'EOF'

config TEST_MOR_PROBE_SELECTOR
	bool
	default y
	select PAYLOAD_MM_AUTHVAR_MOR_ENTRY_PROBE
EOF
make -C "$root" obj="$temporary/build-probe" \
	KBUILD_KCONFIG="$temporary/Kconfig" \
	DOTCONFIG="$temporary/config-probe/.config" olddefconfig >/dev/null
grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_MOR_ENTRY_PROBE=y' \
	"$temporary/config-probe/.config"
grep -qx 'CONFIG_SMMSTORE_READ_REGION=y' "$temporary/config-probe/.config"
grep -qx 'CONFIG_NO_SMM=y' "$temporary/config-probe/.config"
! grep -q '^CONFIG_HAVE_SMI_HANDLER=y$' "$temporary/config-probe/.config"
! grep -q '^CONFIG_SMMSTORE=y$' "$temporary/config-probe/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_CONTRACT=y$' \
	"$temporary/config-probe/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_POLICY=y$' \
	"$temporary/config-probe/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_STORE_SCANNER=y$' \
	"$temporary/config-probe/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_FTW_DECODER=y$' \
	"$temporary/config-probe/.config"

printf '%s\n' 'Payload-MM MOR entry probe orchestration tests: PASS'
