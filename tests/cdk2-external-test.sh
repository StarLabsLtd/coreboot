#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
make_command=${MAKE:-make}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
source_dir="$tmp/source"
mkdir -p "$source_dir"
git -C "$source_dir" init -q
git -C "$source_dir" config user.email test@example.com
git -C "$source_dir" config user.name Test
cat > "$source_dir/Makefile" <<'EOF'
coreboot-stage:
	@test -n "$(COREBOOT_CONFIG)"
	@test -n "$(COREBOOT_CDK2_PROFILE)"
	@test -n "$(COREBOOT_OUTPUT_DIR)"
	@test -z "$(KCONFIG_CONFIG)"
	@test -z "$(obj)"
	@test -z "$(top)"
	@test -z "$(src)"
	@test "$(origin AR)$(origin AS)$(origin LD)" = defaultdefaultdefault
	@test "$(origin NM)$(origin OBJCOPY)$(origin OBJDUMP)" = undefinedundefinedundefined
	@test "$(origin RANLIB)$(origin STRIP)" = undefinedundefined
	@mkdir -p "$(COREBOOT_OUTPUT_DIR)/native"
	@cp "$(COREBOOT_CDK2_PROFILE)" "$(COREBOOT_OUTPUT_DIR)/received.config"
	@touch "$(COREBOOT_OUTPUT_DIR)/native/cdk2-coreboot-stage.elf"
EOF
git -C "$source_dir" add Makefile
git -C "$source_dir" commit -q -m fixture
revision=$(git -C "$source_dir" rev-parse HEAD)
cat > "$tmp/gmake" <<EOF
#!/bin/sh
echo invoked > "$tmp/make-invoked"
exec make "\$@"
EOF
chmod +x "$tmp/gmake"
echo 'CONFIG_PAYLOAD_CDK2=y' > "$tmp/coreboot.config"
"$root/util/cdk2-config" "$tmp/profile"
COREBOOT_EXPORTS='COREBOOT_EXPORTS KCONFIG_CONFIG obj top src' \
	KCONFIG_CONFIG=wrong obj=wrong top=wrong src=wrong \
	AR=wrong AS=wrong LD=wrong NM=wrong OBJCOPY=wrong OBJDUMP=wrong \
	RANLIB=wrong STRIP=wrong \
	"$root/util/cdk2-build" "$source_dir" "$revision" \
	"$tmp/coreboot.config" "$tmp/profile" "$tmp/output" \
	"$tmp/gmake"
test -f "$tmp/make-invoked"
cmp "$tmp/profile" "$tmp/output/received.config"
test -f "$tmp/output/native/cdk2-coreboot-stage.elf"

if "$root/util/cdk2-build" "$source_dir" \
	0000000000000000000000000000000000000000 "$tmp/coreboot.config" \
	"$tmp/profile" "$tmp/bad-revision" 2> "$tmp/error"; then
	echo 'mismatched CDK2 revision unexpectedly accepted' >&2
	exit 1
fi
grep -q 'revision mismatch' "$tmp/error"

echo changed >> "$source_dir/Makefile"
if "$root/util/cdk2-build" "$source_dir" "$revision" \
	"$tmp/coreboot.config" "$tmp/profile" "$tmp/dirty" 2> "$tmp/error"; then
	echo 'dirty CDK2 checkout unexpectedly accepted' >&2
	exit 1
fi
grep -q 'tracked modifications' "$tmp/error"

for board in config.emulation_qemu_x86_q35_smm_tseg config.starlabs_starbook_mtl; do
	cp "$root/configs/$board" "$tmp/$board"
	echo 'CONFIG_PAYLOAD_CDK2=y' >> "$tmp/$board"
	"$make_command" -s -C "$root" DOTCONFIG="$tmp/$board" \
		obj="$tmp/build-$board" olddefconfig
	for symbol in PAYLOAD_CDK2 HANDOFF_COREBOOT_TABLES \
		PAYLOAD_OWNS_PCI_DEVICES WANT_LINEAR_FRAMEBUFFER SMMSTORE; do
		grep -qx "CONFIG_${symbol}=y" "$tmp/$board"
	done
	grep -qx 'CONFIG_CDK2_SOURCE_REVISION="b6c70863f4d0c8b893d561397fab8c4abb61d382"' \
		"$tmp/$board"
done

# Merely adding CDK2 must not perturb the existing EDK2 selection.
cp "$root/configs/config.starlabs_starbook_mtl" "$tmp/edk2"
"$make_command" -s -C "$root" DOTCONFIG="$tmp/edk2" \
	obj="$tmp/build-edk2" olddefconfig
grep -qx 'CONFIG_PAYLOAD_EDK2=y' "$tmp/edk2"
grep -qx '# CONFIG_PAYLOAD_CDK2 is not set' "$tmp/edk2"
