#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd -P)
cdk2=${1:?frozen corrected CDK2 snapshot required}
oldcaller=${2:?original unmodified caller required}
gate=${3:?retained causal gate directory required}
mkdir -p "$gate"
# Retain isolated actual-source overlays; never mutate the reviewed proposal.
for inverse in old-sequence erase-omission prefix-omission; do
 overlay="$gate/$inverse"
 mkdir -p "$overlay/src/drivers" "$overlay/tests" "$overlay/cdk2/src/lib"
 cp -a "$root/src/drivers/spi" "$overlay/src/drivers/"
 cp -a "$root/src/commonlib" "$root/src/include" "$overlay/src/"
 cp -a "$root/tests/drivers" "$overlay/tests/"
 cp -a "$cdk2/include" "$overlay/cdk2/"
 case "$inverse" in
 old-sequence)
  cp "$oldcaller" "$overlay/cdk2/src/lib/diagnostic.c"
  sh "$overlay/tests/drivers/payload_spi_cdk2_transport_test.sh" "$overlay/cdk2" "$overlay/receipt" 1
  for opt in O0 O2; do
   grep -q '^EXPECTED: old write-before-flush fails exact pending suffix' "$overlay/receipt/$opt-test.log"
  done
  ;;
 erase-omission|prefix-omission)
  if test "$inverse" = erase-omission; then
   sed 's/rdev_eraseat(\&candidate, 0, size) != size/false/' "$root/src/drivers/spi/flashconsole.c" > "$overlay/src/drivers/spi/flashconsole.c"
   define=EXPECT_ERASE_OMISSION
   expected='EXPECTED: omitted reclaim erase rejects stale prior-boot prefix'
  else
   sed 's/(prefix_size \&\& !flashconsole_append(prefix, prefix_size))/(false)/' "$root/src/drivers/spi/payload_spi_console_smi.c" > "$overlay/src/drivers/spi/payload_spi_console_smi.c"
   define=EXPECT_PREFIX_OMISSION
   expected='EXPECTED: omitted native prefix loses complete current coreboot stream'
  fi
  for opt in O0 O2; do
   sh "$overlay/tests/drivers/payload_spi_native_config.sh" "$overlay/include"
   cc -std=gnu11 -"$opt" -Wall -Wextra -Werror -Wno-sign-compare -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-sanitize-recover=all -D"$define" \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -include "$overlay/tests/drivers/spi-native-mocks/environment.h" \
    -I"$overlay/tests/drivers/spi-native-mocks" \
    -I"$overlay/include" \
    -I"$overlay/src/commonlib/include" -I"$overlay/src/commonlib/bsd/include" \
    "$overlay/tests/drivers/payload_spi_native_prefix_test.c" "$overlay/src/commonlib/region.c" \
    -o "$overlay/$opt" > "$overlay/$opt-build.log" 2>&1
   status=0
   ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$overlay/$opt" > "$overlay/$opt-test.log" 2>&1 || status=$?
   test "$status" = 1
   printf '%s\n' "$status" > "$overlay/$opt.exit"
   grep -q "^$expected" "$overlay/$opt-test.log"
  done
  ;;
 esac
done
