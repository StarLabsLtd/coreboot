#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
candidate=${1:?candidate source root}
header=${2:?real resolved config header}
shift 2
proof=/home/sean/phase-owned-lvgl-proof.CuEvQK
configuration=$(dirname "$(dirname "$header")")
test "$#" -gt 0
for source in "$@"; do
	case "$source" in *.c) test -f "$source" ;; *) exit 2 ;; esac
done
grep -qx '#define CONFIG_CDK2_LINEAR_SETUP_HOTKEY 1' "$header"
grep -qx '#define CONFIG_CDK2_STRICT_DIRECT_RUNTIME 1' "$header"
sha256sum "$candidate/src/modules/dxe_core/entry.c" "$candidate/tests/splash_status_report_test.c" \
	"$header" "$proof/admission.c" "$proof/run-admission.sh" > "$proof/admission-before.sha256"
for source in "$@"; do
	sha256sum "$source" >> "$proof/admission-before.sha256"
done
# All tracked candidate sources/headers plus every actual supplied TU's Git
# source tree cover relative private-header and public include resolution.
declare -A trees=()
trees["$(git -C "$candidate" rev-parse --show-toplevel)"]=1
for source in "$@"; do
	trees["$(git -C "$(dirname "$source")" rev-parse --show-toplevel)"]=1
done
for tree in "${!trees[@]}"; do
	while IFS= read -r -d '' path; do
		if test -f "$tree/$path"; then
			sha256sum "$tree/$path" >> "$proof/admission-before.sha256"
		fi
	done < <(git -C "$tree" ls-files -z)
done

# Exactly one complete, verbatim production block; the outer HOTKEY opening
# immediately precedes the unique generation assignment. No body is rewritten.
awk '
	/^\tui_generation = core.images.next_handle;$/ {
		if (previous != "#if CONFIG_CDK2_LINEAR_SETUP_HOTKEY" || starts++) exit 2
		print previous; inside = 1
	}
	inside && /^\tif \(core.runtime_storage == NULL\)$/ { ends++; inside = 0; exit }
	inside { print }
	{ previous = $0 }
	END { if (starts != 1 || ends != 1 || inside) exit 2 }
' "$candidate/src/modules/dxe_core/entry.c" > "$proof/admission-original.h"

perl -0pe 's/memcmp\(status_owner->transaction->source, ui_source, ui_source_size\) != 0/false/g == 1 or die "source equality selector changed\n"' \
	"$proof/admission-original.h" > "$proof/admission-source.h"
perl -0pe 's/\|\|\n\t    false/||\n\t    memcmp(status_owner->transaction->source, ui_source, ui_source_size) != 0/g == 1 or die "source inverse changed\n"' \
	"$proof/admission-source.h" > "$proof/admission-source-inverse.h"
cmp "$proof/admission-original.h" "$proof/admission-source-inverse.h"

perl -0pe 's/status_owner->load_generation != ui_generation/false/g == 1 or die "generation selector changed\n"' \
	"$proof/admission-original.h" > "$proof/admission-generation.h"
perl -0pe 's/\|\|\n\t    false/||\n\t    status_owner->load_generation != ui_generation/g == 1 or die "generation inverse changed\n"' \
	"$proof/admission-generation.h" > "$proof/admission-generation-inverse.h"
cmp "$proof/admission-original.h" "$proof/admission-generation-inverse.h"

perl -0pe 's/EFI_ERROR\(cdk2_dxe_image_transaction_validate_owned\(&core.images,\n\t\tstatus_owner->transaction, status_owner->transaction_allocation_generation\)\)/false/g == 1 or die "transaction selector changed\n"' \
	"$proof/admission-original.h" > "$proof/admission-transaction.h"
perl -0pe 's/\|\|\n\t    false/||\n\t    EFI_ERROR(cdk2_dxe_image_transaction_validate_owned(\&core.images,\n\t\tstatus_owner->transaction, status_owner->transaction_allocation_generation))/g == 1 or die "transaction inverse changed\n"' \
	"$proof/admission-transaction.h" > "$proof/admission-transaction-inverse.h"
cmp "$proof/admission-original.h" "$proof/admission-transaction-inverse.h"
ulimit -c 0

for optimization in 0 2; do
	for mode in original source generation transaction; do
		cc -std=c11 -O"$optimization" -Wall -Wextra -Werror -fshort-wchar -m64 \
			-ffunction-sections -fdata-sections -Wl,--gc-sections -fno-pie -no-pie \
			-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
			-DCDK2_HOST_TEST -DCDK2_SPLASH_TEST_DEBUG=1 \
			-DCDK2_SPLASH_FIXTURE_SOURCE="\"$candidate/tests/splash_status_report_test.c\"" \
			-DCDK2_UI_ADMISSION_BLOCK="\"$proof/admission-$mode.h\"" \
			-I"$configuration" -I"$candidate/include" -I"$candidate/src/modules/dxe_core" \
			-I"$candidate/src/lib/tcg_hash/vendor/linux/include" \
			"$proof/admission.c" "$@" -o "$proof/admission-$mode-o$optimization"
		status=0
		ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$proof/admission-$mode-o$optimization" \
			> "$proof/admission-$mode-o$optimization.log" 2>&1 || status=$?
		if test "$mode" = original; then
			test "$status" = 0
			continue
		fi
		test "$status" = 134
		case "$mode" in
		source) reason='admission source equality refusal' ;;
		generation) reason='admission first-load generation refusal' ;;
		transaction) reason='admission owned transaction refusal' ;;
		esac
		grep -Fq "$reason" "$proof/admission-$mode-o$optimization.log"
		! grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
			"$proof/admission-$mode-o$optimization.log"
	done
done
sha256sum -c "$proof/admission-before.sha256"
echo 'Verbatim strict HOST admission block O0/O2/SAN and six exact causes: PASS'
