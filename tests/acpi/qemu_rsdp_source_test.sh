#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

check_source()
{
	source_file=$1
	test "$(grep -c 'if (acpi_qemu_rsdp_needs_xsdt(rsdp))' "$source_file")" -eq 1 ||
		return 1
	test "$(grep -c 'acpi_qemu_clear_xsdt(xsdt);' "$source_file")" -eq 1 ||
		return 1
	if grep -q 'if (rsdp->xsdt_address' "$source_file"; then
		return 1
	fi
	return 0
}

check_source "$root/src/acpi/acpi.c"
sed 's/acpi_qemu_rsdp_needs_xsdt(rsdp)/rsdp->xsdt_address == 0/' \
	"$root/src/acpi/acpi.c" >"$temporary/direct-field.c"
if check_source "$temporary/direct-field.c"; then
	exit 1
fi
sed '/if (acpi_qemu_rsdp_needs_xsdt(rsdp))/d' "$root/src/acpi/acpi.c" \
	>"$temporary/missing-helper.c"
if check_source "$temporary/missing-helper.c"; then
	exit 1
fi
sed '/acpi_qemu_clear_xsdt(xsdt);/d' "$root/src/acpi/acpi.c" \
	>"$temporary/missing-clear.c"
if check_source "$temporary/missing-clear.c"; then
	exit 1
fi
printf '%s\n' 'qemu RSDP source contract: PASS'
