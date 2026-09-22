#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

sed \
	-e '/#include <sys\/mman[.]h>/a\
bool payload_mm_authvar_media_test_cache_bound(void);\
bool payload_mm_authvar_media_test_poisoned(void);' \
	-e '/assert(!payload_mm_authvar_media_cache_valid(output_generation, output_token));/a\
\tassert(!payload_mm_authvar_media_test_cache_bound());\
\tassert(payload_mm_authvar_media_test_poisoned());' \
	"$root/tests/lib/payload_mm_authvar_media_test.c" > "$temporary/test.c"

compile_and_reject()
{
	name=$1
	mode=$2
	shift 2
	sed "$@" "$root/src/lib/payload_mm_authvar_media.c" > "$temporary/$name.c"
	cat >> "$temporary/$name.c" <<'EOF'
bool payload_mm_authvar_media_test_cache_bound(void)
{
	return __atomic_load_n(&media.cache_bound, __ATOMIC_ACQUIRE);
}

bool payload_mm_authvar_media_test_poisoned(void)
{
	return __atomic_load_n(&media.poisoned, __ATOMIC_ACQUIRE);
}
EOF
	for optimization in 0 2; do
		binary="$temporary/$name-O$optimization"
		if ! "${CC:-cc}" -std=gnu11 "-O$optimization" -g -Wall -Wextra \
			-Werror -Wconversion -Wshadow -Wstrict-prototypes -fno-builtin \
			-fno-omit-frame-pointer -fsanitize=address,undefined \
			-fno-sanitize-recover=all -D__TEST__ -D__COREBOOT__ -D__SMM__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$root/src" -I"$root/src/include" \
			-I"$root/src/lib" \
			-I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" -I"$temporary/include" \
			"$temporary/test.c" "$root/src/lib/payload_mm_authvar.c" \
			"$root/src/lib/payload_mm_authvar_runtime.c" \
			"$root/src/lib/payload_mm_authvar_ftw.c" \
			"$temporary/$name.c" -o "$binary"; then
			printf 'mutation did not compile: %s O%s\n' "$name" \
				"$optimization" >&2
			exit 1
		fi
		if ASAN_OPTIONS=detect_leaks=1 "$binary" "$mode" >/dev/null 2>&1; then
			printf 'mutation survived: %s O%s\n' "$name" "$optimization" >&2
			exit 1
		fi
	done
}

compile_and_reject keep_cache fail-closed \
	'0,/__atomic_store_n(&media[.]cache_bound, 0, __ATOMIC_RELEASE);/s//(void)0;/'
compile_and_reject no_permanent_poison fail-closed \
	'0,/__atomic_store_n(&media[.]poisoned, 1, __ATOMIC_RELEASE);/s//(void)0;/'
compile_and_reject accept_wrong_owner fail-closed-wrong-token \
	'/Misuse of this internal terminal path/a\
\tif (!session_owned(generation, token))\
\t\treturn PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;'
compile_and_reject release_transaction fail-closed \
	'/Misuse of this internal terminal path/a\
\t__atomic_store_n(&media.transaction, TRANSACTION_IDLE, __ATOMIC_RELEASE);'
compile_and_reject skip_sealed_end fail-closed \
	'/uint32_t expected = TRANSACTION_ACTIVE;/a\
\tif (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE))\
\t\treturn PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;'
compile_and_reject allow_later_begin fail-closed \
	'/if (!media[.]installed)/i\
\t__atomic_store_n(&media.fail_closed, 0, __ATOMIC_RELEASE);\
\t__atomic_store_n(&media.poisoned, 0, __ATOMIC_RELEASE);'
compile_and_reject remove_terminal_gates fail-closed-program-callback \
	's/__atomic_load_n(&media[.]fail_closed, __ATOMIC_ACQUIRE)/false/g'
compile_and_reject allow_private_buffer disjoint \
	's/return buffer \&\& size \&\& !overlaps_media(buffer, size);/return buffer \&\& size;/'
compile_and_reject narrow_private_state disjoint \
	'0,/sizeof(media));/s//sizeof(media.policy));/'
compile_and_reject omit_private_tail disjoint \
	'0,/sizeof(media));/s//sizeof(media) - 1U);/'
compile_and_reject omit_private_prefix disjoint \
	'0,/&media,/s//\&media.policy.context,/'

printf '%s\n' 'Payload-MM authenticated-variable media mutation tests: PASS'
