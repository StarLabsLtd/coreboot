#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

run_suite()
{
	binary=$1
	for case in success max overflow readback side-effect class-side-effect \
		invalid alias context-alias workspace-alias io-mutation late-io-mutation \
		initial-mutation hot-add remove identity class command bme mutation \
		invalid-reuse header; do
		"$binary" "$case" >/dev/null || return 1
	done
}

for flags in '-O0' '-O2' '-O1 -fsanitize=address,undefined'; do
	cc -std=gnu11 -Wall -Wextra -Werror $flags -idirafter "$root/src/include" \
		-I"$root/src/commonlib/bsd/include" \
		"$root/tests/lib/pci_bme_quiesce_test.c" \
		"$root/src/lib/pci_bme_quiesce.c" -o "$tmp/test"
	run_suite "$tmp/test"
done

mutant_test()
{
	name=$1
	expression=$2
	mutant="$tmp/$name.c"
	binary="$tmp/$name"

	sed "$expression" "$root/src/lib/pci_bme_quiesce.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/pci_bme_quiesce.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	cc -std=gnu11 -Wall -Wextra -Werror -O2 -idirafter "$root/src/include" \
		-I"$root/src/commonlib/bsd/include" \
		"$root/tests/lib/pci_bme_quiesce_test.c" "$mutant" -o "$binary"
	if run_suite "$binary" >/dev/null 2>&1; then
		echo "ERROR: $name mutant survived" >&2
		exit 1
	fi
}

mutant_test retained-bme 's/if (command & PCI_COMMAND_MASTER)/if (false)/'
mutant_test topology-compare \
	's/memcmp(&function, &saved, sizeof(function))/(memcmp(\&function, \&saved, sizeof(function)) \&\& false)/'
mutant_test snapshot-zero \
	's/memset(snapshot, 0, sizeof(\*snapshot));/\/\* mutant: retained output \*\//'
mutant_test callback-loop-bound \
	'/static enum cb_err observe/,/^}/ s/bus < bus_count/bus < snapshot->bus_count/'
mutant_test callback-write-index \
	'/static enum cb_err observe/,/^}/ s/functions\[count\]/functions[snapshot->count]/'
mutant_test non-bme-readback \
	's/) != cleared_command/) \& PCI_COMMAND_MASTER/'
mutant_test class-after-write \
	'/static enum cb_err observe/,/^}/ s/\.class = class,/.class = (class \& 0U) | (io->read32(io->context, bus, devfn, PCI_CLASS_REVISION) >> 8),/'
mutant_test initial-callback-mutation \
	's/!snapshot_is_initial(snapshot)/(snapshot_is_initial(snapshot) \&\& false)/'
mutant_test invalid-reuse \
	'/pci_bme_quiesce_revalidate/,/^}/ s/snapshot->failed = 1;/\/\* mutant: no poison \*\//'

cc -std=gnu11 -Wall -Wextra -Werror -O2 -fstack-usage \
	-idirafter "$root/src/include" -I"$root/src/commonlib/bsd/include" \
	-c "$root/src/lib/pci_bme_quiesce.c" -o "$tmp/stack.o"
awk -F '\t' '$1 ~ /pci_bme_quiesce/ && $2 > 512 { exit 1 }' "$tmp/stack.su"
cc -m32 -std=gnu11 -Wall -Wextra -Werror -O2 -fstack-usage \
	-idirafter "$root/src/include" -I"$root/src/commonlib/bsd/include" \
	-c "$root/src/lib/pci_bme_quiesce.c" -o "$tmp/stack32.o"
awk -F '\t' '$1 ~ /pci_bme_quiesce/ && $2 > 512 { exit 1 }' "$tmp/stack32.su"

echo 'PCI BME quiesce tests: PASS'
