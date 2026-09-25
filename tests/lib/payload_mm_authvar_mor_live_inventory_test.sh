#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
config="$root/build/tests/tests/lib/payload-mm-authvar-mor-live-inventory-test"

make -C "$root" build-tests/lib/payload-mm-authvar-mor-live-inventory-test >/dev/null

build_and_run()
{
	name=$1
	source=$2
	shift 2
	"${CC:-cc}" -std=gnu23 -Wall -Wextra -Werror -Wundef \
		-Wstrict-prototypes -fno-builtin -fno-pie -fno-pic "$@" \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -D__TEST_SRCOBJ__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$config" -I"$root/tests/include/mocks" -I"$root/tests/include" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$root/build/tests" \
		"$root/tests/lib/payload_mm_authvar_mor_live_inventory_test.c" \
		"$source" "$root/src/lib/payload_mm_authvar_mor_clear_plan.c" \
		-no-pie -o "$temporary/$name"
	for case_name in success failures boundaries owned owned-aliases; do
		if ! "$temporary/$name" "$case_name"; then
			return 1
		fi
	done
	return 0
}

source_file="$root/src/lib/payload_mm_authvar_mor_live_inventory.c"
build_and_run o0 "$source_file" -O0
build_and_run o2 "$source_file" -O2
build_and_run asan "$source_file" -O1 -fsanitize=address -fno-omit-frame-pointer
build_and_run ubsan "$source_file" -O1 -fsanitize=undefined -fno-omit-frame-pointer
build_and_run tsan "$source_file" -O1 -fsanitize=thread -fno-omit-frame-pointer

mutant_test()
{
	name=$1
	old=$2
	new=$3
	mutant="$temporary/$name.c"

	sed "s#$old#$new#" "$source_file" > "$mutant"
	if cmp -s "$source_file" "$mutant"; then
		echo "ERROR: $name mutation was not applied" >&2
		exit 1
	fi
	if build_and_run "$name" "$mutant" -O2 >/dev/null 2>&1; then
		echo "ERROR: $name mutation survived" >&2
		exit 1
	fi
}

mutant_test ram-classification \
	'tag == BM_MEM_RAM' 'tag == BM_MEM_RESERVED'
mutant_test active-firmware-tags \
	'tag == BM_MEM_RAMSTAGE || tag == BM_MEM_TABLE ||' \
	'tag == BM_MEM_RAMSTAGE \&\& tag == BM_MEM_TABLE ||'
mutant_test overlay-coverage \
	'workspace->overlay_covered\[index\] !=' \
	'workspace->request_snapshot.overlays[index].size !='
mutant_test input-mutation \
	'memcmp(\&workspace->request_snapshot, request,' \
	'memcmp(request, request,'
mutant_test unsupported-tag \
	'tag <= BM_MEM_FIRST || tag >= BM_MEM_LAST' 'false'

"${CC:-cc}" -std=gnu23 -O2 -Wall -Wextra -Werror -Wundef \
	-Wstrict-prototypes -fno-builtin -fno-pie -fno-pic \
	-DMOCK_PLAN_BUILDER -D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
	-D__TEST_SRCOBJ__ -include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$config" -I"$root/tests/include/mocks" -I"$root/tests/include" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$root/build/tests" \
	"$root/tests/lib/payload_mm_authvar_mor_live_inventory_test.c" \
	"$source_file" -no-pie -o "$temporary/raw-count"
"$temporary/raw-count" raw-count

"${CC:-cc}" -std=gnu11 -Os -m32 -Wall -Wextra -Werror -Wundef \
	-Wstrict-prototypes -fno-builtin -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$config" -I"$root/src" -I"$root/src/include" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" -I"$root/build/tests" \
	-fstack-usage -c "$source_file" -o "$temporary/live-inventory.o"
nm -g --defined-only "$temporary/live-inventory.o" |
	grep -q ' payload_mm_authvar_mor_live_inventory_compose_owned$'
if nm -g --defined-only "$temporary/live-inventory.o" |
	grep -q ' payload_mm_authvar_mor_live_inventory_compose$'; then
	echo 'ERROR: stack-allocating live-inventory wrapper reached ramstage' >&2
	exit 1
fi
owned_stack=$(awk -F '\t' \
	'$1 ~ /:payload_mm_authvar_mor_live_inventory_compose_owned$/ { print $2 }' \
	"$temporary/live-inventory.su")
if test -z "$owned_stack" || test "$owned_stack" -gt 256; then
	echo "ERROR: owned live-inventory stack bound missing or exceeded: ${owned_stack:-missing}" >&2
	exit 1
fi

echo 'payload MM MOR live-inventory validation: PASS'
