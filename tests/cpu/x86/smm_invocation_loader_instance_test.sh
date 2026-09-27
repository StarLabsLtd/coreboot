#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

build()
{
	output=$1
	flags=$2
	source=${3:-$root/src/cpu/x86/smm_invocation_loader_instance.c}
	# Deliberate normal flag splitting for this strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-pthread $flags -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/cpu/x86/smm_invocation_loader_instance_test.c" "$source" \
		-o "$temporary/$output"
}

for optimization in 0 2; do
	build "plain-O$optimization" "-O$optimization"
	"$temporary/plain-O$optimization"
	build "sanitize-O$optimization" \
		"-O$optimization -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/sanitize-O$optimization"
done

build tsan "-O1 -g -fsanitize=thread"
TSAN_OPTIONS=halt_on_error=1 "$temporary/tsan"

${CC:-cc} -m32 -march=i686 -std=gnu11 -Wall -Wextra -Werror \
	-Wconversion -Wshadow -ffreestanding -fno-builtin -D__COREBOOT__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" -c \
	"$root/src/cpu/x86/smm_invocation_loader_instance.c" \
	-o "$temporary/boot-instance-32.o"
if nm -u "$temporary/boot-instance-32.o" | grep -q '__atomic_'; then
	printf '%s\n' '32-bit loader instance object gained libatomic dependency' >&2
	exit 1
fi
ld -m elf_i386 -r "$temporary/boot-instance-32.o" \
	-o "$temporary/boot-instance-32-linked.o"

mutate()
{
	name=$1
	expression=$2
	mutant="$temporary/$name.c"
	sed "$expression" "$root/src/cpu/x86/smm_invocation_loader_instance.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/cpu/x86/smm_invocation_loader_instance.c"; then
		printf 'mutant did not change source: %s\n' "$name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		build "$name-O$optimization" "-O$optimization" "$mutant"
		if "$temporary/$name-O$optimization" >/dev/null 2>&1; then
			printf 'surviving mutant: %s O%s\n' "$name" "$optimization" >&2
			exit 1
		fi
	done
}

mutate all-zero 's/(!copy.loader_instance_nonce.low \&\&/(!false \&\&/'
mutate nonce-low 's/instance->loader_instance_nonce.low != copy.loader_instance_nonce.low/false/'
mutate nonce-high 's/instance->loader_instance_nonce.high != copy.loader_instance_nonce.high/false/'
mutate lifecycle 's/return lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD ||/return true ||/'
mutate seed-revision 's/copy.revision != SMM_INVOCATION_LOADER_INSTANCE_REVISION/false/'
mutate seed-size 's/copy.size != sizeof(copy)/false/'
mutate seed-reserved 's/copy.reserved ||/false ||/'
mutate source-recheck 's/memcmp(\&copy, seed, sizeof(copy))/false/'
mutate instance-recheck 's/instance->reserved ||/false ||/'
mutate read-recheck 's/memcmp(\&copy, instance, sizeof(copy))/false/'
mutate loader-fail-scrub 's/if (result)/if (false)/'
mutate cas-ownership \
	's/if (!__atomic_compare_exchange_n/if (false \&\& !__atomic_compare_exchange_n/'
mutate exact-zero-tail \
	's/if (memcmp((const uint8_t \*)instance/if (false \&\& memcmp((const uint8_t *)instance/'
mutate provisioning-recheck \
	's/SMM_INVOCATION_LOADER_INSTANCE_PROVISIONING ||/SMM_INVOCATION_LOADER_INSTANCE_EMPTY ||/'
mutate incomplete-scrub \
	's/sizeof(\*instance) - sizeof(instance->state)/sizeof(*instance) - sizeof(instance->state) - 1U/'
mutate early-ready \
	's/LOADER_INSTANCE_TEST_HOOK(2);/__atomic_store_n(\&instance->state, SMM_INVOCATION_LOADER_INSTANCE_READY, __ATOMIC_RELEASE); LOADER_INSTANCE_TEST_HOOK(2);/'

grep -q '^config SMM_INVOCATION_LOADER_INSTANCE$' "$root/src/cpu/x86/Kconfig"
if rg -q 'select[[:space:]]+SMM_INVOCATION_LOADER_INSTANCE' "$root/src"; then
	printf '%s\n' 'loader instance gained a platform selector' >&2
	exit 1
fi
if rg -q 'smm_invocation_loader_instance_(publish|read)' "$root/src" \
	-g '!src/cpu/x86/smm_invocation_loader_instance.c' \
	-g '!src/include/cpu/x86/smm_invocation_loader_instance.h'; then
	printf '%s\n' 'loader instance gained a production callsite' >&2
	exit 1
fi
if rg -q 'lb_new_record|lb_add|CBMEM|APM_CNT|outb|random|reset|save_state' \
	"$root/src/cpu/x86/smm_invocation_loader_instance.c" \
	"$root/src/include/cpu/x86/smm_invocation_loader_instance.h"; then
	printf '%s\n' 'loader instance gained provider, reset or public-route baggage' >&2
	exit 1
fi
pod="$temporary/boot-instance-pod.txt"
sed -n '/^struct smm_invocation_loader_instance {/,/^} __aligned(8);/p' \
	"$root/src/include/cpu/x86/smm_invocation_loader_instance.h" > "$pod"
if rg -q '\*|callback|uintptr_t|size_t' "$pod"; then
	printf '%s\n' 'protected loader instance is not pointer-free POD' >&2
	exit 1
fi
if rg -q '__uint128|unsigned[[:space:]]+__int128' \
	"$root/src/cpu/x86/smm_invocation_loader_instance.c" \
	"$root/src/include/cpu/x86/smm_invocation_loader_instance.h" \
	"$root/src/include/cpu/x86/smm_invocation_loader_identity.h"; then
	printf '%s\n' 'loader instance gained a 128-bit scalar or atomic' >&2
	exit 1
fi
test "$(rg -c '__ATOMIC_RELEASE' \
	"$root/src/cpu/x86/smm_invocation_loader_instance.c")" -ge 3
test "$(rg -c '__ATOMIC_ACQUIRE' \
	"$root/src/cpu/x86/smm_invocation_loader_instance.c")" -ge 3
documentation="$root/Documentation/arch/x86/smm-invocation-loader-instance.md"
grep -q 'loader-quiescent' "$documentation"
grep -q 'S0ix' "$documentation"
grep -q 'monotonicity or rollback' "$documentation"
grep -q 'No current board selects' "$documentation"
identity="$root/src/include/cpu/x86/smm_invocation_loader_identity.h"
grep -q 'return !nonce.low && !nonce.high;' "$identity"
grep -q 'return first.low == second.low && first.high == second.high;' \
	"$identity"

printf '%s\n' 'SMM invocation loader instance tests passed'
