#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/build"

python3 - "$temporary/build/coreboot.rom" <<'PY'
import pathlib
import struct
import sys

image = bytearray(512)
struct.pack_into("<8sBBQI32sH", image, 0, b"__FMAP__", 1, 1, 0,
                 len(image), b"recipe-test", 2)
struct.pack_into("<II32sH", image, 56, 0, 140, b"FMAP", 0)
struct.pack_into("<II32sH", image, 98, 0, len(image), b"COREBOOT", 0)
pathlib.Path(sys.argv[1]).write_bytes(image)
PY
cp "$temporary/build/coreboot.rom" "$temporary/original.rom"

printf 'MZfmp-driver' > "$temporary/FmpDxe.efi"
python3 - "$temporary/DXEFV.Fv" <<'PY'
import pathlib
import struct
import sys
import uuid

image_guid = uuid.UUID("975cd0e6-c540-4e2b-906c-72c0d0d1e40d")
fv_size = 0x1000
header_size = 72
fv = bytearray(b"\xff" * fv_size)
fv[:16] = b"\0" * 16
fv[16:32] = uuid.UUID("8c8ce578-8a3d-4f1c-9935-896185c32dd3").bytes_le
struct.pack_into("<Q4sIHHHBB", fv, 32, fv_size, b"_FVH", 0x800,
                 header_size, 0, 0, 0, 2)
struct.pack_into("<II", fv, 56, 1, fv_size)
struct.pack_into("<II", fv, 64, 0, 0)
file_size = 32
header = image_guid.bytes_le + b"\0\0" + bytes([0x07, 0])
header += file_size.to_bytes(3, "little") + b"\x07"
fv[header_size:header_size + 24] = header
fv[header_size + 24:header_size + file_size] = b"\xa5" * 8
pathlib.Path(sys.argv[1]).write_bytes(fv)
PY

openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
	-subj /CN=coreboot-capsule-recipe-test/ \
	-keyout "$temporary/key.pem" -out "$temporary/cert.pem" \
	>/dev/null 2>&1
cat "$temporary/key.pem" "$temporary/cert.pem" > "$temporary/signer.pem"
: > "$temporary/config"
{
	printf 'top := %s\n' "$root"
	printf '%s\n' 'strip_quotes = $(subst ",,$(1))'
	printf '%s\n' 'finalised_rom::' 'capsule::' 'files_added:'
	printf 'include %s/util/efi_capsule/Makefile.mk\n' "$root"
} > "$temporary/Makefile"

build_capsule()
{
	make -s -B -C "$root" -f "$temporary/Makefile" \
		obj="$temporary/build" DOTCONFIG="$temporary/config" \
		CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y \
		CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=0x001a0009 \
		CONFIG_DRIVERS_EFI_MAIN_FW_LSV=0x001a0001 \
		CONFIG_DRIVERS_EFI_MAIN_FW_GUID='"975cd0e6-c540-4e2b-906c-72c0d0d1e40d"' \
		CONFIG_DRIVERS_EFI_CAPSULE_SIGNER_PRIVATE_CERT="\"$temporary/signer.pem\"" \
		CONFIG_DRIVERS_EFI_CAPSULE_OTHER_PUBLIC_CERT='""' \
		CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT="\"$temporary/cert.pem\"" \
		CONFIG_DRIVERS_EFI_CAPSULE_INITIATE_RESET=n \
		CONFIG_DRIVERS_EFI_CAPSULE_EMBED_FMP_DXE=n \
		CONFIG_LOCALVERSION='"26.09"' "$@" \
		"$temporary/build/coreboot.cap"
}

build_capsule CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT=n \
	CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT=y \
	CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=0 CONFIG_LOCALVERSION='"v26.09-rc1"'
PYTHONPATH="$root" python3 - "$temporary/build/coreboot.rom" \
	"$temporary/build/coreboot.cap" <<'PY'
import pathlib
import sys

from util.efi_capsule.validate_capsule import parse_capsule_details

rom = pathlib.Path(sys.argv[1]).read_bytes()
details = parse_capsule_details(pathlib.Path(sys.argv[2]).read_bytes())
assert details["image"] == rom
assert not details["image"].endswith(b"RMAP\1\0\1\0")
assert details["fw_version"] == 0x001a0009
PY

rm "$temporary/build/coreboot.cap"
build_capsule CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT=y \
	CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT=n \
	CONFIG_DRIVERS_EFI_CAPSULE_REGIONS='"COREBOOT"' \
	CONFIG_EDK2_REPOSITORY='"https://github.com/example/custom-edk2.git"'
PYTHONPATH="$root" python3 - "$temporary/build/coreboot.rom" \
	"$temporary/build/coreboot.cap" <<'PY'
