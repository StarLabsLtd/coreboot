#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '#define %s %s\n' CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0 > \
	"$temporary/include/config.h"

cases='validator misaligned success close-before-install close-after-install
take-success take-unprotected take-output-nonzero take-mutate-output
take-mutate-grant take-mutate-candidate take-mutate-state take-mutate-context
take-null-output take-misaligned-output take-null-callback take-context-alias
take-context-too-large
take-output-authority-alias take-output-callback-alias
discard-ready discard-empty discard-after-take discard-unprotected
discard-mutate-grant discard-mutate-candidate discard-mutate-state
discard-mutate-context discard-recursive discard-null-callback
discard-context-null-size discard-context-too-large
discard-context-authority-alias discard-context-callback-alias
discard-callback-authority-alias discard-malformed-grant discard-dirty-candidate
consume-mismatch-0 consume-mismatch-1
consume-mismatch-2 consume-mismatch-3 consume-mismatch-4 consume-mismatch-5 consume-null
consume-alias
install-unprotected install-mutate-grant install-mutate-authority
install-mutate-candidate install-malformed install-bad-0 install-bad-1 install-bad-2'

build_and_run()
{
	name=$1
	source=$2
	shift 2
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin \
		-ffunction-sections -fdata-sections -Wl,--gc-sections "$@" \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$temporary/include" \
		"$root/tests/lib/payload_mm_authvar_mor_grant_test.c" "$source" \
		-o "$temporary/$name"
	for case_name in $cases; do
		if ! "$temporary/$name" "$case_name"; then
			return 1
		fi
	done
}

source_file="$root/src/lib/payload_mm_authvar_mor_grant.c"
build_and_run o0 "$source_file" -O0
build_and_run o2 "$source_file" -O2
build_and_run asan "$source_file" -O1 -fsanitize=address -fno-omit-frame-pointer
build_and_run ubsan "$source_file" -O1 -fsanitize=undefined -fno-omit-frame-pointer

"${CC:-cc}" -std=gnu11 -g -O2 -Wall -Wextra -Werror -fno-builtin \
	-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" \
	"$root/tests/lib/payload_mm_authvar_mor_grant_range_test.c" \
	-o "$temporary/range"
"$temporary/range"

"${CC:-cc}" -std=gnu11 -Os -m32 -Wall -Wextra -Werror -fno-builtin \
	-fstack-usage -D__COREBOOT__ -D__SMM__ \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" -c "$source_file" -o "$temporary/smm-stack.o"
awk -F '\t' '
	$1 ~ /:payload_mm_authvar_mor_grant_install$/ ||
	$1 ~ /:payload_mm_authvar_mor_grant_close$/ ||
	$1 ~ /:payload_mm_authvar_mor_grant_consume$/ ||
	$1 ~ /:payload_mm_authvar_mor_grant_take$/ ||
	$1 ~ /:payload_mm_authvar_mor_grant_discard$/ ||
	$1 ~ /:grant_snapshot_valid([.][^:]*)?$/ {
		if (!seen[$1]++)
			count++
		if ($2 + 0 > 128) {
			print "ERROR: SMM stack bound exceeded by " $1 ": " $2 > "/dev/stderr"
			exit 1
		}
	}
	END {
		if (count != 6) {
			print "ERROR: incomplete SMM stack-usage evidence" > "/dev/stderr"
			exit 1
		}
	}
' "$temporary/smm-stack.su"

mutant_test()
{
	name=$1
	old=$2
	new=$3
	case_name=$4
	mutant="$temporary/$name.c"
	sed "s#$old#$new#" "$source_file" > "$mutant"
	if cmp -s "$source_file" "$mutant"; then
		echo "ERROR: $name mutation was not applied" >&2
		exit 1
	fi
	if build_and_run "$name" "$mutant" -O2 >/dev/null 2>&1; then
		echo "ERROR: $name mutation survived ($case_name)" >&2
		exit 1
	fi
}

mutant_test flags-exact \
	'snapshot->flags != PAYLOAD_MM_AUTHVAR_MOR_GRANT_REQUIRED_FLAGS' \
	'(snapshot->flags \& PAYLOAD_MM_AUTHVAR_MOR_GRANT_REQUIRED_FLAGS) != PAYLOAD_MM_AUTHVAR_MOR_GRANT_REQUIRED_FLAGS' \
	validator
