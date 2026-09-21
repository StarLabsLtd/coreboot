#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

build()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__RAMSTAGE__ -D__COREBOOT__ -DCAPSULE_BROKER_ENDPOINT_TEST \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/capsule_broker_endpoint_table_test.c" \
		"$root/src/lib/capsule_broker_endpoint_table.c" \
		"$root/src/lib/capsule_broker_endpoint.c" \
		"$root/src/lib/capsule_update.c" \
		-o "$temporary/table-$name"
	if nm -u "$temporary/table-$name" | grep -E \
		'capsule_(broker_endpoint|handoff)_validate|lb_new_record'; then
		printf '%s\n' 'ramstage endpoint publication is not link-closed' >&2
		exit 1
	fi
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__RAMSTAGE__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/capsule_broker_endpoint_table_test.c" \
		"$root/src/lib/capsule_broker_endpoint_table.c" \
		"$root/src/lib/capsule_broker_endpoint.c" \
		"$root/src/lib/capsule_update.c" \
		-o "$temporary/ramstage-link-$name"
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/capsule_broker_endpoint_smi_test.c" \
		"$root/src/lib/capsule_broker_endpoint_smi.c" \
		-o "$temporary/smi-$name"
	for case in happy missing malformed short ready-failure callback-mutation \
		input-mutation callback-close closed-before-install reentry \
		stale-readiness stored-handoff-mutation callback-stored-1 \
		callback-stored-2 callback-stored-3 callback-stored-4 \
		callback-stored-5 s3; do
		"$temporary/table-$name" "$case"
	done
	for case in happy port value not-ready reentry; do
		"$temporary/smi-$name" "$case"
	done
}

build ordinary
build optimized -O2
build strict -O2 -Wconversion -Wsign-conversion
build sanitized -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
printf '%s\n' 'Capsule endpoint publication O0/O2/strict/ASan+UBSan: PASS'
