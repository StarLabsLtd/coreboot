#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

set -eo pipefail

export WORKSPACE=$1
output=$2
repository=$3
revision=$4
capsules=$5
trust=$6
source_dir=$WORKSPACE/source
set --

if [[ ! $revision =~ ^[[:xdigit:]]{40}$ ]]; then
	echo "ASS: Fixed MM requires an immutable source revision" >&2
	exit 1
fi
if [[ $capsules == y && $trust != /* ]]; then
	echo "ASS: capsule verification requires an absolute public trust certificate path" >&2
	exit 1
fi

mkdir -p "$WORKSPACE"
sh util/efi_capsule/check-pinned-source.sh "$source_dir" "$repository" "$revision" "" fixed-mm

platform=UefiPayloadPkg/CorebootFixedMmService
if [[ ! -f $source_dir/$platform.dsc || ! -f $source_dir/$platform.fdf ]]; then
	echo "ASS: Fixed MM source is missing its DSC or FDF" >&2
	exit 1
fi

export PACKAGES_PATH=$source_dir
cd "$WORKSPACE"
source "$source_dir/edksetup.sh" --reconfig
unset CC
make -C "$source_dir/BaseTools"

args=()
if [[ $capsules == y ]]; then
	mkdir -p Conf
	der=$(mktemp "$WORKSPACE/capsule-cert.XXXXXXXX")
	trap 'rm -f -- "$der"' EXIT
	openssl x509 -in "$trust" -outform der -out "$der"
	python3 "$source_dir/BaseTools/Scripts/BinToPcd.py" -q -x -i "$der" \
		-o Conf/CapsuleFmpPkcs7Pcd.inc \
		-p gFmpDevicePkgTokenSpaceGuid.PcdFmpDevicePkcs7CertBufferXdr
	args=(-D CAPSULE_SUPPORT=TRUE -D FMP_DEVICE_PKCS7_PCD_INC=Conf/CapsuleFmpPkcs7Pcd.inc)
fi

build -a IA32 -b RELEASE -t GCC -p "$platform.dsc" "${args[@]}"
fv=Build/CorebootFixedMmService/RELEASE_GCC/FV/FIXEDMMFV.Fv
test -s "$fv"
cp "$fv" "$output.tmp"
mv -f "$output.tmp" "$output"
