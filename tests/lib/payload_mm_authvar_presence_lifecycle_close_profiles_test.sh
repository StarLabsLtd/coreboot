#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "$root/../.lifecycle-close-profile.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
base=8506f9dadb075d79d2d2c30b5568e26d724b6087
baseline="$temporary/base"
common_git=$(realpath "$(git -C "$root" rev-parse --git-common-dir)")
intel_fsp=$(dirname "$(dirname "$common_git")")/intel_fsp
fsp_headers="$intel_fsp/arl/202507011953/Include/"
fsp_fd="$intel_fsp/arl/202507011953/Release/Fsp.fd"
test -d "$fsp_headers" && test -f "$fsp_fd"
current_fsp_headers=$(realpath --relative-to="$root" "$fsp_headers")
current_fsp_fd=$(realpath --relative-to="$root" "$fsp_fd")

test -f "$root/3rdparty/vboot/firmware/include/vb2_sha.h"
test -f "$root/3rdparty/stm/Readme.STMPE"
test -f "$root/3rdparty/mbedtls/library/asn1parse.c"

scratch_make()
(
	tree=$1
	shift
	vboot_source="$root/3rdparty/vboot"
	if [ "$tree" = "$baseline" ]; then
		vboot_source=3rdparty/vboot
	fi
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make BUILD_TIMELESS=1 KERNELVERSION=coreboot-lifecycle-close-test \
		VBOOT_SOURCE="$vboot_source" -C "$tree" "$@"
)

profile_kconfig="$temporary/Kconfig.lifecycle-close-profile"
head -n 3 "$root/src/Kconfig" > "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PREREQUISITES
	bool "test-only MTL lifecycle close prerequisites"
	default n
	select BOOTMEM_ALIGNED_RESERVATIONS
	select BOOTMEM_ALIGNED_RESERVATION_RECEIPT
	select ENABLE_EARLY_DMA_PROTECTION
	select PAYLOAD_MM_AUTHVAR_CONTRACT
	select PAYLOAD_MM_AUTHVAR_STORE_SCANNER
	select PAYLOAD_MM_AUTHVAR_FTW_DECODER
	select PAYLOAD_MM_AUTHVAR_STORE_SEMANTICS
	select PAYLOAD_MM_AUTHVAR_MEDIA_PORT
	select PAYLOAD_MM_AUTHVAR_WRITER
	select PAYLOAD_MM_AUTHVAR_EXECUTOR
	select PAYLOAD_MM_AUTHVAR_FORMAT_PARSER
	select PAYLOAD_MM_AUTHVAR_SIGNATURE_DB
	select PAYLOAD_MM_AUTHVAR_ROUTE
	select PAYLOAD_MM_AUTHVAR_AUTHORITY
	select PAYLOAD_MM_AUTHVAR_BUNDLE_PLAN
	select PAYLOAD_MM_AUTHVAR_CANDIDATE
	select PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT
	select PAYLOAD_MM_AUTHVAR_COORDINATOR
	select PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER
	select PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY
	select STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP
	select SMM_INVOCATION_EVIDENCE
	select SMM_INVOCATION_ENTRY_PLATFORM
	select SMM_INVOCATION_ENTRY
	select SMM_INVOCATION_INTEL_ADAPTER
	select SMM_INVOCATION_TOPOLOGY
	select SMM_INVOCATION_LOADER_INSTANCE
	select STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER
	select SMM_INVOCATION_LOADER_COMPOSITION
	select SMM_INVOCATION_RUNTIME_VIEW
	select SMM_INVOCATION_INTEL_ADAPTER_PROVIDER
	select SMM_APMC_COMPOSITION_ATTESTED
	select SMM_APMC_COMMAND_REGISTRY
	select PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION

config TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PROFILE
	bool "test-only MTL authenticated-variable lifecycle close"
	default n
	select TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PREREQUISITES
	select PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_OWNER
	select PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT_PROVIDER
	select PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT
	select PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT
	select SMM_APMC_ROUTE_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE

config SMM_MODULE_STACK_SIZE
	default 0x4000 if TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PREREQUISITES
EOF
tail -n +4 "$root/src/Kconfig" >> "$profile_kconfig"

lib_kconfig="$temporary/lib.Kconfig"
sed -e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PREREQUISITES\n\tdefault n/' \
	"$root/src/lib/Kconfig" > "$lib_kconfig"
sed -i "s|source \"src/lib/Kconfig\"|source \"$lib_kconfig\"|" \
	"$profile_kconfig"

