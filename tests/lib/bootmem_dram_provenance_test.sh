#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
config="$root/build/tests/tests/lib/bootmem-dram-provenance-test"

make -C "$root" build-tests/lib/bootmem-dram-provenance-test >/dev/null

build_and_run()
{
	name=$1
	bootmem_source=$2
	shift 2
	"${CC:-cc}" -std=gnu23 -Wall -Werror -Wundef -Wstrict-prototypes \
		-fno-builtin -ffunction-sections -fdata-sections -fno-pie -fno-pic \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -D__TEST_SRCOBJ__ "$@" \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$config" -I"$root/tests/include/mocks" -I"$root/tests/include" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$root/3rdparty/vboot/firmware/include" \
		-I"$root/build/tests" \
		"$root/tests/lib/bootmem_dram_provenance_test.c" \
		"$root/src/device/device_util.c" "$bootmem_source" \
		"$root/src/lib/memrange.c" -Wl,--gc-sections -no-pie \
		-o "$temporary/$name"
	if ! "$temporary/$name"; then
		return 1
	fi
	if "$temporary/$name" overflow >/dev/null 2>&1; then
		echo "ERROR: overflowing domain resource was accepted" >&2
		return 1
	fi
	return 0
}

source_file="$root/src/lib/bootmem.c"
build_and_run o0 "$source_file" -O0
build_and_run o2 "$source_file" -O2
build_and_run asan "$source_file" -O1 -fsanitize=address \
	-fno-omit-frame-pointer
build_and_run ubsan "$source_file" -O1 -fsanitize=undefined \
	-fno-omit-frame-pointer

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

mutant_test domain-only \
	'return dev->path.type == DEVICE_PATH_DOMAIN \&\& res->size \&\&' \
	'return res->size \&\&'
mutant_test pre-insert-overflow-bound \
	'res->base <= UINT64_MAX - res->size;' \
	'true;'
mutant_test page-expanded-provenance \
	'memranges_init_empty_with_alignment(\&bootmem_dram, NULL, 0, 0);' \
	'memranges_init_empty(\&bootmem_dram, NULL, 0);'
mutant_test provenance-source \
	'memranges_each_entry(dram, \&bootmem_dram)' \
	'memranges_each_entry(dram, \&bootmem)'
mutant_test intersection-base \
	'base = MAX(range_entry_base(dram), range_entry_base(final));' \
	'base = range_entry_base(final);'
mutant_test intersection-end \
	'end = MIN(range_entry_end(dram), range_entry_end(final));' \
	'end = range_entry_end(final);'
mutant_test final-tag \
	'range_entry_tag(final));' \
	'range_entry_tag(dram));'

"${CC:-cc}" -std=gnu11 -Os -m32 -Wall -Werror -Wundef \
	-Wstrict-prototypes -fno-builtin -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$config" -I"$root/src" -I"$root/src/include" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" -I"$root/3rdparty/vboot/firmware/include" \
	-I"$root/build/tests" -c "$source_file" -o "$temporary/bootmem.o"
nm -g --defined-only "$temporary/bootmem.o" |
	grep -q ' bootmem_walk_dram$'

echo 'bootmem DRAM provenance tests: PASS'