import pathlib
import sys

from util.efi_capsule.validate_capsule import parse_capsule_details, parse_rmap

rom = pathlib.Path(sys.argv[1]).read_bytes()
details = parse_capsule_details(pathlib.Path(sys.argv[2]).read_bytes())
regions, image = parse_rmap(details["image"])
assert regions == ["COREBOOT"]
assert image == rom
PY

rm "$temporary/build/coreboot.cap"
build_capsule CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT=y \
	CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT=n \
	CONFIG_DRIVERS_EFI_CAPSULE_REGIONS='"COREBOOT"' \
	CONFIG_DRIVERS_EFI_CAPSULE_EMBED_FMP_DXE=y \
	CAPSULE_LEGACY_FMP_DXE="$temporary/FmpDxe.efi" \
	CAPSULE_LEGACY_DXE_FV="$temporary/DXEFV.Fv" \
	CONFIG_EDK2_REPOSITORY='"https://github.com/example/custom-edk2.git"'
PYTHONPATH="$root" python3 - "$temporary/build/coreboot.cap" <<'PY'
import pathlib
import sys

from util.efi_capsule.validate_capsule import parse_capsule_details

details = parse_capsule_details(pathlib.Path(sys.argv[1]).read_bytes())
assert details["embedded_count"] == 1
PY

cp "$temporary/original.rom" "$temporary/build/coreboot.rom"
python3 - "$temporary/build/coreboot.rom" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
image = bytearray(path.read_bytes())
image[10] = 1
path.write_bytes(image)
PY
printf '%s' preserved > "$temporary/build/coreboot.cap"
if build_capsule CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT=n \
	CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT=y; then
	printf '%s\n' 'ERROR: corrupt typed FMAP survived' >&2
	exit 1
fi
test "$(cat "$temporary/build/coreboot.cap")" = preserved

python3 - "$temporary/build/coreboot.rom" <<'PY'
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
single = bytearray(512)
struct.pack_into("<8sBBQI32sH", single, 0, b"__FMAP__", 1, 1, 0,
                 1024, b"recipe-test", 2)
struct.pack_into("<II32sH", single, 56, 0, 140, b"FMAP", 0)
struct.pack_into("<II32sH", single, 98, 0, 1024, b"COREBOOT", 0)
image = single + bytearray(single)
struct.pack_into("<II", image, 512 + 56, 512, 140)
path.write_bytes(image)
PY
if build_capsule CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT=n \
	CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT=y; then
	printf '%s\n' 'ERROR: duplicate typed FMAP survived' >&2
	exit 1
fi
test "$(cat "$temporary/build/coreboot.cap")" = preserved

cp "$temporary/original.rom" "$temporary/build/coreboot.rom"

printf '%s' preserved > "$temporary/build/coreboot.cap"
if build_capsule CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT=y \
	CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT=n \
	CONFIG_DRIVERS_EFI_CAPSULE_REGIONS='""' \
	CONFIG_EDK2_REPOSITORY='"https://github.com/example/custom-edk2.git"'; then
	printf '%s\n' 'ERROR: empty legacy region list survived' >&2
	exit 1
fi
test "$(cat "$temporary/build/coreboot.cap")" = preserved

if build_capsule CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT=y \
	CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT=n \
	CONFIG_DRIVERS_EFI_CAPSULE_REGIONS='"COREBOOT"' \
	CONFIG_DRIVERS_EFI_CAPSULE_SIGNER_PRIVATE_CERT='"relative-signer.pem"' \
	CONFIG_EDK2_REPOSITORY='"https://github.com/example/custom-edk2.git"' \
	>"$temporary/relative-path.log" 2>&1; then
	printf '%s\n' 'ERROR: missing relative legacy certificate survived' >&2
	exit 1
fi
grep -q 'payloads/external/edk2/workspace/custom-edk2/relative-signer.pem' \
	"$temporary/relative-path.log"
test "$(cat "$temporary/build/coreboot.cap")" = preserved

if [ -n "${CAPSULE_TEST_REAL_ROM:-}" ]; then
	test -f "$CAPSULE_TEST_REAL_ROM"
	cp "$CAPSULE_TEST_REAL_ROM" "$temporary/build/coreboot.rom"
	build_capsule CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT=n \
		CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT=y
	cmp "$CAPSULE_TEST_REAL_ROM" "$temporary/build/coreboot.rom"
	printf '%s\n' 'EFI capsule Make recipe real-ROM typed transport: PASS'
fi

printf '%s\n' \
	'EFI capsule Make recipe typed/legacy/embedded/FMAP/path/atomic-failure: PASS'
