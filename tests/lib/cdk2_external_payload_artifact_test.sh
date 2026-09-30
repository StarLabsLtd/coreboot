#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$#" -ne 4 ]; then
	echo "usage: $0 coreboot-rom coreboot-build-directory coreboot-config cbfstool" >&2
	exit 2
fi

rom=$1
build=$2
coreboot_config=$3
cbfstool=$4
payload=$build/cdk2/native/cdk2-coreboot-image.elf
cdk2_config=$build/cdk2/.config
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

test -f "$rom"
test -f "$payload"
test -f "$cdk2_config"
test -f "$coreboot_config"
grep -qx 'CONFIG_PAYLOAD_CDK2=y' "$coreboot_config"
grep -qx 'CONFIG_CDK2_STRICT_DIRECT_RUNTIME=y' "$cdk2_config"
if grep -Eq '^CONFIG_(SMMSTORE|DRIVERS_EFI_VARIABLE_STORE)=y$' \
	"$coreboot_config"; then
	echo 'production CDK2 artifact gate forbids the legacy variable-store route' >&2
	exit 1
fi

"$cbfstool" "$rom" print > "$temporary/cbfs.txt"
grep -Eq '^fallback/payload[[:space:]].*simple elf' "$temporary/cbfs.txt"
grep -Eq '^cdk2/coreboot-config[[:space:]].*raw' "$temporary/cbfs.txt"
grep -Eq '^cdk2/config[[:space:]].*raw' "$temporary/cbfs.txt"

"$cbfstool" "$rom" extract -n cdk2/coreboot-config \
	-f "$temporary/coreboot.config" >/dev/null
"$cbfstool" "$rom" extract -n cdk2/config \
	-f "$temporary/cdk2.config" >/dev/null
cmp "$coreboot_config" "$temporary/coreboot.config"
cmp "$cdk2_config" "$temporary/cdk2.config"

"$build/cdk2/native/cdk2-elfcheck" --entry cdk2_coreboot_entry32 \
	--require-direct-images "$payload"
if readelf -S "$payload" | grep -Eiq '(^|[[:space:]])(fv|ffs)($|[[:space:]])'; then
	echo 'CDK2 payload unexpectedly contains an FV or FFS section' >&2
	exit 1
fi

printf '%s\n' 'CDK2 external payload artifact: PASS'
