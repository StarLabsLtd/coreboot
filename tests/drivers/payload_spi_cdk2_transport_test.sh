#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd -P)
cdk2=${1:?frozen CDK2 source snapshot required}
gate=${2:?retained gate directory required}
expected_old=${3:-0}
test "$expected_old" = 0 || test "$expected_old" = 1
causal_define=
test "$expected_old" = 0 || causal_define=-DEXPECT_OLD_SEQUENCE
mkdir -p "$gate"
sh "$root/tests/drivers/payload_spi_native_config.sh" "$gate/include"
# Replace only the unit-test equivalent of the physical outb instruction.
# All caller sequencing and actual SMM request/parser/sink code stay intact.
test "$(grep -c 'request->status = spi_console_test_status;' "$cdk2/src/lib/diagnostic.c")" = 1
sed 's/request->status = spi_console_test_status;/cdk2_spi_host_submit(request);/' \
  "$cdk2/src/lib/diagnostic.c" > "$gate/diagnostic-host.c"
for optimization in O0 O2; do
 cc -std=gnu11 -"$optimization" -Wall -Wextra -Werror \
  -Wno-sign-compare -Wno-unused-parameter -fshort-wchar \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -ffunction-sections -fdata-sections -Wl,--gc-sections -DCDK2_DIAG_UNIT_TEST $causal_define \
  -include "$root/tests/drivers/spi-native-mocks/environment.h" \
  -include "$root/tests/drivers/spi-native-mocks/submit.h" \
  -I"$root/tests/drivers/spi-native-mocks" \
  -I"$gate/include" \
  -I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
  -I"$cdk2/include" "$root/tests/drivers/payload_spi_cdk2_transport_test.c" \
  "$root/src/commonlib/region.c" "$gate/diagnostic-host.c" \
  -o "$gate/$optimization" > "$gate/$optimization-build.log" 2>&1
 status=0
 ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 \
 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  "$gate/$optimization" > "$gate/$optimization-test.log" 2>&1 || status=$?
 printf '%s\n' "$status" > "$gate/$optimization.exit"
 test "$status" = "$expected_old"
 if test "$expected_old" = 1; then
  grep -q '^EXPECTED: old write-before-flush fails exact pending suffix' "$gate/$optimization-test.log"
 fi
done
