# SPDX-License-Identifier: GPL-2.0-only
"""Finite read-only saved proof check; no VM, media write or admission."""
import argparse
import csv
import hashlib
import importlib.util
import json
import subprocess
import sys
import tarfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source-repo', type=Path, default=Path('/home/sean/Documents/.cdk2-worktrees/certified-refusal-continuation-peer-after595.3Qdgg0'))
parser.add_argument('--compare-originals', action='store_true')
args = parser.parse_args()
packet = Path(__file__).resolve().parent
commit = '36d58d744f87d844b1ed7599717d06569dbac9a8'
with (packet / 'source-after597.tsv').open() as stream:
    source_rows = list(csv.DictReader(stream, delimiter='\t'))
assert len(source_rows) == 6
with tarfile.open(packet / 'source-after597.tar.gz', 'r:gz') as archive:
    members = archive.getmembers()
    assert len(members) == 6 and all(item.isfile() for item in members)
    for row in source_rows:
        data = archive.extractfile(row['path']).read()
        assert row['commit'] == commit
        assert hashlib.sha256(data).hexdigest() == row['sha256']
        assert subprocess.check_output(['git', '-C', str(args.source_repo), 'show', commit + ':' + row['path']]) == data
        assert subprocess.check_output(['git', '-C', str(args.source_repo), 'rev-parse', commit + ':' + row['path']]).decode().strip() == row['blob']
runner_path = args.source_repo / 'tests/system_fmp_core_ram_native_test.py'
expected_runner = next(row['sha256'] for row in source_rows if row['path'] == 'tests/system_fmp_core_ram_native_test.py')
assert hashlib.sha256(runner_path.read_bytes()).hexdigest() == expected_runner
sys.path.insert(0, str(args.source_repo / 'tests'))
spec = importlib.util.spec_from_file_location('actual_runner', runner_path)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
cases = [('root-wrong-signer', 'wrong-signer'), ('root-signed-byte', 'signed-byte'), ('peer-wrong-signer', 'wrong-signer')]
for label, case in cases:
    run = packet / label / 'run'
    result = json.loads((run / 'result.json').read_text())
    assert result['failure'] is None and result['qemu_status'] == 3
    assert result['refusal_observed'] is True
    assert result['completion'] == 'guest continuation after certified refusal'
    assert result['expected_certified_refusal'] == case
    assert result['budget_seconds'] == 180 and 0 < result['wall_seconds'] <= 180
    before = json.loads((run / 'inputs-before.json').read_text())
    assert before == json.loads((run / 'inputs-after.json').read_text())
    capsule_path = next(path for path in before if path.endswith('/wrong-signer-a.cap' if case == 'wrong-signer' else '/signed-byte-corrupt-a.cap'))
    with (packet / 'external-input-hashes.tsv').open() as stream:
        hashes = {row['original_path']: row for row in csv.DictReader(stream, delimiter='\t')}
    runner.check_boots(sorted(run.glob('boot-*')), int(hashes[capsule_path]['bytes']), refusal=case, certified=True)
    runner.check_refusal_markers((run / 'serial.log').read_bytes(), result['events'], True)
    assert (packet / label / 'source-head.txt').read_text().strip() == '31e3a5bae19db98457bfbd61fb1b37ed6b0ade52'
    assert (packet / label / 'source-status.txt').stat().st_size == 0
    if args.compare_originals:
        original = Path('/home/sean/certified-refusal-continuation-peer.Js7GPg/wrong-signer') if label.startswith('peer') else Path('/home/sean/normal-refusal-consumed-producer.VPNIdB') / ('wrong-signer' if case == 'wrong-signer' else 'signed-byte')
        for name in ('native.log', 'native.time', 'app.log', 'source-head.txt', 'source-status.txt'):
            assert (packet / label / name).read_bytes() == (original / name).read_bytes()
        for saved in run.rglob('*'):
            if saved.is_file():
                assert saved.read_bytes() == (original / 'run-1' / saved.relative_to(run)).read_bytes()
    print(label + ': actual saved epochs/markers/maps/source/archive PASS')
for path in packet.rglob('*'):
    if path.is_file():
        assert path.suffix not in ('.rom', '.raw', '.cap', '.efi', '.o', '.elf', '.key')
        if path.suffix == '.bin':
            assert path.name.startswith('cbmem-') or path.name.startswith('publication-')
print('Finite saved receipt check PASS; not a new VM or current historical path-hash assertion.')