mutant_test cleared-required \
	'(\!snapshot->cleared_bytes || \!snapshot->cleared_spans)' \
	'false' validator
mutant_test unused-spans \
	'if (!bytes_zero(\&snapshot->spans\[i\], sizeof(snapshot->spans\[i\])))' \
	'if (false)' validator
mutant_test install-recheck \
	'memcmp(\&authority.candidate, trusted_grant,' \
	'memcmp(\&authority.candidate, \&authority.candidate,' \
	install-mutate-grant
mutant_test consume-binding \
	'!memcmp(\&authority.candidate, \&authority.grant,' \
	'!memcmp(\&authority.candidate, \&authority.candidate,' \
	consume-mismatch-1
mutant_test close-one-shot \
	'if (authority.install_attempted || authority.poisoned)' \
	'if (false)' close-after-install
mutant_test take-protection \
	'output_cleanup_safe =' \
	'output_cleanup_safe = true ||' take-unprotected
mutant_test take-output-zero \
	'bytes_zero(output, sizeof(\*output)) \&\&' \
	'true \&\&' take-output-nonzero
mutant_test take-grant-recheck \
	'!memcmp(\&authority.grant, \&authority.candidate,' \
	'!memcmp(\&authority.candidate, \&authority.candidate,' take-mutate-grant
mutant_test take-state-recheck \
	'authority.install_attempted \&\& authority.installed \&\&' \
	'authority.install_attempted \&\& true \&\&' take-mutate-state
mutant_test take-context-recheck \
	'(!context_size || !memcmp(context_snapshot, context, context_size));' \
	'true;' take-mutate-context
mutant_test take-output-mutation \
	'bytes_zero(output, sizeof(\*output)) \&\&' \
	'true \&\&' take-mutate-output
mutant_test take-one-shot \
	'authority.consumed = true;' \
	'authority.consumed = false;' take-success
mutant_test take-cleanup-authority \
	'if (!valid \&\& output_cleanup_safe)' \
	'if (!valid \&\& output_structurally_valid)' take-output-callback-alias
mutant_test discard-protection \
	'valid = storage_is_protected(context, \&authority, sizeof(authority));' \
	'valid = true;' discard-unprotected
mutant_test discard-state-recheck \
	'authority.installed == grant_installed \&\& !authority.consumed \&\&' \
	'true \&\& !authority.consumed \&\&' discard-mutate-state
mutant_test discard-context-recheck \
	'(!context_size || !memcmp(context_snapshot, context, context_size));' \
	'true;' discard-mutate-context
mutant_test discard-idempotence \
	'if (authority.discarded) {' \
	'if (false \&\& authority.discarded) {' discard-ready

mutant_range_test()
{
	name=$1
	old=$2
	new=$3
	mutant="$temporary/$name.c"
	range_source="$temporary/$name-range.c"
	binary="$temporary/$name-range"
	sed "s#$old#$new#" "$source_file" > "$mutant"
	if cmp -s "$source_file" "$mutant"; then
		echo "ERROR: $name mutation was not applied" >&2
		exit 1
	fi
	sed "s#../../src/lib/payload_mm_authvar_mor_grant.c#$mutant#" \
		"$root/tests/lib/payload_mm_authvar_mor_grant_range_test.c" > \
		"$range_source"
	"${CC:-cc}" -std=gnu11 -g -O2 -Wall -Wextra -Werror -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$temporary/include" "$range_source" -o "$binary"
	if "$binary" >/dev/null 2>&1; then
		echo "ERROR: $name mutation survived" >&2
		exit 1
	fi
}

mutant_range_test close-poison \
	'authority.poisoned = true;' \
	'authority.poisoned = false;'
mutant_range_test close-grant-scrub \
	'memset(\&authority.grant, 0, sizeof(authority.grant));' \
	'memset(\&authority.grant, 0xa5, sizeof(authority.grant));'
mutant_range_test close-candidate-scrub \
	'memset(\&authority.candidate, 0, sizeof(authority.candidate));' \
	'memset(\&authority.candidate, 0x5a, sizeof(authority.candidate));'

