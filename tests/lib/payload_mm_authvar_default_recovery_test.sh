#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_RECOVERY 1' \
	> "$tmp/include/config.h"

compile_binary()
{
	optimization="$1"
	executor_source="$2"
	block_size="$3"
	block_count="$4"
	arena_size="$5"
	binary="$6"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-DTEST_BLOCK_SIZE="$block_size" -DTEST_BLOCK_COUNT="$block_count" \
		-DTEST_ERASE_SIZE=4096U -DTEST_ARENA_SIZE="$arena_size" \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_executor_acceptance_test.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_authvar_media.c" "$executor_source" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" \
		"$root/src/lib/payload_mm_authvar_default_store.c" \
		-Wl,--wrap=payload_mm_authvar_media_fail_closed \
		-Wl,--wrap=payload_mm_authvar_media_cache_invalidate \
		-Wl,--wrap=payload_mm_authvar_media_cache_bind \
		-Wl,--wrap=payload_mm_authvar_default_store_compose -o "$binary"
}

for optimization in 0 2; do
	compile_binary "$optimization" \
		"$root/src/lib/payload_mm_authvar_executor.c" 4096U 3U \
		'128U * 1024U' "$tmp/test-4k-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$tmp/test-4k-O$optimization" default-recovery
	compile_binary "$optimization" \
		"$root/src/lib/payload_mm_authvar_executor.c" 65536U 3U \
		'1024U * 1024U' "$tmp/test-64k-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$tmp/test-64k-O$optimization" \
		default-recovery-geometry
done

mutation_test()
{
	name="$1"
	expression="$2"
	block_size="${3:-4096U}"
	arena_size="${4:-128U * 1024U}"
	mutant="$tmp/executor-$name.c"

	sed "$expression" "$root/src/lib/payload_mm_authvar_executor.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_executor.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/mutant-$name-O$optimization"
		log="$tmp/mutant-$name-O$optimization.log"
		if ! compile_binary "$optimization" "$mutant" "$block_size" 3U \
			"$arena_size" "$binary" >"$log" 2>&1; then
			echo "ERROR: $name O$optimization mutant did not compile" >&2
			cat "$log" >&2
			exit 1
		fi
		test_mode=default-recovery
		if [ "$block_size" = 65536U ]; then
			test_mode=default-recovery-geometry
		fi
		if ASAN_OPTIONS=detect_leaks=1 "$binary" "$test_mode" \
			>"$log" 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutation_test skip-fresh-routing \
	's/if (!bytes_erased(current/if (true || !bytes_erased(current/'
mutation_test accept-invalid \
	's/if (source == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID)/if (false \&\& source == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID)/; /source != PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED/,/PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_NOR_SUBSET)/ s/if (source !=/if (false \&\& source !=/'
mutation_test consume-foreign \
	's/if (source == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_FOREIGN)/if (false \&\& source == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_FOREIGN)/; /source != PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED/,/PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_NOR_SUBSET)/ s/if (source !=/if (false \&\& source !=/'
mutation_test lifecycle-gate \
	's/if (executor.ready_to_boot || executor.at_runtime)/if (false)/'
mutation_test header-first \
	's/for (offset = PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;/for (offset = 0U;/'
mutation_test skip-body-equality \
	's/if (offset < geometry.variable_size) {/if (false) {/'
mutation_test header-span \
	's/size = PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;/size = PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE - 1U;/'
mutation_test omit-resnapshot \
	'/result = checked_program(state/,/continue;/ s/continue;/return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;/'
mutation_test program-workspace \
	's/size = geometry.variable_size - offset;/size = state->contract.store_size - offset;/'
mutation_test transfer-cap \
	'/size = geometry.variable_size - offset;/,/size = EXECUTOR_TRANSFER_SIZE;/ s/if (size > EXECUTOR_TRANSFER_SIZE)/if (false)/' \
	65536U '1024U * 1024U'
mutation_test reject-complete \
	's/if (source == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_COMPLETE)/if (false \&\& source == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_COMPLETE)/'

printf '%s\n' 'Payload-MM authenticated-variable default recovery tests: PASS'
