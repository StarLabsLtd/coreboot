#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
gate=${1:?retained inventory gate directory required}
mkdir -p "$gate"
test "$(grep -Fxc '# Audited active registry ownership inventory begins.' \
 "$root/tests/cpu/x86/smm_command_test.sh")" -eq 1
test "$(grep -Fxc '# Audited active registry ownership inventory ends.' \
 "$root/tests/cpu/x86/smm_command_test.sh")" -eq 1
awk '/^# Audited active registry ownership inventory begins\.$/ {copy=1; next} \
 /^# Audited active registry ownership inventory ends\.$/ {copy=0} \
 copy {print}' "$root/tests/cpu/x86/smm_command_test.sh" > "$gate/guard.sh"
test -s "$gate/guard.sh"
(cd "$root" && root="$root" sh -e "$gate/guard.sh") > "$gate/baseline.log" 2>&1
printf '0\n' > "$gate/baseline.exit"
for inverse in outsider-select outsider-call duplicate-allowed-call; do
 overlay="$gate/$inverse"
 mkdir -p "$overlay"
 (cd "$root" && cp --parents src/mainboard/emulation/qemu-q35/Kconfig \
  src/mainboard/emulation/qemu-q35/native_service_receiver.c \
  src/mainboard/emulation/qemu-q35/public_service.c \
  src/mainboard/starlabs/starbook/variants/mtl/authvar_service_runtime_dispatch.c \
  src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.c \
  src/lib/payload_mm_authvar_presence_route_session.c \
  src/lib/payload_mm_authvar_presence_lifecycle_close_route.c \
  src/lib/payload_mm_authvar_presence_lifecycle_close_s3_route.c \
  src/cpu/x86/Kconfig "$overlay")
 case "$inverse" in
 outsider-select)
  cp "$root/src/mainboard/emulation/qemu-q35/Kconfig" "$overlay/src/outsider-Kconfig"
  expected='APMC registry selected outside the attested Q35 owner'
  ;;
 outsider-call)
  cp "$root/src/mainboard/emulation/qemu-q35/native_service_receiver.c" "$overlay/src/outsider.c"
  expected='registry select escaped the audited owner inventory'
  ;;
 duplicate-allowed-call)
  sed 's/smm_apmc_command_select(/smm_apmc_command_select(); smm_apmc_command_select(/' \
   "$root/src/mainboard/emulation/qemu-q35/native_service_receiver.c" \
   > "$overlay/src/mainboard/emulation/qemu-q35/native_service_receiver.c"
  expected='registry call inventory changed: mainboard/emulation/qemu-q35/native_service_receiver.c select'
  ;;
 esac
 status=0
 (cd "$overlay" && root="$overlay" sh -e "$gate/guard.sh") > "$overlay/guard.log" 2>&1 || status=$?
 printf '%s\n' "$status" > "$overlay/guard.exit"
 test "$status" = 1
 grep -Fxq "$expected" "$overlay/guard.log"
done
