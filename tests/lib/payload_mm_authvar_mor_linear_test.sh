#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '#define CONFIG_COLLECT_TIMESTAMPS 0\n#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0\n' > \
	"$temporary/include/config.h"

compile_and_run()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -pthread "$@" \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/payload_mm_authvar_mor_linear_test.c" \
		"$root/src/lib/payload_mm_authvar_mor_linear.c" -o "$temporary/$name"
	"$temporary/$name"
}

compile_and_run o0 -O0
compile_and_run o2 -O2
compile_and_run asan -O1 -fsanitize=address -fno-omit-frame-pointer
compile_and_run ubsan -O1 -fsanitize=undefined -fno-omit-frame-pointer
compile_and_run tsan -O1 -fsanitize=thread -fno-omit-frame-pointer

grep -q 'BOOT_STATE_INIT_ENTRY(BS_OS_RESUME_CHECK, BS_ON_ENTRY, mor_before_bootmem' \
	"$root/src/lib/payload_mm_authvar_mor_linear.c"
grep -q 'BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_EXIT, mor_after_bootmem' \
	"$root/src/lib/payload_mm_authvar_mor_linear.c"
! grep -R -q 'select PAYLOAD_MM_AUTHVAR_MOR_LINEAR_ORCHESTRATOR' \
	"$root/src/mainboard" "$root/src/soc"
probe_line=$(grep -n 'payload_mm_authvar_mor_probe_entry(&entry)' \
	"$root/src/lib/payload_mm_authvar_mor_linear.c" | cut -d: -f1)
reserve_line=$(grep -n 'frozen_ops.reservations_register' \
	"$root/src/lib/payload_mm_authvar_mor_linear.c" | cut -d: -f1)
test "$probe_line" -lt "$reserve_line"

mutant_test()
{
	name=$1
	shift
	mutant="$temporary/$name.c"
	sed "$@" "$root/src/lib/payload_mm_authvar_mor_linear.c" > "$mutant"
	if cmp -s "$root/src/lib/payload_mm_authvar_mor_linear.c" "$mutant"; then
		echo "ERROR: $name mutation was not applied" >&2
		exit 1
	fi
	"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror \
		-Wno-unused-function -Wno-unused-but-set-variable -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -pthread \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/payload_mm_authvar_mor_linear_test.c" "$mutant" \
		-o "$temporary/$name"
	if "$temporary/$name" >/dev/null 2>&1; then
		echo "ERROR: $name mutation survived" >&2
		exit 1
	fi
}

mutant_test probe-error \
	-e 's/payload_mm_authvar_mor_probe_entry(&entry) != CB_SUCCESS/false/'
mutant_test s3-no-close \
	-e 's/return close_authority(state, LIFECYCLE_CLASSIFYING);/return PAYLOAD_MM_AUTHVAR_MOR_LINEAR_CONTINUE;/'
mutant_test clear-error \
	-e '/&state->grant) != CB_SUCCESS ||/s/!=/==/'
mutant_test commit-mutation \
	-e 's/commit_status != CB_SUCCESS || !commit_unchanged/commit_status != CB_SUCCESS/'
mutant_test reentry-no-poison \
	-e '/__atomic_store_n(&lifecycle.phase, LIFECYCLE_FAILED, __ATOMIC_RELEASE);/d'
mutant_test owner-substitution \
	-e 's/__atomic_load_n(&lifecycle.owner, __ATOMIC_ACQUIRE)/(uintptr_t)state/g'
