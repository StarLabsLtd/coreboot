#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

scratch_make()
{
	env -u MAKELEVEL -u MAKEFLAGS -u MFLAGS -u MAKEOVERRIDES \
		"${MAKE:-make}" "$@"
}
mkdir -p "$temporary/include"
printf '#define %s %s\n' CONFIG_STARLABS_STARBOOK_MTL_MOR_DMA_GUARD 0 > \
	"$temporary/include/config.h"
printf '#define %s %s\n' CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0 >> \
	"$temporary/include/config.h"

run_suite()
{
	binary=$1
	for case_name in presence-classification presence-classification-mutation \
		cold high-zero s3 publish-race capture-reuse random-failure \
		zero-generation \
		publish-capture-record-alias publish-ops-record-alias \
		publish-capture-ops-alias publish-context-record-alias \
		publish-context-record-interior publish-context-capture-alias \
		publish-context-capture-interior publish-context-ops-alias \
		publish-limit-failure publish-quiesce-failure publish-limit-change \
		publish-state-mutation \
		short-entry wrong-base mutation ops-mutation corrupt-mirror high-mutation \
		consume-limit-failure consume-quiesce-failure consume-limit-change \
		output-record-alias output-ops-alias output-context-alias \
		consume-ops-record-alias consume-context-record-alias \
		consume-context-record-interior consume-context-ops-alias \
		consume-context-output-alias; do
		"$binary" "$case_name"
	done
}

for flags in '-O0' '-O2' '-O1 -fsanitize=address' \
	'-O1 -fsanitize=undefined'; do
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin -pthread $flags \
		-D__TEST__ -D__COREBOOT__ -D__BOOTBLOCK__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/soc/intel/meteorlake/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
		-I"$temporary/include" \
		"$root/tests/lib/starbook_mtl_mor_cold_boot_test.c" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/loader_instance_source.c" \
		-o "$temporary/test"
	run_suite "$temporary/test"
done

"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin -pthread \
	-O1 -fsanitize=thread -D__TEST__ -D__COREBOOT__ -D__BOOTBLOCK__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/soc/intel/meteorlake/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
	-I"$temporary/include" "$root/tests/lib/starbook_mtl_mor_cold_boot_test.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/loader_instance_source.c" \
	-o "$temporary/tsan"
TSAN_OPTIONS=halt_on_error=1 "$temporary/tsan" publish-race

mutant()
{
	name=$1
	expression=$2
	case_name=$3
	source="$temporary/$name.c"
	binary="$temporary/$name"
	sed "$expression" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/loader_instance_source.c" > \
		"$source"
	! cmp -s "$source" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/loader_instance_source.c"
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin -pthread -O2 \
		-D__TEST__ -D__COREBOOT__ -D__BOOTBLOCK__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
		-I"$temporary/include" \
		-I"$root/src/soc/intel/meteorlake/include" \
		"$root/tests/lib/starbook_mtl_mor_cold_boot_test.c" "$source" \
		-o "$binary"
	if "$binary" "$case_name" >/dev/null 2>&1; then
		echo "ERROR: $name mutant survived" >&2
		exit 1
	fi
}

mutant s3-authority \
	's/if (boot_kind != STARBOOK_MTL_MOR_BOOT_COLD)/if (false)/' s3
mutant mirror-seal \
	's/memcmp(&saved.primary, &saved.mirror, sizeof(saved.primary))/false/' \
	corrupt-mirror
mutant callback-mutation \
	's/memcmp(&saved, record, sizeof(saved))/false/' mutation
mutant order-limit \
	's/final_limit != first_limit/false/' consume-limit-change
mutant one-shot-wipe \
	's/STARBOOK_MTL_LOADER_INSTANCE_SOURCE_CONSUMED);/STARBOOK_MTL_LOADER_INSTANCE_SOURCE_READY);/' \
	cold
mutant capture-record-alias \
	's/objects_overlap(capture, sizeof(\*capture), record, sizeof(\*record)) ||/false ||/' \
	publish-capture-record-alias
