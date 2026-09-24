#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0\n' > "$temporary/include/config.h"

cases='install close close-nonzero-grant bad-revision bad-size unknown-command
bad-reserved bad-capability bad-grant wrong-pointer wrong-size wrong-caller
wrong-context range-recheck channel-one-shot channel-unprotected channel-unshared
channel-mutation channel-size channel-wrap channel-zero-capability
channel-zero-caller channel-zero-context channel-null channel-transport-alias
channel-stored-callback-mutation late-authority-redirect late-authority-callback
sender-trigger-failure sender-rewrite sender-channel-mutation
sender-grant-mutation sender-null sender-misaligned sender-channel-transport-alias
sender-grant-channel-alias'

build_and_run()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin \
		-ffunction-sections -fdata-sections -Wl,--gc-sections "$@" \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/payload_mm_authvar_mor_seal_test.c" \
		"$root/src/lib/payload_mm_authvar_mor_seal_sender.c" \
		"$root/src/lib/payload_mm_authvar_mor_seal.c" \
		"$root/src/lib/payload_mm_authvar_mor_grant.c" \
		-o "$temporary/$name"
	for case_name in $cases; do
		"$temporary/$name" "$case_name"
	done
}

build_and_run o0 -O0
build_and_run o2 -O2
build_and_run asan -O1 -fsanitize=address -fno-omit-frame-pointer
build_and_run ubsan -O1 -fsanitize=undefined -fno-omit-frame-pointer

compile_mutant()
{
	name=$1
	file=$2
	expression=$3
	case_name=$4
	mutant="$temporary/$name.c"
	sed "$expression" "$root/src/lib/$file" > "$mutant"
	! cmp -s "$root/src/lib/$file" "$mutant"
	sender="$root/src/lib/payload_mm_authvar_mor_seal_sender.c"
	receiver="$root/src/lib/payload_mm_authvar_mor_seal.c"
	case "$file" in
	payload_mm_authvar_mor_seal_sender.c) sender=$mutant ;;
	payload_mm_authvar_mor_seal.c) receiver=$mutant ;;
	esac
	"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-parentheses -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$temporary/include" \
		"$root/tests/lib/payload_mm_authvar_mor_seal_test.c" "$sender" "$receiver" \
		"$root/src/lib/payload_mm_authvar_mor_grant.c" -o "$temporary/$name"
	if "$temporary/$name" "$case_name" >/dev/null 2>&1; then
		echo "ERROR: $name mutation survived" >&2
		exit 1
	fi
}

compile_mutant caller-binding payload_mm_authvar_mor_seal.c \
	's/observed_caller != seal_authority.channel.caller ||/(observed_caller \&\& false) ||/' \
	wrong-caller
compile_mutant capability-binding payload_mm_authvar_mor_seal.c \
	's/request->capability, channel->capability/channel->capability, channel->capability/' \
	bad-capability
compile_mutant fixed-range-recheck payload_mm_authvar_mor_seal.c \
	's/!fixed_transport_copy(transport, sizeof(\*transport))/false/' range-recheck
compile_mutant channel-mutation payload_mm_authvar_mor_seal.c \
	's/memcmp(channel, \&snapshot, sizeof(snapshot)) ||/false ||/' channel-mutation
compile_mutant sender-output-recheck payload_mm_authvar_mor_seal_sender.c \
	's/!bytes_zero(transport, sizeof(\*transport))/false/' sender-rewrite
compile_mutant late-channel-invariant payload_mm_authvar_mor_seal.c \
	'/static bool grant_storage_is_protected/,/^}/ s/\&channel_copy, \&seal_authority.channel/\&seal_authority.channel, \&seal_authority.channel/' \
	late-authority-redirect
compile_mutant late-callback-invariant payload_mm_authvar_mor_seal.c \
	's/fixed_transport_copy == seal_authority.fixed_transport/fixed_transport_copy != NULL/' \
	late-authority-callback
compile_mutant terminal-secret-scrub payload_mm_authvar_mor_seal.c \
	's/memset(&seal_authority.channel, 0, sizeof(seal_authority.channel));/(void)seal_authority.channel;/' \
	late-authority-callback
compile_mutant immutable-scrub-target payload_mm_authvar_mor_seal.c \
	's/memset(transport, 0, size);/memset((void *)(uintptr_t)seal_authority.channel.transport_base, 0, size);/' \
	late-authority-redirect

common_flags="-std=gnu11 -Os -m32 -Wall -Wextra -Werror -fno-builtin
-include $root/src/include/kconfig.h -include $root/src/include/rules.h
-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
-I$root/src -I$root/src/include -I$root/src/commonlib/include
-I$root/src/commonlib/bsd/include -I$root/src/arch/x86/include -I$temporary/include"
# shellcheck disable=SC2086
"${CC:-cc}" $common_flags -D__COREBOOT__ -D__RAMSTAGE__ -c \
	"$root/src/lib/payload_mm_authvar_mor_seal_sender.c" -o "$temporary/sender.o"
# shellcheck disable=SC2086
"${CC:-cc}" $common_flags -D__COREBOOT__ -D__SMM__ -fstack-usage -c \
	"$root/src/lib/payload_mm_authvar_mor_seal.c" -o "$temporary/receiver.o"
nm -g --defined-only "$temporary/sender.o" | grep -q 'payload_mm_authvar_mor_seal_send_install'
! nm -g --defined-only "$temporary/sender.o" | grep -q 'mor_seal_receive'
nm -g --defined-only "$temporary/receiver.o" | grep -q 'payload_mm_authvar_mor_seal_receive'
! nm -g --defined-only "$temporary/receiver.o" | grep -q 'mor_seal_send'
awk -F '\t' '$1 ~ /:payload_mm_authvar_mor_seal_receive$/ && $2 + 0 > 256 { exit 1 }' \
	"$temporary/receiver.su"

printf '%s\n' 'Payload-MM MOR completion seal tests: PASS'
