#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
make_command=${MAKE:-make}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
source_dir="$tmp/source"
mkdir -p "$source_dir"
git -C "$source_dir" init -q --object-format=sha1
git -C "$source_dir" config user.email test@example.com
git -C "$source_dir" config user.name Test
cat > "$source_dir/Makefile" <<'EOF'
native-coreboot-image:
	@test -n "$(COREBOOT_CONFIG)"
	@test -n "$(COREBOOT_CDK2_PROFILE)"
	@test -n "$(CDK2_BUILD_DIR)"
	@test -s "$(CDK2_PAYLOAD_FV)"
	@test -z "$(KCONFIG_CONFIG)"
	@test -z "$(obj)"
	@test -z "$(top)"
	@test -z "$(src)"
	@test "$(origin AR)$(origin AS)$(origin LD)" = defaultdefaultdefault
	@test "$(origin NM)$(origin OBJCOPY)$(origin OBJDUMP)" = undefinedundefinedundefined
	@test "$(origin RANLIB)$(origin STRIP)" = undefinedundefined
	@mkdir -p "$(CDK2_BUILD_DIR)/native"
	@cp "$(COREBOOT_CDK2_PROFILE)" "$(CDK2_BUILD_DIR)/received.config"
	@printf 'complete-image-with-fv\n' > "$(CDK2_BUILD_DIR)/native/cdk2-coreboot-image.elf"
EOF
git -C "$source_dir" add Makefile
git -C "$source_dir" -c commit.gpgSign=false commit -q -m fixture
revision=$(git -C "$source_dir" rev-parse HEAD)
echo 'CONFIG_PAYLOAD_CDK2=y' > "$tmp/coreboot.config"
printf 'admitted retained FV fixture\n' > "$tmp/retained.fv"
if command -v sha256sum >/dev/null 2>&1; then
	retained_hash=$(sha256sum "$tmp/retained.fv" | awk '{print $1}')
elif command -v sha256 >/dev/null 2>&1; then
	retained_hash=$(sha256 -q "$tmp/retained.fv")
else
	retained_hash=$(shasum -a 256 "$tmp/retained.fv" | awk '{print $1}')
fi
cat > "$tmp/gmake" <<EOF
#!/bin/sh
echo invoked > "$tmp/make-invoked"
exec "$make_command" "\$@"
EOF
chmod +x "$tmp/gmake"
"$root/util/cdk2-config" "$tmp/coreboot.config" "$tmp/profile"
COREBOOT_EXPORTS='COREBOOT_EXPORTS KCONFIG_CONFIG obj top src' \
	KCONFIG_CONFIG=wrong obj=wrong top=wrong src=wrong \
	AR=wrong AS=wrong LD=wrong NM=wrong OBJCOPY=wrong OBJDUMP=wrong \
	RANLIB=wrong STRIP=wrong \
	"$root/util/cdk2-build" "$source_dir" "$revision" \
	"$tmp/coreboot.config" "$tmp/profile" "$tmp/output" \
	"$tmp/retained.fv" "$retained_hash" "$tmp/gmake"
test -f "$tmp/make-invoked"
cmp "$tmp/profile" "$tmp/output/received.config"
grep -qx 'complete-image-with-fv' "$tmp/output/native/cdk2-coreboot-image.elf"

if "$root/util/cdk2-build" "$source_dir" \
	0000000000000000000000000000000000000000 "$tmp/coreboot.config" \
	"$tmp/profile" "$tmp/bad-revision" "$tmp/retained.fv" \
	"$retained_hash" 2> "$tmp/error"; then
	echo 'mismatched CDK2 revision unexpectedly accepted' >&2
	exit 1
fi
grep -q 'revision mismatch' "$tmp/error"

echo changed >> "$source_dir/Makefile"
if "$root/util/cdk2-build" "$source_dir" "$revision" \
	"$tmp/coreboot.config" "$tmp/profile" "$tmp/dirty" \
	"$tmp/retained.fv" "$retained_hash" 2> "$tmp/error"; then
	echo 'dirty CDK2 checkout unexpectedly accepted' >&2
	exit 1
fi
grep -q 'tracked modifications' "$tmp/error"
git -C "$source_dir" checkout -q -- Makefile

if "$root/util/cdk2-build" "$source_dir" "$revision" \
	"$tmp/coreboot.config" "$tmp/profile" "$tmp/bad-fv" \
	"$tmp/retained.fv" \
	0000000000000000000000000000000000000000000000000000000000000000 \
	2> "$tmp/error"; then
	echo 'mismatched retained FV digest unexpectedly accepted' >&2
	exit 1
fi
grep -q 'retained FV digest mismatch' "$tmp/error"

for board in config.emulation_qemu_x86_q35_smm_tseg config.starlabs_starbook_mtl; do
	cp "$root/configs/$board" "$tmp/$board"
	echo 'CONFIG_PAYLOAD_CDK2=y' >> "$tmp/$board"
	echo 'CONFIG_CDK2_RETAINED_FV_PATH="/firmware/retained.fv"' >> "$tmp/$board"
	"$make_command" -s -C "$root" DOTCONFIG="$tmp/$board" \
		obj="$tmp/build-$board" olddefconfig
	for symbol in PAYLOAD_CDK2 HANDOFF_COREBOOT_TABLES \
		PAYLOAD_OWNS_PCI_DEVICES WANT_LINEAR_FRAMEBUFFER \
		GENERIC_LINEAR_FRAMEBUFFER SMMSTORE; do
		grep -qx "CONFIG_${symbol}=y" "$tmp/$board"
	done
	grep -qx 'CONFIG_CDK2_SOURCE_REVISION="b6d2059ccce7d9e3df593a8f1d8906fb00b63f1c"' \
		"$tmp/$board"
	grep -qx 'CONFIG_CDK2_RETAINED_FV_SHA256="ca1ebfd0ff6c7c82935a4302c1ddc4cc418ed177756c678260dfb09527e1f50e"' \
		"$tmp/$board"
done

# CDK2 requires a framebuffer HOB, so boards without linear-framebuffer
# capability must not expose it as a selectable payload.
cp "$root/configs/builder/config.intel.crb.ac" "$tmp/no-linear"
echo 'CONFIG_PAYLOAD_CDK2=y' >> "$tmp/no-linear"
"$make_command" -s -C "$root" DOTCONFIG="$tmp/no-linear" \
	obj="$tmp/build-no-linear" olddefconfig
! grep -qx 'CONFIG_PAYLOAD_CDK2=y' "$tmp/no-linear"

# Merely adding CDK2 must not perturb the existing EDK2 selection.
cp "$root/configs/config.starlabs_starbook_mtl" "$tmp/edk2"
"$make_command" -s -C "$root" DOTCONFIG="$tmp/edk2" \
	obj="$tmp/build-edk2" olddefconfig
grep -qx 'CONFIG_PAYLOAD_EDK2=y' "$tmp/edk2"
grep -qx '# CONFIG_PAYLOAD_CDK2 is not set' "$tmp/edk2"