mutant nonce-high-seal \
	's/((nonce.high << 17) | (nonce.high >> 47))/0/' high-mutation
mutant capture-claim \
	'/compare_exchange_n(&capture->captured/,/goto out;/{s/if (!__atomic_compare_exchange_n/if (false \&\& !__atomic_compare_exchange_n/;}' \
	publish-race
mutant publish-state-cleanup \
	'/static void record_publish_failed/,/^}/{s/record_scrub_tail(record, STARBOOK_MTL_LOADER_INSTANCE_SOURCE_EMPTY);/return;/;}' \
	publish-state-mutation
cp "$root/src/Kconfig" "$temporary/Kconfig"
printf '\nconfig TEST_MTL_MOR_COLD_SELECTOR\n\tbool\n\tdefault y\n\tselect STARLABS_STARBOOK_MTL_MOR_COLD_CLASSIFICATION\n' >> \
	"$temporary/Kconfig"
cp "$root/configs/config.starlabs_starbook_mtl" "$temporary/config"
printf '%s\n' 'CONFIG_ENABLE_EARLY_DMA_PROTECTION=y' >> "$temporary/config"
scratch_make -s -C "$root" KBUILD_KCONFIG="$temporary/Kconfig" \
	DOTCONFIG="$temporary/config" obj="$temporary/obj" olddefconfig
for required in STARLABS_STARBOOK_MTL_MOR_COLD_CLASSIFICATION \
	STARLABS_STARBOOK_MTL_LOADER_INSTANCE_SOURCE \
	PCI_BME_QUIESCE RANDOM_GENERATOR_IN_ROMSTAGE \
	SOC_INTEL_METEORLAKE_LOADER_INSTANCE_SOURCE; do
	grep -q "^CONFIG_$required=y$" "$temporary/config"
done
! grep -q '^CONFIG_STARLABS_STARBOOK_MTL_MOR_DMA_GUARD=y$' "$temporary/config"

romstage_source="$root/src/soc/intel/meteorlake/romstage/romstage.c"
romstage_header="$root/src/soc/intel/meteorlake/include/soc/romstage.h"

check_publication_policy()
{
	source=$1
	publication=$(grep -n 'mainboard_loader_instance_source_publish()' "$source" | \
		cut -d: -f1)
	policy=$(grep -nF 'if (result != CB_SUCCESS &&' "$source" | cut -d: -f1)
	failure=$(grep -n 'die("MTL loader-instance source' "$source" | cut -d: -f1)
	grep -q '!s3wake || CONFIG(SOC_INTEL_METEORLAKE_LOADER_INSTANCE_REQUIRED)' \
		"$source"
	test "$publication" -lt "$policy"
	test "$policy" -lt "$failure"
}

check_publication_policy "$romstage_source"
! grep -q 'starbook_mtl' "$romstage_source" "$romstage_header"
capture_line=$(grep -n 'mainboard_loader_instance_source_capture(s3wake, ps)' \
	"$romstage_source" |
	cut -d: -f1)
fspm_line=$(grep -n '^[[:space:]]*fsp_memory_init(s3wake)' "$romstage_source" |
	cut -d: -f1)
vtd_line=$(grep -n '^[[:space:]]*vtd_enable_dma_protection()' "$romstage_source" |
	cut -d: -f1)
publish_line=$(grep -n 'mainboard_loader_instance_source_publish()' \
	"$romstage_source" |
	cut -d: -f1)
test "$capture_line" -lt "$fspm_line"
test "$fspm_line" -lt "$vtd_line"
test "$vtd_line" -lt "$publish_line"
test "$(grep -R -E -n --include='*.c' --include='*.h' \
	'starbook_mtl_mor_cold_ramstage_consume\(' "$root/src" | wc -l)" -eq 3
test "$(grep -R -E -n --include='*.c' --include='*.h' \
	'starbook_mtl_mor_cold_ramstage_consume_snapshot\(' "$root/src" | wc -l)" -eq 2

printf '%s\n' 'StarBook MTL MOR cold-boot tests: PASS'
