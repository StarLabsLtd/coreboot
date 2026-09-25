#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/include"
cat > "$tmp/include/config.h" <<EOF
#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0
#define CONFIG_MAX_CPUS 4
#define CONFIG_SMMSTORE 0
#define CONFIG_SMMSTORE_FULL_FLASH_ACCESS 0
#define CONFIG_SMMSTORE_BLOCK_SIZE 65536
#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 0
#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 0
#define CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION 0
EOF

build_test()
{
	name=$1
	optimization=$2
	source=$3
	shift 3
	cc -std=gnu11 -O"$optimization" -g -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes -fno-builtin \
		-fno-pie -no-pie "$@" -D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$tmp/include" \
		"$root/tests/lib/payload_mm_authvar_smm_bootstrap_test.c" \
		"$source" "$root/src/lib/payload_mm_authvar.c" -o "$tmp/$name"
}

for opt in 0 2; do
	build_test "test-$opt" "$opt" \
		"$root/src/lib/payload_mm_authvar_smm_bootstrap.c" \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-fno-omit-frame-pointer
	for mode in success repeat restricted short alias unaligned overflow \
		receipt-revision receipt-size receipt-reserved receipt-smram arena-empty \
		arena-outside store-failure flash-absent flash-size flash-sector \
		small-smram transport-alias context-overlap concurrent two-taker \
		pre-take post-take callback-retake state-mutation torn-publication \
		receipt-owner receipt-generation \
		media-context-alias media-facts-mutation \
		media-facts-reserved media-ops-reserved media-ops-missing-install \
		media-install-failure \
		mutate1 mutate2 mutate3 mutate4 mutate5; do
		ASAN_OPTIONS=detect_leaks=0 "$tmp/test-$opt" "$mode"
	done
done

# Exercise the real platform wrappers, not just the bootstrap's hostile mock.
for provider in spi qemu; do
	case "$provider" in
	spi)
		provider_source="$root/src/lib/payload_mm_authvar_smm_media_spi.c"
		modes="success store-failure flash-absent flash-size flash-sector media-store-drift"
		;;
	qemu)
		provider_source="$root/src/lib/payload_mm_authvar_smm_media_qemu.c"
		modes="success store-failure flash-absent media-store-drift media-size-drift"
		;;
	esac
	for opt in 0 2; do
		build_test "$provider-provider-$opt" "$opt" \
			"$root/src/lib/payload_mm_authvar_smm_bootstrap.c" \
			-DTEST_EXTERNAL_MEDIA_PROVIDER "$provider_source" \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-fno-omit-frame-pointer
		for mode in $modes; do
			ASAN_OPTIONS=detect_leaks=0 \
				"$tmp/$provider-provider-$opt" "$mode"
		done
	done
done

# Prove the default-off boundary and the selected Q35 SMM composition.
mkdir -p "$tmp/q35-default" "$tmp/q35-selected"
cat > "$tmp/q35-default/.config" <<'EOF'
CONFIG_VENDOR_EMULATION=y
CONFIG_BOARD_EMULATION_QEMU_X86_Q35=y
CONFIG_ANY_TOOLCHAIN=y
EOF
make -s -C "$root" obj="$tmp/q35-default/out" \
	DOTCONFIG="$tmp/q35-default/.config" olddefconfig >/dev/null
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP=y$' \
	"$tmp/q35-default/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_QEMU_PFLASH_BACKEND=y$' \
	"$tmp/q35-default/.config"

cp "$root/src/Kconfig" "$tmp/q35-selected/Kconfig"
cat >> "$tmp/q35-selected/Kconfig" <<'EOF'

config TEST_Q35_AUTHVAR_BOOTSTRAP_SELECTOR
	bool
	default y
	select Q35_PAYLOAD_MM_AUTHVAR_EXECUTOR_TEST_PROOF
	select Q35_PAYLOAD_MM_MOR_TEST_ADAPTER
	select PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER
	select PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_SEAL
	select PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP
EOF
cat > "$tmp/q35-selected/.config" <<'EOF'
CONFIG_VENDOR_EMULATION=y
CONFIG_BOARD_EMULATION_QEMU_X86_Q35=y
CONFIG_ANY_TOOLCHAIN=y
# CONFIG_SMMSTORE is not set
EOF
make -s -C "$root" obj="$tmp/q35-selected/out" \
	KBUILD_KCONFIG="$tmp/q35-selected/Kconfig" \
	DOTCONFIG="$tmp/q35-selected/.config" olddefconfig >/dev/null
for option in PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP \
	PAYLOAD_MM_AUTHVAR_QEMU_PFLASH_BACKEND SMMSTORE_READ_REGION; do
	grep -qx "CONFIG_$option=y" "$tmp/q35-selected/.config"
