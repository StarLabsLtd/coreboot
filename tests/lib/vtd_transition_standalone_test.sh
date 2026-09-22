#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

for flags in '-O0' '-O2' '-O1 -fsanitize=address,undefined'; do
	cc -std=gnu11 -Wall -Wextra -Werror $flags \
		"$root/tests/lib/vtd_transition_standalone_test.c" \
		"$root/src/soc/intel/common/block/vtd/vtd_transition.c" \
		-o "$temporary/test"
	"$temporary/test"
done
printf '%s\n' 'VT-d PMR transition tests: PASS'
