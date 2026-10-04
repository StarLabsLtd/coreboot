#!/usr/bin/env python3
"""Check the one real evaluated recipe and exact hidden prerequisite binding."""
from pathlib import Path
import os
import sys

if sys.flags.optimize or 'PYTHONOPTIMIZE' in os.environ:
    raise RuntimeError('optimized Python is forbidden')
database, build, root, expected = sys.argv[1:]
target = build + '/native/dxe-core-capsule_report.o'
required = [build + '/include/cdk2/config.h',
            build + '/native/command-inputs', root + '/Makefile',
            root + '/src/boot/Makefile']
in_files = False
current = None
prerequisites = []
recipes = []
for line in Path(database).read_text().splitlines():
    if line == '# Files':
        in_files = True
        continue
    if not in_files:
        continue
    if line and line[0] not in '# \t' and ':' in line:
        if ': .EXTRA_PREREQS' in line:
            continue
        current, remainder = line.split(':', 1)
        prerequisites = remainder.split()
    elif line.startswith('#  recipe to execute') and current == target:
        recipes.append(prerequisites)
if len(recipes) != 1:
    raise RuntimeError('exact capsule object recipe cardinality is not one')
print('target:', target)
print('evaluated prerequisites:', recipes[0])
normalized = [os.path.normpath(value) for value in recipes[0]]
present = [os.path.normpath(value) in normalized for value in required]
print('configuration binding:', dict(zip(required, present)))
if expected == 'registered':
    if not all(present):
        raise RuntimeError('registered recipe lacks an exact configuration input')
elif expected == 'removed':
    if any(present):
        raise RuntimeError('private removal did not remove the exact binding')
else:
    raise RuntimeError('unknown expectation')
if os.path.normpath(root + '/src/lib/capsule_report.c') not in normalized:
    raise RuntimeError('underlying capsule source prerequisite disappeared')
print('exact capsule cache binding ' + expected + ': PASS')
