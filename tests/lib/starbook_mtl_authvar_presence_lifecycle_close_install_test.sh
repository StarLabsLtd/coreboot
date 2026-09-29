#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	> "$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -include $root/src/include/kconfig.h
	-include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -no-pie"
sources="$root/tests/lib/starbook_mtl_authvar_presence_lifecycle_close_install_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_receiver.c"

for flags in '-O0' '-O2' '-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/test"
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$temporary/test"
done

mkdir -p "$temporary/mailbox/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY 1' \
	> "$temporary/mailbox/include/config.h"
mailbox_common=$(printf '%s' "$common" | sed \
	"s@$temporary/include@$temporary/mailbox/include@g")
for flags in '-O0' '-O2' \
	'-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $mailbox_common $flags $sources -o "$temporary/mailbox-test"
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/mailbox-test"
done
# A failure after the one-shot proof is consumed must poison the authority,
# preserve the caller's proof, and return only the canonical rejected frame.
# shellcheck disable=SC2086
${CC:-cc} $mailbox_common -O2 -DTEST_ROUTE_FAILURE=1 $sources \
	-o "$temporary/mailbox-failure"
"$temporary/mailbox-failure"
# A rejected one-shot proof is not consumed and must not poison the authority.
# shellcheck disable=SC2086
${CC:-cc} $mailbox_common -O2 -DTEST_VERIFY_FAILURE=1 $sources \
	-o "$temporary/mailbox-verify-failure"
"$temporary/mailbox-verify-failure"

mutation="$temporary/noncanonical-slot.c"
sed 's/slot = smm_get_payload_mm_authvar_presence_transaction_slot()/slot = NULL/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_receiver.c" \
	> "$mutation"
# shellcheck disable=SC2086
${CC:-cc} $common -O2 \
	"$root/tests/lib/starbook_mtl_authvar_presence_lifecycle_close_install_test.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.c" \
	"$mutation" -o "$temporary/mutant"
if "$temporary/mutant" >/dev/null 2>&1; then
	echo 'lifecycle-close install canonical-slot mutation survived' >&2
	exit 1
fi

mutation="$temporary/unscrubbed-sender.c"
sed -e 's/scrub(&frame, sizeof(frame));/(void)frame;/' \
	-e 's/scrub(frame, sizeof(\*frame));/(void)frame;/' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.c" \
	> "$mutation"
! cmp -s "$mutation" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.c"
# shellcheck disable=SC2086
${CC:-cc} $common -O2 \
	"$root/tests/lib/starbook_mtl_authvar_presence_lifecycle_close_install_test.c" \
	"$mutation" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_receiver.c" \
	-o "$temporary/unscrubbed"
if "$temporary/unscrubbed" >/dev/null 2>&1; then
	echo 'lifecycle-close install frame-scrub mutation survived' >&2
	exit 1
fi

receiver="$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_receiver.c"
for mutation in first-range second-range; do
	mutant="$temporary/$mutation.c"
	case "$mutation" in
first-range)
		sed '0,/!dependencies->communication_range_valid(/s//dependencies->communication_range_valid(/' \
			"$receiver" > "$mutant" ;;
second-range)
		sed 's/!dependencies->communication_range_valid(/dependencies->communication_range_valid(/g' \
			"$receiver" > "$mutant" ;;
	esac
	! cmp -s "$receiver" "$mutant"
	# shellcheck disable=SC2086
	${CC:-cc} $common -O2 \
		"$root/tests/lib/starbook_mtl_authvar_presence_lifecycle_close_install_test.c" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.c" \
		"$mutant" -o "$temporary/$mutation"
	if "$temporary/$mutation" >/dev/null 2>&1; then
		echo "lifecycle-close install range mutation survived: $mutation" >&2
		exit 1
	fi
done

grep -q 'UINT32_MAX - (sizeof(\*frame) - 1U)' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.c"

echo 'StarBook MTL lifecycle-close installation channel: PASS'
