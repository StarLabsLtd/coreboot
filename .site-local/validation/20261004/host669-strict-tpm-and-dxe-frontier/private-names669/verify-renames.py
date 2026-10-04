#!/usr/bin/env python3
import re
import sys
from pathlib import Path

source, original = map(Path, sys.argv[1:])
files = {
    'cpu_arch/model.c': {'flush_cpu_data_cache': 'flush'},
    'capsule_runtime/entry.c': {
        'persist_capsule_variable': 'persist', 'writeback_capsule_scatter': 'writeback'},
    'pci_bus/immutable_entry.c': {'import_coreboot_pci_topology': 'discover'},
}
literal = re.compile(r'"(?:\\.|[^"\\])*"')
for name, replacements in files.items():
    current = (source / 'src/modules' / name).read_text()
    old = (original / name.replace('/', '-')).read_text()
    if literal.findall(current) != literal.findall(old):
        raise SystemExit(f'literal bytes changed: {name}')
    restored = current
    for new, previous in replacements.items():
        if len(re.findall(r'\b' + re.escape(new) + r'\b', restored)) != 2:
            raise SystemExit(f'private definition/reference cardinality changed: {new}')
        restored = re.sub(r'\b' + re.escape(new) + r'\b', previous, restored)
    if re.sub(r'\s+', '', restored) != re.sub(r'\s+', '', old):
        raise SystemExit(f'change beyond private names/prototype formatting: {name}')
print('inverse private names and exact literal bytes: PASS')
