#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
source=${CAPSULE_MEMORY_MAP_SOURCE:-$root/src/drivers/efi/capsules.c}

check()
{
	python3 - "$source" <<'PY'
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text()

init = source.index("memranges_init(&memory_map")
hard = re.search(
    r"memranges_add_resources\(\s*&memory_map,\s*"
    r"IORESOURCE_RESERVE\s*,\s*IORESOURCE_RESERVE\s*,\s*"
    r"BM_MEM_RESERVED\s*\)", source)
soft = re.search(
    r"memranges_add_resources\(\s*&memory_map,\s*"
    r"IORESOURCE_SOFT_RESERVE\s*,\s*IORESOURCE_SOFT_RESERVE\s*,\s*"
    r"BM_MEM_SOFT_RESERVED\s*\)", source)
discover = source.index("discover_capsule_blocks(", init)
pick = source.index("static struct memory_range pick_buffer(")
ram_filter = source.index("range_entry_tag(r) != BM_MEM_RAM", pick)

assert hard is not None, "hard reservation overlay missing"
assert soft is not None, "soft reservation overlay missing"
assert init < hard.start() < soft.start() < discover, \
    "reservation overlays must precede capsule discovery/allocation"
assert ram_filter > pick, "pick_buffer must retain a RAM-only allocation filter"
PY
}

check
[ "${CAPSULE_MEMORY_MAP_CHECK_ONLY:-0}" = 1 ] && exit 0

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

reject()
{
	if CAPSULE_MEMORY_MAP_SOURCE="$tmp/capsules.c" \
		CAPSULE_MEMORY_MAP_CHECK_ONLY=1 "$0" >/dev/null 2>&1; then
		echo "capsule memory-map contract accepted mutation: $1" >&2
		exit 1
	fi
}

cp "$source" "$tmp/capsules.c"
python3 - "$tmp/capsules.c" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
old = "IORESOURCE_RESERVE,\n\t\t\t\tBM_MEM_RESERVED"
new = "IORESOURCE_RESERVE,\n\t\t\t\tBM_MEM_RAM"
assert old in text
path.write_text(text.replace(old, new, 1))
PY
reject hard-reservation-tag

cp "$source" "$tmp/capsules.c"
python3 - "$tmp/capsules.c" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
old = "IORESOURCE_SOFT_RESERVE,\n\t\t\t\tIORESOURCE_SOFT_RESERVE, BM_MEM_SOFT_RESERVED"
new = "IORESOURCE_SOFT_RESERVE,\n\t\t\t\tIORESOURCE_SOFT_RESERVE, BM_MEM_RAM"
assert old in text
path.write_text(text.replace(old, new, 1))
PY
reject soft-reservation-tag

cp "$source" "$tmp/capsules.c"
python3 - "$tmp/capsules.c" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
old = "\tmemranges_add_resources(&memory_map, IORESOURCE_RESERVE, IORESOURCE_RESERVE,\n"
assert old in text
path.write_text(text.replace(old, "\t/* removed reservation overlay */\n", 1))
PY
reject hard-reservation-omission

cp "$source" "$tmp/capsules.c"
python3 - "$tmp/capsules.c" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
old = "\tmemranges_add_resources(&memory_map, IORESOURCE_SOFT_RESERVE,\n"
assert old in text
path.write_text(text.replace(old, "\t/* removed soft reservation overlay */\n", 1))
PY
reject soft-reservation-omission
