#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd -P)
gate=${1:?retained gate directory required}
mkdir -p "$gate"
sh "$root/tests/drivers/payload_spi_native_config.sh" "$gate/include"
for optimization in O0 O2; do
 for family in prefix lease; do
  cc -std=gnu11 -"$optimization" -Wall -Wextra -Werror -g \
    -Wno-sign-compare -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -include "$root/tests/drivers/spi-native-mocks/environment.h" \
    -I"$root/tests/drivers/spi-native-mocks" \
    -I"$gate/include" \
    -I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
    "$root/tests/drivers/payload_spi_native_${family}_test.c" \
    "$root/src/commonlib/region.c" -o "$gate/$optimization-$family" \
    > "$gate/$optimization-$family-build.log" 2>&1
  ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$gate/$optimization-$family" > "$gate/$optimization-$family-test.log" 2>&1
  printf '0\n' > "$gate/$optimization-$family.exit"
 done
done
