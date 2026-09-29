#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d); trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_SMM_INVOCATION_AUXILIARY_CHANNELS 1' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_VIEW 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_COMPOSITION 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_INSTANCE 1' \
	'#define CONFIG_SMM_INVOCATION_TOPOLOGY 1' '#define CONFIG_MAX_CPUS 64' \
	> "$temporary/include/config.h"
common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -D__RAMSTAGE__ -I$temporary/include
	-include $root/src/include/kconfig.h -include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$root/src -I$root/src/include -I$root/src/commonlib/include
	-I$root/src/commonlib/bsd/include -I$root/src/arch/x86/include"
sources="$root/tests/lib/smm_invocation_auxiliary_channels_test.c $root/src/cpu/x86/smm_invocation_auxiliary_channels.c"
for flags in '-O0' '-O2' '-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/test"
	for case_name in valid first-failure second-failure hook1 hook2 hook3 hook4 \
		dirty double concurrent null misaligned overlap lookup-alias abort \
		abort-invalid \
		composition-drift topology-drift instance-drift revision size count \
		reserved composition-id topology-id instance-id primary-id seed index \
		binding-reserved identity evidence-tail evidence-mask evidence-nonce \
		evidence-lifecycle evidence-empty evidence-provisioning poisoned-phase; do
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$temporary/test" "$case_name"
	done
done
for architecture in native ia32; do
	arch_flags=
	if [ "$architecture" = ia32 ]; then
		arch_flags='-m32 -Wno-conversion'
	fi
	# shellcheck disable=SC2086
	${CC:-cc} $common $arch_flags -O2 -fstack-usage -c \
		"$root/src/cpu/x86/smm_invocation_auxiliary_channels.c" \
		-o "$temporary/aux-$architecture.o"
	usage="$temporary/aux-$architecture.su"
	for symbol in smm_invocation_auxiliary_channels_compose \
		smm_invocation_auxiliary_channel_evidence; do
		frame=$(awk -F '\t' -v symbol="$symbol" \
			'$1 ~ symbol "$" { print $2 }' "$usage")
		test -n "$frame" && test "$frame" -le 2048
	done
	! nm -u "$temporary/aux-$architecture.o" | grep -q '__atomic_'
done
echo 'SMM invocation auxiliary channels: PASS'