build="$temporary/MTL_ON"
config="$build/full.config"
mkdir -p "$build"
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile_kconfig" \
	KBUILD_DEFCONFIG=configs/config.starlabs_starbook_mtl defconfig >/dev/null
"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN \
	-e TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PROFILE -d LTO \
	--set-str FSP_HEADER_PATH "$current_fsp_headers" \
	--set-str FSP_FD_PATH "$current_fsp_fd"
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile_kconfig" olddefconfig >/dev/null
grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_OWNER=y$' "$config"
grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT=y$' "$config"
grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT=y$' "$config"
grep -q '^CONFIG_SMM_APMC_ROUTE_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE=y$' "$config"
grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION=y$' "$config"
grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY=y$' "$config"
grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR=y$' "$config"
grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_CONTRACT=y$' "$config"
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile_kconfig" STACK_AUDIT_CFLAGS='-fstack-usage' -j4 \
	"$build/cbfs/fallback/ramstage.debug" "$build/smm/smm" >/dev/null

for stem in lifecycle_close pre_external_image_close payload_failure_close \
	warm_reset_close s3_resume_close closed_reproof; do
	object="$build/smm/lib/payload_mm_authvar_presence_${stem}.o"
	test -s "$object"
	file "$object" | grep -q 'ELF 32-bit'
	test "$(find "$build" -type f \
		-name "payload_mm_authvar_presence_${stem}.o" | wc -l)" -eq 1
	if nm -u "$object" | grep -Eq '__atomic|libatomic'; then
		echo "lifecycle-close owner gained runtime atomic dependency" >&2
		exit 1
	fi
	if find "$build/ramstage" -type f \
		-name "payload_mm_authvar_presence_${stem}.o" | grep -q .; then
		echo "lifecycle-close owner escaped SMM" >&2
		exit 1
	fi
done
for stem in lifecycle_close_backing lifecycle_close_publication lifecycle_close_provider \
	lifecycle_close_sender; do
	object="$build/ramstage/lib/payload_mm_authvar_presence_${stem}.o"
	test -s "$object"
	file "$object" | grep -q 'ELF 32-bit'
	test "$(find "$build" -type f \
		-name "payload_mm_authvar_presence_${stem}.o" | wc -l)" -eq 1
	if nm -u "$object" | grep -Eq '__atomic|libatomic'; then
		echo "lifecycle-close endpoint gained runtime atomic dependency" >&2
		exit 1
	fi
	if find "$build/smm" -type f \
		-name "payload_mm_authvar_presence_${stem}.o" | grep -q .; then
		echo "lifecycle-close endpoint escaped ramstage" >&2
		exit 1
	fi
done
for endpoint_object in \
	"$build/ramstage/lib/payload_mm_authvar_presence_lifecycle_close_endpoint.o" \
	"$build/smm/lib/payload_mm_authvar_presence_lifecycle_close_endpoint.o"; do
	test -s "$endpoint_object"
	file "$endpoint_object" | grep -q 'ELF 32-bit'
	if nm -u "$endpoint_object" | grep -Eq '__atomic|libatomic'; then
		echo 'lifecycle-close endpoint gained runtime atomic dependency' >&2
		exit 1
	fi
done
test "$(find "$build" -type f \
	-name 'payload_mm_authvar_presence_lifecycle_close_endpoint.o' | wc -l)" -eq 2
route_object="$build/smm/lib/payload_mm_authvar_presence_lifecycle_close_route.o"
test -s "$route_object"
file "$route_object" | grep -q 'ELF 32-bit'
test "$(find "$build" -type f \
	-name 'payload_mm_authvar_presence_lifecycle_close_route.o' | wc -l)" -eq 1
if nm -u "$route_object" | grep -Eq '__atomic|libatomic'; then
	echo 'lifecycle-close route gained runtime atomic dependency' >&2
	exit 1
fi
sender_object="$build/ramstage/lib/payload_mm_authvar_presence_lifecycle_close_sender.o"
test "$(nm -g "$sender_object" | awk \
	'$3 == "payload_mm_authvar_presence_lifecycle_close_send" { count++ } \
	 END { print count + 0 }')" -eq 1
if find "$build/ramstage" -type f \
	-name 'payload_mm_authvar_presence_lifecycle_close_route.o' | grep -q .; then
	echo 'lifecycle-close route escaped SMM' >&2
	exit 1