mkdir -p "$temporary/config-default" "$temporary/build-default" \
	"$temporary/config-grant" "$temporary/build-grant"
cat > "$temporary/config-default/.config" <<'EOF'
CONFIG_VENDOR_EMULATION=y
CONFIG_BOARD_EMULATION_QEMU_X86_Q35=y
CONFIG_ANY_TOOLCHAIN=y
EOF
make -C "$root" obj="$temporary/build-default" \
	DOTCONFIG="$temporary/config-default/.config" olddefconfig >/dev/null
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT=y$' \
	"$temporary/config-default/.config"

cp "$root/src/Kconfig" "$temporary/Kconfig"
cat >> "$temporary/Kconfig" <<'EOF'

config TEST_MOR_GRANT_SELECTOR
	bool
	default y
	select PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT
EOF
cp "$temporary/config-default/.config" "$temporary/config-grant/.config"
make -C "$root" obj="$temporary/build-grant" KBUILD_KCONFIG="$temporary/Kconfig" \
	DOTCONFIG="$temporary/config-grant/.config" olddefconfig >/dev/null
grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT=y' \
	"$temporary/config-grant/.config"
grep -qx 'CONFIG_HAVE_SMI_HANDLER=y' "$temporary/config-grant/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_ENTRY_PROBE=y$' \
	"$temporary/config-grant/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_POLICY=y$' \
	"$temporary/config-grant/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_CONTRACT=y$' \
	"$temporary/config-grant/.config"
! grep -q '^CONFIG_SMMSTORE=y$' "$temporary/config-grant/.config"

make -C "$root" obj="$temporary/build-grant" \
	KBUILD_KCONFIG="$temporary/Kconfig" \
	DOTCONFIG="$temporary/config-grant/.config" -j2 >/dev/null
test -f "$temporary/build-grant/smm/lib/payload_mm_authvar_mor_grant.o"
nm -g --defined-only \
	"$temporary/build-grant/smm/lib/payload_mm_authvar_mor_grant.o" |
	grep -q ' payload_mm_authvar_mor_grant_take$'
nm -g --defined-only \
	"$temporary/build-grant/smm/lib/payload_mm_authvar_mor_grant.o" |
	grep -q ' payload_mm_authvar_mor_grant_discard$'
test -f "$temporary/build-grant/ramstage/lib/payload_mm_authvar_mor_grant.o"
! nm -g --defined-only \
	"$temporary/build-grant/ramstage/lib/payload_mm_authvar_mor_grant.o" |
	grep -q ' payload_mm_authvar_mor_grant_take$'
! nm -g --defined-only \
	"$temporary/build-grant/ramstage/lib/payload_mm_authvar_mor_grant.o" |
	grep -q ' payload_mm_authvar_mor_grant_discard$'

grep -qx 'ramstage-$(CONFIG_PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT) += payload_mm_authvar_mor_grant.c' \
	"$root/src/lib/Makefile.mk"
grep -qx 'smm-$(CONFIG_PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT) += payload_mm_authvar_mor_grant.c' \
	"$root/src/lib/Makefile.mk"
test "$(rg -n 'payload_mm_authvar_mor_grant[.]c' "$root/src" \
	-g Makefile.mk | wc -l)" -eq 2

nm -g --defined-only "$temporary/smm-stack.o" |
	grep -q ' payload_mm_authvar_mor_grant_take$'
nm -g --defined-only "$temporary/smm-stack.o" |
	grep -q ' payload_mm_authvar_mor_grant_discard$'

if command -v qemu-system-x86_64 >/dev/null; then
	qemu_status=0
	timeout 10s qemu-system-x86_64 -M q35 -m 1G \
		-bios "$temporary/build-grant/coreboot.rom" -display none \
		-serial stdio -monitor none -no-reboot > "$temporary/qemu.log" 2>&1 || \
		qemu_status=$?
	test "$qemu_status" -eq 0 || test "$qemu_status" -eq 124
	grep -qi 'coreboot' "$temporary/qemu.log"
fi

echo 'payload_mm_authvar_mor_grant tests: PASS'
