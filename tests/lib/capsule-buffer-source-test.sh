#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
capsules=${CAPSULES_SOURCE:-$root/src/drivers/efi/capsules.c}
clear=${MEMORY_CLEAR_SOURCE:-$root/src/security/memory/memory_clear.c}

check()
{
	python3 - "$capsules" "$clear" <<'PY'
import pathlib, re, sys
c = pathlib.Path(sys.argv[1]).read_text()
m = pathlib.Path(sys.argv[2]).read_text()
assert c.count("static struct memory_range coalesce_buffer;") == 1
assert not re.search(r"extern[^;]*coalesce_buffer", c + m)
publication = ("coalesce_capsules(block_chain, (void *)(uintptr_t)candidate.base);\n"
               "\t\tif (uefi_capsule_count > 0)\n"
               "\t\t\tcoalesce_buffer = candidate;")
assert c.count(publication) == 1
assert c.count("coalesce_buffer = candidate;") == 1
assert c.count("efi_capsule_coalesce_span(&base, &size)") == 1
assert m.count("efi_capsule_coalesce_span(&capsule_base, &capsule_size)") == 1
assert m.index("efi_capsule_coalesce_span(&capsule_base, &capsule_size)") < m.index(
    "memranges_each_entry(r, &mem)")
assert c.count("coalesce_buffer.") == 8
PY
}

check
[ "${CAPSULE_SOURCE_CHECK_ONLY:-0}" = 1 ] && exit 0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cp "$capsules" "$tmp/capsules.c"
cp "$clear" "$tmp/memory_clear.c"
capsules=$tmp/capsules.c MEMORY_CLEAR_SOURCE=$tmp/memory_clear.c

mutate_reject()
{
	if CAPSULES_SOURCE=$capsules MEMORY_CLEAR_SOURCE=$MEMORY_CLEAR_SOURCE \
		CAPSULE_SOURCE_CHECK_ONLY=1 \
		"$0" >/dev/null 2>&1; then
		echo "source contract accepted mutation: $1" >&2
		exit 1
	fi
}

python3 - "$capsules" <<'PY'
import pathlib, sys
p=pathlib.Path(sys.argv[1]); s=p.read_text()
p.write_text(s.replace("if (uefi_capsule_count > 0)", "if (true)", 1))
PY
mutate_reject unguarded-publication
cp "$root/src/drivers/efi/capsules.c" "$capsules"
python3 - "$MEMORY_CLEAR_SOURCE" <<'PY'
import pathlib, sys
p=pathlib.Path(sys.argv[1]); s=p.read_text()
p.write_text(s.replace("efi_capsule_coalesce_span(&capsule_base, &capsule_size)",
                       "false", 1))
PY
mutate_reject missing-pre-clear-consumer
cp "$root/src/security/memory/memory_clear.c" "$MEMORY_CLEAR_SOURCE"
python3 - "$capsules" <<'PY'
import pathlib, sys
p=pathlib.Path(sys.argv[1]); s=p.read_text()
p.write_text(s.replace("efi_capsule_coalesce_span(&base, &size)", "false", 1))
PY
mutate_reject missing-bootmem-consumer