fi
stack_files=$(find "$build/smm/lib" -type f \
	-name '*payload_mm_authvar_presence_*close*.su' -o \
	-name '*payload_mm_authvar_presence_*reproof*.su')
test -n "$stack_files"
awk '$2 > 4096 { exit 1 }' $stack_files
if nm "$build/cbfs/fallback/ramstage.debug" | \
	grep -Eq 'payload_mm_authvar_presence_(pre_external_image_close|payload_failure_close|warm_reset_close|s3_resume_close|closed_reproof|lifecycle_close_source)'; then
	echo "SMM lifecycle-close owner linked into ramstage" >&2
	exit 1
fi
for symbol in payload_mm_authvar_presence_lifecycle_close_endpoint_validate \
	payload_mm_authvar_presence_lifecycle_close_endpoint_reserve \
	payload_mm_authvar_presence_lifecycle_close_backing_reserve \
	payload_mm_authvar_presence_lifecycle_close_ready_receipt_consume \
	payload_mm_authvar_presence_lifecycle_close_publication_commit \
	payload_mm_authvar_presence_lifecycle_close_provider_prepare \
	lb_add_payload_mm_authvar_presence_lifecycle_close_endpoint; do
	test "$(nm -g "$build/cbfs/fallback/ramstage.debug" | awk -v symbol="$symbol" \
		'$3 == symbol { count++ } END { print count + 0 }')" -eq 1
done

mkdir -p "$baseline"
git -C "$root" archive "$base" | tar -x -C "$baseline"
rmdir "$baseline/3rdparty/vboot" "$baseline/3rdparty/stm"
ln -s "$root/3rdparty/vboot" "$baseline/3rdparty/vboot"
ln -s "$root/3rdparty/stm" "$baseline/3rdparty/stm"
ln -s "$intel_fsp" "$temporary/intel_fsp"

build_natural()
(
	tree=$1
	name=$2
	profile=$3
	build="$temporary/$name"
	config="$build/full.config"
	mkdir -p "$build"
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
		KBUILD_DEFCONFIG="configs/config.$profile" defconfig >/dev/null
	"$tree/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN -d LTO \
		-d PAYLOAD_SEABIOS -e PAYLOAD_NONE
	if [ "$profile" = starlabs_starbook_mtl ]; then
		tree_fsp_headers=$(realpath --relative-to="$tree" "$fsp_headers")
		tree_fsp_fd=$(realpath --relative-to="$tree" "$fsp_fd")
		"$tree/util/scripts/config" --file "$config" \
			--set-str FSP_HEADER_PATH "$tree_fsp_headers" \
			--set-str FSP_FD_PATH "$tree_fsp_fd"
	fi
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
		olddefconfig >/dev/null
	if [ "$tree" = "$root" ]; then
		if grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_OWNER=y$' \
			"$config"; then
			echo "lifecycle-close owner unexpectedly default-on" >&2
			exit 1
		fi
		if grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT=y$' \
			"$config"; then
			echo "lifecycle-close endpoint unexpectedly default-on" >&2
			exit 1
		fi
		if grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT=y$' \
			"$config"; then
			echo "lifecycle-close transport unexpectedly default-on" >&2
			exit 1
		fi
		if grep -q '^CONFIG_SMM_APMC_ROUTE_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE=y$' \
			"$config"; then
			echo "lifecycle-close route unexpectedly default-on" >&2
			exit 1
		fi
	fi
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
		-j4 "$build/cbfs/fallback/ramstage.debug" "$build/smm/smm" >/dev/null
)

for profile in emulation_qemu_x86_q35_smm_tseg starlabs_lite_glk \
	starlabs_lite_adl starlabs_starbook_mtl; do
	name=$(printf '%s' "$profile" | tr / _)
	build_natural "$root" "current-$name" "$profile"
	build_natural "$baseline" "base-$name" "$profile"
	objcopy -O binary "$temporary/current-$name/cbfs/fallback/ramstage.debug" \
		"$temporary/current-$name.ramstage"
	objcopy -O binary "$temporary/base-$name/cbfs/fallback/ramstage.debug" \
		"$temporary/base-$name.ramstage"
	cmp "$temporary/current-$name.ramstage" "$temporary/base-$name.ramstage"
	cmp "$temporary/current-$name/smm/smm" "$temporary/base-$name/smm/smm"
done

printf '%s\n' 'Payload-MM authenticated-variable lifecycle close profiles: PASS'
