#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
ulimit -c 0
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

cases='happy install-owner install-protection install-revision install-size
install-guid install-version install-floor install-image-size install-capabilities
install-reserved install-overlap install-source-mutation install-context-mutation
install-authority-mutation
read-failure read-record read-absent read-cold-version read-cold-attempt
read-cold-floor-newer read-version-stale read-floor-newer read-floor-running-newer
read-authority-mutation read-current-mutation
read-capabilities-mutation read-lifecycle-mutation read-output read-null read-misaligned
read-reentry read-no-attempt read-updated-state read-policy-floor'

build_test()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin -fno-pie -no-pie "$@" \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/capsule_broker_info_test.c" \
		"$info_source" -o "$temporary/$name"
}

run_test()
{
	name=$1
	shift
	build_test "$name" "$@"
	for test_case in $cases; do
		"$temporary/$name" "$test_case"
	done
}

info_source="$root/src/lib/capsule_broker_info.c"
run_test ordinary
run_test optimized -O2
run_test strict -O2 -Wconversion -Wsign-conversion
run_test sanitized -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
printf '%s\n' 'Capsule broker sealed INFO O0/O2/strict/ASan+UBSan: PASS'

for mutation in baseline present stale; do
	python3 - "$root/src/lib/capsule_broker_info.c" "$temporary" "$mutation" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_bytes()
directory = Path(sys.argv[2])
name = sys.argv[3]
changes = {
    "baseline": (b"durable_version = record.data[0] ? read32(record.data + 4) :",
                 b"durable_version = true ? read32(record.data + 4) :"),
    "present": (b"!record.present)", b"false)"),
    "stale": (b"if (durable_version < policy.current_version)", b"if (false)"),
}
before, after = changes[name]
if source.count(before) != 1:
    raise SystemExit("mutation must match exactly once")
changed = source.replace(before, after)
if changed.count(after) != 1:
    raise SystemExit("reverse mutation must match exactly once")
(directory / (name + ".c")).write_bytes(changed)
(directory / (name + ".restored")).write_bytes(changed.replace(after, before))
PY
	cmp "$root/src/lib/capsule_broker_info.c" "$temporary/$mutation.restored"
	case "$mutation" in
	baseline) scenario=read-cold-version; diagnostic='capsule_broker_info_read(&info) == CB_SUCCESS' ;;
	present) scenario=read-absent; diagnostic='capsule_broker_info_read(&info) == CB_ERR' ;;
	stale) scenario=read-version-stale; diagnostic='capsule_broker_info_read(&info) == CB_ERR' ;;
	esac
	info_source="$temporary/$mutation.c"
	for optimization in 0 2; do
		name="$mutation-O$optimization"
		build_test "$name" "-O$optimization" -g -fno-omit-frame-pointer \
			-fsanitize=address,undefined -fno-sanitize-recover=all
		status=0
		"$temporary/$name" "$scenario" > "$temporary/$name.log" 2>&1 || status=$?
		if test "$status" -ne 134 ||
		   ! rg -F "$diagnostic" "$temporary/$name.log" > /dev/null ||
		   rg 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
			"$temporary/$name.log" > /dev/null; then
			printf '%s\n' "INFO causal failure: $name ($status)" >&2
			exit 1
		fi
		printf '%s\n' "INFO $name targeted refusal: PASS"
	done
done
