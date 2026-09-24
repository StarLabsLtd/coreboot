#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/q35-mor-fixture.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '#define %s %s\n' \
	CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0 \
	CONFIG_SMMSTORE_BLOCK_SIZE 4096 \
	> "$temporary/include/config.h"

for optimization in 0 2; do
	output="$temporary/q35-mor-fixture-O$optimization"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$root/tests/lib" \
		"$root/tests/lib/q35_mor_fixture_test.c" \
		"$root/tests/lib/q35_mor_fixture.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" \
		"$root/src/lib/payload_mm_authvar_mor_identity.c" \
		"$root/src/lib/payload_mm_authvar_mor_probe.c" -o "$output"
	ASAN_OPTIONS=detect_leaks=1 "$output"
done

mutant_test()
{
	name="$1"
	fixture_source="$2"
	test_source="$3"
	output="$temporary/mutant-$name"
	${HOSTCC:-cc} -std=gnu11 -O2 -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$root/tests/lib" \
		"$test_source" "$fixture_source" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" \
		"$root/src/lib/payload_mm_authvar_mor_identity.c" \
		"$root/src/lib/payload_mm_authvar_mor_probe.c" -o "$output"
	if ASAN_OPTIONS=detect_leaks=1 "$output" >/dev/null 2>&1; then
		echo "ERROR: $name mutant survived" >&2
		exit 1
	fi
}

oracle_mutant="$temporary/oracle-independent.c"
sed '0,/0xbe, 0x39/s//0xbf, 0x39/' \
	"$root/tests/lib/q35_mor_fixture.c" > "$oracle_mutant"
mutant_test oracle-independent "$oracle_mutant" \
	"$root/tests/lib/q35_mor_fixture_test.c"

byte_mutant="$temporary/byte-mismatch.c"
sed 's/record\[120U\] = value;/record[121U] = value;/' \
	"$root/tests/lib/q35_mor_fixture.c" > "$byte_mutant"
mutant_test byte-mismatch "$byte_mutant" \
	"$root/tests/lib/q35_mor_fixture_test.c"

utf16le_mutant="$temporary/utf16le.c"
sed '0,/0x4d, 0x00/s//0x00, 0x4d/' \
	"$root/tests/lib/q35_mor_fixture.c" > "$utf16le_mutant"
mutant_test utf16le "$utf16le_mutant" \
	"$root/tests/lib/q35_mor_fixture_test.c"

negative_mutant="$temporary/negative-enum.c"
sed 's/(int)kind < 0 ||//' \
	"$root/tests/lib/q35_mor_fixture.c" > \
	"$negative_mutant"
! cmp -s "$negative_mutant" "$root/tests/lib/q35_mor_fixture.c"
mutant_test negative-enum "$negative_mutant" \
	"$root/tests/lib/q35_mor_fixture_test.c"

replay_mutant="$temporary/skipped-replay.c"
sed 's/assert(writer_clear(transformed, true) == PAYLOAD_MM_AUTHVAR_WRITE_NOOP);/assert(PAYLOAD_MM_AUTHVAR_WRITE_NOOP == PAYLOAD_MM_AUTHVAR_WRITE_NOOP);/' \
	"$root/tests/lib/q35_mor_fixture_test.c" > "$replay_mutant"
mutant_test skipped-replay "$root/tests/lib/q35_mor_fixture.c" \
	"$replay_mutant"

mkdir -p "$temporary/config" "$temporary/build"
cat > "$temporary/config/.config" <<'EOF'
CONFIG_VENDOR_EMULATION=y
CONFIG_BOARD_EMULATION_QEMU_X86_Q35=y
CONFIG_ANY_TOOLCHAIN=y
CONFIG_Q35_PAYLOAD_MM_MOR_TEST_ADAPTER=y
# CONFIG_SMMSTORE is not set
EOF
make -C "$root" obj="$temporary/build" \
	DOTCONFIG="$temporary/config/.config" olddefconfig >/dev/null
grep -qx 'CONFIG_SMMSTORE_READ_REGION=y' "$temporary/config/.config"
! grep -qx 'CONFIG_SMMSTORE=y' "$temporary/config/.config"
grep -qx 'CONFIG_Q35_PAYLOAD_MM_MOR_TEST_ADAPTER=y' \
	"$temporary/config/.config"
! grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_SMMSTORE_BACKEND=y' \
	"$temporary/config/.config"

printf '%s\n' 'Q35 MOR deterministic fixture tests: PASS'
