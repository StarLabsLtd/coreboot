#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
mkdir -p "$temporary/config"
printf '%s\n' '#define CONFIG_BOOTMEM_ALIGNED_RESERVATIONS 1' \
	'#define CONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT 1' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/config.h"
cp "$temporary/config.h" "$temporary/config/config.h"
includes="-I$temporary -I$root/src/include -I$root/src -I$root/src/commonlib/include -I$root/src/commonlib/bsd/include -I$root/src/arch/x86/include -I$root/src/lib"
sources="$root/src/lib/payload_mm_authvar_presence_lifecycle_close_endpoint.c $root/src/lib/payload_mm_authvar_presence_lifecycle_close_backing.c $root/src/lib/payload_mm_authvar_presence_lifecycle_close_publication.c"
common="-std=gnu11 -Wall -Wextra -Werror -pthread -D__COREBOOT__ -D__TEST__ -include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h -include $root/src/include/kconfig.h -include $root/src/include/rules.h -no-pie $includes"

for optimization in 0 2; do
	# shellcheck disable=SC2086
	cc $common -O$optimization $sources \
		"$root/tests/lib/payload_mm_authvar_presence_lifecycle_close_endpoint_test.c" \
		-o "$temporary/test-O$optimization"
	"$temporary/test-O$optimization"
done

# shellcheck disable=SC2086
cc $common -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $sources \
	"$root/tests/lib/payload_mm_authvar_presence_lifecycle_close_endpoint_test.c" \
	-o "$temporary/test-sanitize"
ASAN_OPTIONS=detect_leaks=0 "$temporary/test-sanitize"
# shellcheck disable=SC2086
cc $common -O1 -fsanitize=thread $sources \
	"$root/tests/lib/payload_mm_authvar_presence_lifecycle_close_endpoint_test.c" \
	-o "$temporary/test-tsan"
TSAN_OPTIONS=halt_on_error=1 "$temporary/test-tsan"

mutant()
{
	name=$1
	file=$2
	expression=$3
	mutated="$temporary/$name.c"
	sed "$expression" "$file" > "$mutated"
	if cmp -s "$file" "$mutated"; then
		echo "lifecycle-close endpoint mutant did not change source: $name" >&2
		exit 1
	fi
	mutant_sources=$sources
	mutant_sources=$(printf '%s\n' "$mutant_sources" | sed "s|$file|$mutated|")
	# Every mutant must compile; only behavioral rejection counts as a kill.
	# shellcheck disable=SC2086
	cc $common -O2 $mutant_sources \
		"$root/tests/lib/payload_mm_authvar_presence_lifecycle_close_endpoint_test.c" \
		-o "$temporary/$name"
	if "$temporary/$name" >/dev/null 2>&1; then
		echo "lifecycle-close endpoint mutant survived: $name" >&2
		exit 1
	fi
}

endpoint_source="$root/src/lib/payload_mm_authvar_presence_lifecycle_close_endpoint.c"
backing_source="$root/src/lib/payload_mm_authvar_presence_lifecycle_close_backing.c"
mutant flags-exact "$endpoint_source" \
	's/endpoint->flags != LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_REQUIRED_FLAGS/false/'
mutant source-onehot "$endpoint_source" \
	's/!(source & (source - 1U))/true/'
mutant unknown-status "$endpoint_source" \
	's/snapshot.status != PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SECURITY_VIOLATION)/false)/'
mutant receipt-identity "$backing_source" \
	's/receipt->identity == owner.receipt.identity/true/'
mutant receipt-nonce "$backing_source" \
	's/receipt->nonce == owner.receipt.nonce/true/'
mutant receipt-reserved "$backing_source" \
	's/receipt->active == 1 && !receipt->reserved/receipt->active == 1/'

# The ABI dump is a checked handoff artifact for a future CDK2 implementation.
# shellcheck disable=SC2086
cc $common -O2 "$root/tests/lib/payload_mm_authvar_presence_lifecycle_close_layout_dump.c" \
	-o "$temporary/layout"
"$temporary/layout" > "$temporary/layout.tsv"
cat > "$temporary/expected.tsv" <<'EOF'
endpoint	64	generation=16	base=24	source_mask=52
message	64	align=8	status=32	completion=60
EOF
cmp "$temporary/expected.tsv" "$temporary/layout.tsv"

# Strict 32-bit freestanding objects must not introduce libatomic dependencies.
for source in $sources; do
	# shellcheck disable=SC2086
	cc -m32 -std=gnu11 -Wall -Wextra -Werror -ffreestanding -D__COREBOOT__ \
		-D__TEST__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-include "$root/src/include/rules.h" $includes -c "$source" \
		-o "$temporary/$(basename "$source").o"
done
if nm -u "$temporary"/*.o | grep -Eq '__atomic|__sync'; then
	echo 'lifecycle-close endpoint gained a runtime atomic dependency' >&2
	exit 1
fi

scope="$root/src/lib/payload_mm_authvar_presence_lifecycle_close_endpoint.c $root/src/lib/payload_mm_authvar_presence_lifecycle_close_backing.c $root/src/lib/payload_mm_authvar_presence_lifecycle_close_publication.c $root/src/include/boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h $root/src/include/boot/payload_mm_authvar_presence_lifecycle_close_publication.h"
if grep -Eiq 'outb|route[_ (]|sender|selector|board[_ -]enable|LIFECYCLE_SEALED' $scope; then
	echo 'lifecycle-close endpoint exceeded dormant publication scope' >&2
	exit 1
fi
grep -q 'LB_TAG_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT = 0x0057' \
	"$root/src/commonlib/include/commonlib/coreboot_tables.h"
grep -q 'BOOT_STATE_INIT_ENTRY(BS_PRE_DEVICE, BS_ON_EXIT' \
	"$root/src/lib/payload_mm_authvar_presence_lifecycle_close_publication.c"
printf '%s\n' 'payload_mm authvar presence lifecycle-close endpoint tests: PASS'
