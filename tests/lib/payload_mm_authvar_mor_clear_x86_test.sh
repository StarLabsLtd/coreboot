#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0\n' > "$temporary/include/config.h"

cases='prepare execute-high physical-zero high-map init-failure cross-boundary
unmap-mismatch paging-bits prepare-arch-mutation prepare-residual-pg
prepare-residual-pae prepare-no-clflush
prepare-misaligned prepare-not-excluded prepare-partial prepare-wrong-reason
prepare-null-plan prepare-backing-alias prepare-state-alias map-output-alias
cache-mutation'

for flags in '-O0' '-O2' '-O1 -fsanitize=address' \
	'-O1 -fsanitize=undefined'; do
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin $flags \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -D__ARCH_x86_32__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$temporary/include" \
		"$root/tests/lib/payload_mm_authvar_mor_clear_x86_test.c" \
		"$root/src/lib/payload_mm_authvar_mor_clear_x86.c" \
		"$root/src/lib/payload_mm_authvar_mor_clear_executor.c" \
		"$root/src/lib/payload_mm_authvar_mor_clear_plan.c" \
		"$root/src/lib/payload_mm_authvar_mor_clear.c" \
		"$root/src/lib/payload_mm_authvar_mor_grant.c" -o "$temporary/test"
	for case_name in $cases; do
		ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/test" "$case_name"
	done
done

mutant()
{
	name=$1
	expression=$2
	mutant="$temporary/$name.c"
	sed "$expression" "$root/src/lib/payload_mm_authvar_mor_clear_x86.c" > "$mutant"
	! cmp -s "$root/src/lib/payload_mm_authvar_mor_clear_x86.c" "$mutant"
	"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -D__ARCH_x86_32__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$temporary/include" \
		"$root/tests/lib/payload_mm_authvar_mor_clear_x86_test.c" "$mutant" \
		"$root/src/lib/payload_mm_authvar_mor_clear_executor.c" \
		"$root/src/lib/payload_mm_authvar_mor_clear_plan.c" \
		"$root/src/lib/payload_mm_authvar_mor_clear.c" \
		"$root/src/lib/payload_mm_authvar_mor_grant.c" -o "$temporary/$name"
	survived=true
	for case_name in $cases; do
		if ! "$temporary/$name" "$case_name" 2>/dev/null; then
			survived=false
			break
		fi
	done
	if "$survived"; then
		echo "ERROR: $name mutation survived" >&2
		exit 1
	fi
}

mutant excluded-backing \
	'/!excluded_range(&plan_copy, (uintptr_t)page_tables,/{N;s/!excluded_range(&plan_copy, (uintptr_t)page_tables,\n\t\tPAE_PGTL_SIZE)/false/;}'
mutant physical-offset \
	's/backend->mapping = backend->aperture + offset;/backend->mapping = offset;/'
mutant high-physical \
	's/const uint64_t page_base = ALIGN_DOWN(physical, PAE_VMEM_SIZE);/const uint64_t page_base = (uint32_t)ALIGN_DOWN(physical, PAE_VMEM_SIZE);/'
mutant boundary-check \
	's/size > PAE_VMEM_SIZE - offset/false/'
mutant disable-verification \
	's/if (active || changed)/if ((void)active, changed)/'
mutant residual-paging-bit \
	's/return (cr0 \& X86_CR0_PG_BIT) || (cr4 \& X86_CR4_PAE_BIT);/return (cr0 \& X86_CR0_PG_BIT) \&\& (cr4 \& X86_CR4_PAE_BIT);/'
mutant unmap-disable \
	's/const enum cb_err disabled = disable_and_verify(backend);/const enum cb_err disabled = CB_SUCCESS;/'
mutant callback-mutation \
	'/arch.clflush_range/,/return backend_fail/ s/!configuration_matches(backend, page_tables, aperture, &arch)/false/'

"${CC:-cc}" -std=gnu11 -Os -m32 -Wall -Wextra -Werror -fno-builtin \
	-D__COREBOOT__ -D__RAMSTAGE__ -D__ARCH_x86_32__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" -c \
	"$root/src/lib/payload_mm_authvar_mor_clear_x86.c" -o "$temporary/production.o"
nm -g --defined-only "$temporary/production.o" | \
	grep -q 'payload_mm_authvar_mor_clear_x86_prepare$'
! nm -g --defined-only "$temporary/production.o" | grep -q 'prepare_with_ops'

mkdir -p "$temporary/config" "$temporary/build"
printf '%s\n' \
	'CONFIG_VENDOR_EMULATION=y' \
	'CONFIG_BOARD_EMULATION_QEMU_X86_Q35=y' \
	'CONFIG_ANY_TOOLCHAIN=y' > "$temporary/config/.config"
make -C "$root" obj="$temporary/build" DOTCONFIG="$temporary/config/.config" \
	olddefconfig >/dev/null
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_BACKEND=y$' \
	"$temporary/config/.config"

cp "$root/src/Kconfig" "$temporary/Kconfig"
cat >> "$temporary/Kconfig" <<'EOF'

config TEST_MOR_CLEAR_X86_SELECTOR
	bool
	default y
	select HAVE_SMI_HANDLER
	select PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT
	select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_PLAN
	select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RECEIPT
	select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR
	select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_BACKEND
EOF
cp "$temporary/config/.config" "$temporary/config-selected"
make -C "$root" obj="$temporary/build-selected" \
	KBUILD_KCONFIG="$temporary/Kconfig" DOTCONFIG="$temporary/config-selected" \
	olddefconfig >/dev/null
grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_BACKEND=y' \
	"$temporary/config-selected"
make -C "$root" obj="$temporary/build-selected" \
	KBUILD_KCONFIG="$temporary/Kconfig" DOTCONFIG="$temporary/config-selected" \
	-j"$(getconf _NPROCESSORS_ONLN)" >/dev/null
test -f "$temporary/build-selected/ramstage/lib/payload_mm_authvar_mor_clear_x86.o"

if command -v qemu-system-x86_64 >/dev/null; then
	qemu_status=0
	timeout 10s qemu-system-x86_64 -M q35 -m 5G \
		-bios "$temporary/build-selected/coreboot.rom" -display none \
		-serial stdio -monitor none -no-reboot > "$temporary/qemu.log" 2>&1 || \
		qemu_status=$?
	test "$qemu_status" -eq 0 || test "$qemu_status" -eq 124
	grep -qi 'coreboot' "$temporary/qemu.log"
fi

printf '%s\n' 'Payload-MM MOR clear x86 backend tests: PASS'
