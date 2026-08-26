#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_base=${TMPDIR:-/tmp}
tmp=$(mktemp -d "$tmp_base/cb-busmaster-audit.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

awk '!/^CONFIG_ANY_TOOLCHAIN=/' "$root/configs/config.starlabs_starbook_mtl" \
	> "$tmp/defconfig"
printf 'CONFIG_ANY_TOOLCHAIN=y\n' >> "$tmp/defconfig"

mkdir -p "$tmp/build"
if [ -f "$root/build/xcompile" ]; then
	cp "$root/build/xcompile" "$tmp/build/xcompile"
fi

make -s -C "$root" TMPDIR="$tmp" DOTCONFIG="$tmp/config" obj="$tmp/build" \
	KBUILD_DEFCONFIG="$tmp/defconfig" defconfig 2> "$tmp/defconfig.err"
make -s -C "$root" TMPDIR="$tmp" DOTCONFIG="$tmp/config" obj="$tmp/build" \
	CC_x86_32="${CC:-gcc}" IASL="${IASL:-iasl}" printall \
	> "$tmp/printall" 2> "$tmp/printall.err"
make -s -C "$root" TMPDIR="$tmp" DOTCONFIG="$tmp/config" obj="$tmp/build" \
	CC_x86_32="${CC:-gcc}" IASL="${IASL:-iasl}" \
	print-pci-busmaster-build-graph > "$tmp/build-graph" 2>> "$tmp/printall.err"

cat "$tmp/defconfig.err" "$tmp/printall.err" >&2
if grep -Eq 'No space left|write error' "$tmp/defconfig.err" "$tmp/printall.err"; then
	echo "failed to resolve the enabled-source graph" >&2
	exit 1
fi

awk '/^allsrcs:/{enabled=1; next} enabled && NF == 0 {exit} \
	enabled && /^(src|payloads)\// {print}' \
	"$tmp/printall" | sort -u > "$tmp/enabled-sources"

cat > "$tmp/cc-audit" <<EOF
#!/bin/sh
exec ${CC:-gcc} -save-temps=obj "\$@"
EOF
chmod +x "$tmp/cc-audit"
has_verstage=$(grep -c '^CONFIG_VBOOT_SEPARATE_VERSTAGE=y$' "$tmp/config" || true)
awk -F '\t' -v has_verstage="$has_verstage" \
	'$1 ~ /^(bootblock|romstage|postcar|ramstage|smm|verstage)$/ &&
	($1 != "verstage" || has_verstage) &&
	$2 ~ /^(src|3rdparty|payloads)\// && $2 ~ /\.c$/ && $3 ~ /\.o$/ { print $3 }' \
	"$tmp/build-graph" | sort -u > "$tmp/audit-objects"
# Compile translation units without linking the firmware or building its
# payload. Besides proving membership, -save-temps=obj captures the exact
# preprocessor expansion and the compiler-generated dependency closure.
if ! xargs make -s -C "$root" -j4 TMPDIR="$tmp" DOTCONFIG="$tmp/config" \
	obj="$tmp/build" CC_x86_32="$tmp/cc-audit" IASL="${IASL:-iasl}" \
	< "$tmp/audit-objects" > "$tmp/compile.out" 2> "$tmp/compile.err"; then
	cat "$tmp/compile.out" "$tmp/compile.err" >&2
	exit 1
fi
awk -F '\t' -v has_verstage="$has_verstage" \
	'$1 ~ /^(bootblock|romstage|postcar|ramstage|smm|verstage)$/ &&
	($1 != "verstage" || has_verstage) &&
	$2 ~ /^(src|3rdparty|payloads)\// && $2 ~ /\.c$/ && $3 ~ /\.o$/ {
		unit = $3; sub(/\.o$/, ".i", unit); print $1 "\t" unit
	}' \
	"$tmp/build-graph" | sort -u > "$tmp/preprocessed"
[ -s "$tmp/preprocessed" ] || {
	echo "no preprocessed translation units were generated" >&2
	exit 1
}
devicetree="$tmp/build/mainboard/starlabs/starbook/static.c"
[ -f "$devicetree" ] || {
	echo "generated devicetree is missing" >&2
	exit 1
}

"$root/util/check-pci-busmaster-ownership.sh" \
	"$root/configs/policies/starlabs_starbook_mtl_busmaster_ownership.tsv" \
	"$root" "$tmp/config" "$tmp/enabled-sources" "$tmp/preprocessed" \
	"$devicetree"
