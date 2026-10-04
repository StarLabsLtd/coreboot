#!/usr/bin/env python3
"""Bounded source/tool/file receipts for the released local host gate."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

if sys.flags.optimize or 'PYTHONOPTIMIZE' in os.environ:
    raise RuntimeError('optimized Python is forbidden')

def sha(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            value.update(block)
    return value.hexdigest()

records = []
for spelling in sys.argv[1:]:
    path = Path(spelling).resolve()
    if path.is_file():
        records.append({'path': str(path), 'sha256': sha(path)})
        continue
    head = subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
    status = subprocess.check_output(['git', '-C', str(path), 'status', '--porcelain', '--untracked-files=normal'], text=True)
    records.append({'root': str(path), 'head': head, 'status': status})
    index = subprocess.check_output(['git', '-C', str(path), 'ls-files', '-s', '-z'])
    for row in index.split(b'\0'):
        if not row:
            continue
        attributes, name = row.split(b'\t', 1)
        mode, oid, stage = attributes.decode().split()
        if stage != '0':
            raise ValueError('unmerged source')
        file = path / os.fsdecode(name)
        record = {'path': str(file), 'mode': mode}
        if mode == '160000':
            record['gitlink'] = oid
        elif mode == '120000':
            if not file.is_symlink():
                raise ValueError('tracked link changed type')
            record['target'] = os.readlink(file)
        else:
            if not file.is_file() or file.is_symlink():
                raise ValueError('tracked regular source changed type')
            record['sha256'] = sha(file)
        records.append(record)
print(json.dumps(records, indent=2, sort_keys=True))
