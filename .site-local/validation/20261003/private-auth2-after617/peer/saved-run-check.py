# SPDX-License-Identifier: GPL-2.0-only
"""Reapply saved fullgraph predicates; no VM or private media output."""
import hashlib
import re
import struct
from pathlib import Path

TOP = Path('/home/sean/native-private-auth2-final-retry.mPv628')
OUT = TOP / 'artifacts/run.wIor8J'
WT = Path('/home/sean/Documents/.cdk2-worktrees/native-private-auth2-after616')
PHASES = ('DXE_SERVICES ARCH_PROTOCOLS VARIABLES_MIN CAPSULE_EARLY_GATE CAPSULE_RAM '
          'PLATFORM_TABLES PCI_ROOTS PCI_ENUMERATE STORAGE_CONTROLLERS BLOCK_DISCOVERY '
          'FILESYSTEMS CAPSULE_DISK DISPLAY_ADOPT INPUT_UI BOOT_POLICY OS_HANDOFF').split()
COMMON = ('APP_ENTERED READY_CLOSED_OP7_IF1_SET REAL_EBS_CALLBACKS_LIVE '
          'REAL_SET_VIRTUAL_MAP RUNTIME_PHYSICAL_ALIASES_REMOVED '
          'VIRTUAL_GET_SET_CLOSED_OP7 RUNTIME_PASS').split()
ENROLLED = ('AFTER_READY_SETUP_PK_ACCEPTED AFTER_READY_PK_TRUSTED_KEK_ACCEPTED '
            'AFTER_READY_KEK_TRUSTED_DB_ACCEPTED AFTER_READY_ENROLLED_SECURE_BOOT_ON '
            'AFTER_READY_UNSIGNED_CHILD_DENIED AFTER_READY_WRONG_SIGNER_CHILD_DENIED '
            'SIGNED_CHILD_EXECUTED AFTER_READY_SIGNED_CHILD_RETURNED '
            'VIRTUAL_NEWER_AUTH2_ACCEPTED VIRTUAL_AUTH2_REPLAY_DENIED_UNCHANGED '
            'VIRTUAL_WRONG_AUTH2_DENIED_UNCHANGED VIRTUAL_PRIVATE_AUTH2_CREATED_BOUND '
            'VIRTUAL_PRIVATE_WRONG_AUTH2_DENIED_UNCHANGED '
            'VIRTUAL_PRIVATE_AUTH2_REPLAY_DENIED_UNCHANGED '
            'VIRTUAL_PRIVATE_AUTH2_APPENDED_BOUND VIRTUAL_PRIVATE_AUTH2_DELETED_UNBOUND').split()


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def manifest(path, base):
    count = 0
    for line in path.read_text().splitlines():
        expected, name = line.split('  ', 1)
        target = Path(name)
        if not target.is_absolute():
            target = base / target
        assert digest(target) == expected, target
        count += 1
    return count


for case in ('ordinary', 'enrolled'):
    lines = (OUT / f'{case}.serial.log').read_text().splitlines()
    assert (OUT / f'{case}.vm-status').read_text().strip() == '3'
    assert not any(line.startswith('CDK2_PUBLIC_FULLGRAPH_RUNTIME_FAIL') for line in lines)
    assert not any(re.match(r'CDK2 \| (' + '|'.join(PHASES) + r') \| failed \| ', line)
                   for line in lines)
    for stage in COMMON + (ENROLLED if case == 'enrolled' else []):
        assert sum(line.startswith('CDK2_PUBLIC_FULLGRAPH_' + stage) for line in lines) == 1
    if case == 'ordinary':
        assert not any(line.startswith('CDK2_PUBLIC_FULLGRAPH_AFTER_READY_') for line in lines)
        assert not any(re.match(r'CDK2_PUBLIC_FULLGRAPH_VIRTUAL_.*AUTH2', line) for line in lines)
    assert (OUT / f'{case}.before-smmstore.bin').read_bytes() != \
        (OUT / f'{case}.after-smmstore.bin').read_bytes()
