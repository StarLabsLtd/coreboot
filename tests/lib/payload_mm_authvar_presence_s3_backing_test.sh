#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
mkdir -p "$temporary/config"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_BOOTMEM_DRAM_PROVENANCE 1' > "$temporary/config.h"
cp "$temporary/config.h" "$temporary/config/config.h"
includes="-I$temporary -I$root/src/include -I$root/src -I$root/src/commonlib/include -I$root/src/commonlib/bsd/include -I$root/src/arch/x86/include"
sources="$root/src/lib/payload_mm_authvar_presence_s3_backing.c $root/src/lib/payload_mm_authvar_presence.c $root/src/lib/payload_mm_authvar_presence_lifecycle_close_endpoint.c $root/src/cpu/x86/smm_invocation_loader_instance.c $root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_s3_backing.c $root/src/commonlib/bsd/ipchksum.c"
common="-std=gnu11 -Wall -Wextra -Werror -D__COREBOOT__ -D__TEST__ -include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h -include $root/src/include/kconfig.h -include $root/src/include/rules.h -no-pie $includes"
production_common=$(printf '%s\n' "$common" | sed 's/-D__TEST__ //')

for optimization in 0 2; do
	# shellcheck disable=SC2086
	cc $common -O$optimization $sources \
		"$root/tests/lib/payload_mm_authvar_presence_s3_backing_test.c" \
		-o "$temporary/test-O$optimization"
	"$temporary/test-O$optimization"
done

# shellcheck disable=SC2086
cc $common -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $sources \
	"$root/tests/lib/payload_mm_authvar_presence_s3_backing_test.c" \
	-o "$temporary/test-sanitize"
ASAN_OPTIONS=detect_leaks=0 "$temporary/test-sanitize"

for source in $root/src/lib/payload_mm_authvar_presence_s3_backing.c; do
	# shellcheck disable=SC2086
	cc -m32 $common -ffreestanding -c "$source" \
		-o "$temporary/$(basename "$source").o"
done
if nm -u "$temporary"/*.o | grep -Eq '__atomic|__sync'; then
	echo 'S3 backing proof gained an atomic runtime dependency' >&2
	exit 1
fi

# Production-stage linkage must not rely on ENV_TEST hooks or hosted atomics.
printf '%s\n' 'int main(void) { return 0; }' > "$temporary/production_main.c"
production_sources="$root/src/lib/payload_mm_authvar_presence_s3_backing.c $root/src/lib/payload_mm_authvar_presence.c $root/src/lib/payload_mm_authvar_presence_lifecycle_close_endpoint.c $root/src/cpu/x86/smm_invocation_loader_instance.c $root/src/commonlib/bsd/ipchksum.c"
for stage in RAMSTAGE SMM; do
	# shellcheck disable=SC2086
	cc $production_common -D__${stage}__ -O2 $production_sources \
		"$temporary/production_main.c" -o "$temporary/production-$stage"
	"$temporary/production-$stage"
done

# With the option disabled, the protected runtime ABI is byte-for-byte the base ABI.
git -C "$root" show \
	759375bbfb525e7166fea4c46e8f7863111c5ba1:src/include/cpu/x86/smm.h \
	> "$temporary/smm-base.h"
printf '%s\n' '#include <cpu/x86/smm.h>' > "$temporary/smm-current.c"
printf '#include "%s"\n' "$temporary/smm-base.h" > "$temporary/smm-base.c"
# shellcheck disable=SC2086
cc $production_common -D__SMM__ -E -P "$temporary/smm-current.c" \
	-o "$temporary/smm-current.i"
# shellcheck disable=SC2086
cc $production_common -D__SMM__ -E -P "$temporary/smm-base.c" \
	-o "$temporary/smm-base.i"
cmp "$temporary/smm-current.i" "$temporary/smm-base.i"

printf '%s\n' 'authenticated-variable S3 backing proof tests: PASS'
