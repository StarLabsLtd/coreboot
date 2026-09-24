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

mutant()
{
	name=$1
	expression=$2
	source="$temporary/$name.c"
	binary="$temporary/$name"

	sed "$expression" \
		"$root/src/soc/intel/common/block/vtd/vtd_transition.c" > "$source"
	cc -std=gnu11 -Wall -Wextra -Werror -O2 -I"$root/tests/lib" \
		-I"$root/src/soc/intel/common/block/vtd" \
		"$root/tests/lib/vtd_transition_standalone_test.c" "$source" \
		-o "$binary"
	if "$binary" >/dev/null 2>&1; then
		printf 'ERROR: %s mutation survived\n' "$name" >&2
		exit 1
	fi
}

mutant probe-requires-writes \
	's/!io->read32)/!io->read32 || !io->write32)/'
mutant transition-write-required \
	's/!io->write32 ||/false ||/'
mutant transition-commit-required \
	's/!io->commit_tables ||/false ||/'
printf '%s\n' 'VT-d PMR transition tests: PASS'