done
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_SMMSTORE_BACKEND=y$' \
	"$tmp/q35-selected/.config"
! grep -q '^CONFIG_SMMSTORE=y$' "$tmp/q35-selected/.config"
make -s -C "$root" obj="$tmp/q35-selected/out" \
	KBUILD_KCONFIG="$tmp/q35-selected/Kconfig" \
	DOTCONFIG="$tmp/q35-selected/.config" \
	"$tmp/q35-selected/out/smm/smm.elf-ldflags=-u payload_mm_authvar_smm_bootstrap_install" \
	"$tmp/q35-selected/out/smm/smm.elf" -j4 >/dev/null
for symbol in payload_mm_authvar_smm_bootstrap_install \
	platform_payload_mm_authvar_smm_media_ops \
	payload_mm_authvar_qemu_pflash_install payload_mm_authvar_executor_install \
	payload_mm_authvar_authority_install \
	payload_mm_authvar_mor_seal_channel_install; do
	nm -g --defined-only "$tmp/q35-selected/out/smm/smm.elf" |
		grep -Eq " T $symbol$"
done
! nm -g --defined-only "$tmp/q35-selected/out/smm/smm.elf" |
	grep -Eq ' T payload_mm_authvar_smmstore_install$'

objdump -d "$tmp/test-2" | awk '
	/<scrub>:/ { inside = 1 }
	inside { print }
	inside && /^$/ { exit }
' > "$tmp/scrub.disassembly"
if ! grep -Eq 'mov[bwlq].*\$0x0' "$tmp/scrub.disassembly"; then
	echo "optimized scrub has no explicit zero store" >&2
	exit 1
fi

build_test thread-sanitized 2 \
	"$root/src/lib/payload_mm_authvar_smm_bootstrap.c" \
	-fsanitize=thread -fno-sanitize-recover=all -fno-omit-frame-pointer
TSAN_OPTIONS=halt_on_error=1 "$tmp/thread-sanitized" concurrent
TSAN_OPTIONS=halt_on_error=1 "$tmp/thread-sanitized" two-taker

for mutation in 1 2 3 4 5; do
	occurrence=$((mutation + 2))
	mutant="$tmp/mutant-$mutation.c"
	awk -v target="$occurrence" '
		{
			if (index($0, "memcmp(&input, bootstrap")) {
				seen++
				if (seen == target)
					sub("memcmp\\(&input, bootstrap, sizeof\\(input\\)\\)",
						"false")
			}
			print
		}' "$root/src/lib/payload_mm_authvar_smm_bootstrap.c" > "$mutant"
	build_test "mutant-$mutation" 2 "$mutant"
	if "$tmp/mutant-$mutation" "mutate$mutation" >/dev/null 2>&1; then
		echo "mutation-boundary mutant $mutation survived" >&2
		exit 1
	fi
done

for scrubbed in receipt facts media input frozen; do
	scrub_mutant="$tmp/scrub-mutant-$scrubbed.c"
	awk -v target="scrub(&$scrubbed, sizeof($scrubbed));" '
		index($0, target) { next }
		{ print }
	' "$root/src/lib/payload_mm_authvar_smm_bootstrap.c" > "$scrub_mutant"
	build_test "scrub-mutant-$scrubbed" 2 "$scrub_mutant"
	if "$tmp/scrub-mutant-$scrubbed" success >/dev/null 2>&1; then
		echo "$scrubbed scrub mutant survived" >&2
		exit 1
	fi
done

state_mutant="$tmp/state-mutant.c"
awk '
	{
		if (index($0, "smm_payload_mm_authvar_arena_receipt_consumed()")) {
			seen++
			if (seen == 2)
				sub("!smm_payload_mm_authvar_arena_receipt_consumed\\(\\)",
					"false")
		}
		print
	}' "$root/src/lib/payload_mm_authvar_smm_bootstrap.c" > "$state_mutant"
build_test state-mutant 2 "$state_mutant"
if "$tmp/state-mutant" state-mutation >/dev/null 2>&1; then
	echo "consumed-state boundary mutant survived" >&2
	exit 1
fi

publication_mutant="$tmp/publication-mutant.c"
awk '
	{
		if (index($0, "memcmp(receipt.owner, input.seal_channel.capability,")) {
			print "\t    false ||"
			getline
			next
		}
		print
	}' "$root/src/lib/payload_mm_authvar_smm_bootstrap.c" > "$publication_mutant"
build_test publication-mutant 2 "$publication_mutant"
if "$tmp/publication-mutant" torn-publication >/dev/null 2>&1; then
	echo "torn-publication mutant survived" >&2
	exit 1
fi