absent = (OUT / 'absent.serial.log').read_text()
general = (OUT / 'old-general.serial.log').read_text()
for line in ('CDK2 | VariableRuntimeDxe | required driver | NOT_FOUND ',
             'CDK2 | VARIABLES_MIN | failed | NOT_FOUND ',
             'CDK2 | module 0x02 | variable policy protocols | 0x0000000000000000'):
    assert line in absent
assert 'CDK2 | VariableRuntimeDxe | required driver | SUCCESS ' not in absent
assert 'CDK2_PUBLIC_FULLGRAPH_' not in absent
assert (OUT / 'absent.before-smmstore.bin').read_bytes() == \
    (OUT / 'absent.after-smmstore.bin').read_bytes()
for line in ('CDK2 | VariableRuntimeDxe | required driver | SUCCESS ',
             'CDK2 | EsrtDxe | required driver | UNSUPPORTED ',
             'CDK2 | VARIABLES_MIN | failed | UNSUPPORTED '):
    assert line in general
assert 'CDK2 | EsrtDxe | required driver | SUCCESS ' not in general
assert 'CDK2_PUBLIC_FULLGRAPH_' not in general
for case in ('absent', 'old-general', 'ordinary', 'enrolled'):
    assert (OUT / 'production.elf').read_bytes() == (OUT / f'{case}.elf').read_bytes()
assert (OUT / 'source-before.sha256').read_bytes() == (OUT / 'source-after.sha256').read_bytes()
assert (WT / 'src/modules/dxe_core/entry.c').read_bytes() == \
    (OUT / 'source/src/modules/dxe_core/entry.c').read_bytes()
counts = {name: manifest(TOP / name, WT) for name in
          ('source-before.sha256', 'inputs-before.sha256', 'codec-closure-before.sha256')}
counts.update({name: manifest(OUT / name, OUT / 'source') for name in
               ('source-before.sha256', 'native.outputs.sha256', 'payload.inputs.sha256')})
config = (OUT / 'configured/include/cdk2/config.h').read_text()
for setting in ('PROTECTED_VARIABLE_RUNTIME 1', 'NATIVE_SECURITY_STUB 1',
                'SECURE_BOOT 0', 'NATIVE_SYSTEM_FMP 0'):
    assert '#define CONFIG_CDK2_' + setting in config
public = OUT / 'configured/native/fullgraph-enrolled-inputs'
der = (public / 'db.cert.der').read_bytes()


def sequence(offset):
    assert der[offset] == 0x30
    size = der[offset + 1]
    header = 2
    if size & 0x80:
        width = size & 0x7f
        assert 0 < width <= 4 and offset + 2 + width <= len(der)
        size = int.from_bytes(der[offset + 2:offset + 2 + width], 'big')
        header += width
    end = offset + header + size
    assert end <= len(der)
    return offset + header, end


tbs, end = sequence(0)
assert end == len(der)
_, tbs_end = sequence(tbs)
binding = hashlib.sha256(b'native-service-db' + der[tbs:tbs_end]).digest()
name = 'Cdk2PrivateAuth2Diagnostic'.encode('utf-16-le')
node = bytes.fromhex('269f79375917494db8da1d6fc47b85c0') + \
    struct.pack('<III', 28 + len(name) + len(binding), len(name) // 2, len(binding)) + \
    name + binding
assert (public / 'private.binding.bin').read_bytes() == binding
assert (public / 'private_certdb.bin').read_bytes() == struct.pack('<I', 4 + len(node)) + node
print('Saved four-case outcomes/marker cardinality/identical Core/input bindings: PASS', counts)
print('Independent ASCII CN + actual DER TBSCertificate binding and full certdb bytes: PASS')
print('Existing fullgraph profile: SecurityStub=1/buildSecureBoot=0/SystemFmp=0; no new VM.')
