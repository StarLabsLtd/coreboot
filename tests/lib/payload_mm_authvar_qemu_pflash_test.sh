#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

ASAN_OPTIONS="detect_leaks=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
export ASAN_OPTIONS

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
cat > "$temporary/include/config.h" <<EOF
#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0
#define CONFIG_SMMSTORE_BLOCK_SIZE 65536
EOF
cat > "$temporary/include/fmap_config.h" <<EOF
#define FMAP_SECTION_SMMSTORE_START 0x800000
#define FMAP_SECTION_SMMSTORE_SIZE 0x40000
EOF

run_test()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-Wstrict-prototypes -fno-builtin -pthread "$@" \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/payload_mm_authvar_qemu_pflash_test.c" \
		-o "$temporary/$name"
	for mode in transaction geometry-offset geometry-size geometry-media \
		geometry-block geometry-erase contention reentry begin-mutation alias \
		bounds-one-past bounds-crossing bounds-overflow bounds-erase \
		operation-failure callback-mutation context-mutation close-failure \
		concurrency; do
		"$temporary/$name" "$mode"
	done
}

run_test sanitized-O0 -O0 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
run_test sanitized-O2 -O2 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
run_test thread-O1 -O1 -g -fno-omit-frame-pointer -fsanitize=thread

mutant()
{
	name=$1
	expression=$2
	mode=$3
	source="$temporary/$name.c"
	sed "$expression" "$root/src/lib/payload_mm_authvar_qemu_pflash.c" > "$source"
	! cmp -s "$source" "$root/src/lib/payload_mm_authvar_qemu_pflash.c"
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-Wstrict-prototypes -fno-builtin -O2 \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-DBACKEND_SOURCE_INCLUDE=\"$source\" \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/payload_mm_authvar_qemu_pflash_test.c" \
		-o "$temporary/$name"
	if "$temporary/$name" "$mode" >/dev/null 2>&1; then
		echo "surviving mutant: $name" >&2
		exit 1
	fi
}

mutant omit-post-program-context \
	'/result = qemu_pflash_lease_program/,/return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;/ s/if (result || !context_valid(opaque))/if (result)/' \
	callback-mutation
mutant omit-begin-cleanup \
	's/(void)qemu_pflash_lease_end(\&backend[.]lease);/(void)0;/' \
	begin-mutation
mutant relative-not-absolute \
	's/\*absolute = backend[.]policy[.]store_offset + offset;/\*absolute = offset;/' \
	transaction
mutant ignore-close-error \
	'/static enum payload_mm_authvar_media_result end(/,/^}/ { s/if (result || !valid/if ((void)result, false || !valid/; s/return result || __atomic_load_n/return false || __atomic_load_n/; }' \
	close-failure
mutant omit-backend-alias \
	'/static enum payload_mm_authvar_media_result read_media/,/^}/ s/!buffer_disjoint(buffer, size)/false/' \
	alias

# Compile and link the selected production objects in their real ELF32 SMM
# form. This catches stage-graph omissions without replacing either resolver.
common_smm_flags="-m32 -std=gnu11 -Wall -Werror -Wno-unused-parameter
-ffreestanding -fno-builtin -D__COREBOOT__ -D__SMM__
-include $root/src/include/kconfig.h -include $root/src/include/rules.h
-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
-I$temporary/include -I$root/src -I$root/src/include -I$root/src/lib
-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
-I$root/src/arch/x86/include"
# shellcheck disable=SC2086
"${CC:-cc}" $common_smm_flags -c \
	"$root/src/drivers/smmstore/read_region.c" \
	-o "$temporary/read_region.smm.o"
# shellcheck disable=SC2086
"${CC:-cc}" $common_smm_flags -c \
	"$root/src/lib/payload_mm_authvar_qemu_pflash.c" \
	-o "$temporary/authvar_qemu_pflash.smm.o"
ld -m elf_i386 -r "$temporary/read_region.smm.o" \
	"$temporary/authvar_qemu_pflash.smm.o" -o "$temporary/authvar_backend.smm.o"
file "$temporary/authvar_backend.smm.o" | grep -Eq 'ELF 32-bit.*Intel (80386|i386)'
nm "$temporary/authvar_backend.smm.o" | \
	grep -q ' T payload_mm_authvar_qemu_pflash_install$'
nm "$temporary/authvar_backend.smm.o" | \
	grep -q ' T smmstore_lookup_read_region$'
nm "$temporary/authvar_backend.smm.o" | \
	grep -q ' T smmstore_lookup_fmap_region$'
! nm -u "$temporary/authvar_backend.smm.o" | \
	grep -Eq 'smmstore_lookup_(read|fmap)_region'
grep -Fqx 'smm-$(CONFIG_SMMSTORE_READ_REGION) += read_region.c' \
	"$root/src/drivers/smmstore/Makefile.mk"
grep -Fqx 'smm-$(CONFIG_PAYLOAD_MM_AUTHVAR_QEMU_PFLASH_BACKEND) += payload_mm_authvar_qemu_pflash.c' \
	"$root/src/lib/Makefile.mk"

printf '%s\n' 'Payload-MM authenticated-variable QEMU pflash backend: PASS'
