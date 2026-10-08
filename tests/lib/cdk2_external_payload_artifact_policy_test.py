#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""HOST policy controls with modeled CBFS IO and a genuine retained native ELF.

These are not packed-ROM equivalence or menu-route build qualification.
"""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

if len(sys.argv) != 4:
    raise SystemExit(f'usage: {Path(sys.argv[0]).name} artifact-gate resolved-producer-output retained-native-output')
gate, producer_output, native_output = map(Path, sys.argv[1:])
inputs = [gate, Path(__file__), producer_output / 'full.config',
          producer_output / 'build/config.h', native_output / 'resolved.config',
          native_output / 'include/cdk2/config.h', native_output / 'native/cdk2-elfcheck',
          native_output / 'native/cdk2-coreboot-image.elf']
def snapshot():
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
before = snapshot()
with tempfile.TemporaryDirectory(prefix='menu-artifact-policy.') as temporary:
    base = Path(temporary)
    build = base / 'build'
    (build / 'cdk2/native').mkdir(parents=True)
    (build / 'cdk2/include/cdk2').mkdir(parents=True)
    old_gate = base / 'old-reader-policy.sh'
    current = gate.read_text()
    marker = 'CONFIG_(SMMSTORE|DRIVERS_EFI_UPDATE_CAPSULES)'
    assert current.count(marker) == 1
    old_gate.write_text(current.replace(marker, 'CONFIG_(SMMSTORE|DRIVERS_EFI_VARIABLE_STORE)'))
    config = base / 'coreboot.config'
    nested = build / 'cdk2/.config'
    outer_header = build / 'config.h'
    inner_header = build / 'cdk2/include/cdk2/config.h'
    for name in ('cdk2-elfcheck', 'cdk2-coreboot-image.elf'):
        (build / 'cdk2/native' / name).symlink_to(native_output / 'native' / name)
    rom = base / 'modeled.rom'
    rom.write_bytes(b'HOST modeled CBFS, not firmware')
    cbfs = base / 'cbfstool'
    cbfs.write_text('#!/usr/bin/env python3\n'
        'import pathlib,shutil,sys\n'
        'if sys.argv[2]=="print":\n'
        ' print("fallback/payload 0 simple elf\\ncdk2/coreboot-config 0 raw\\ncdk2/config 0 raw")\n'
        'else:\n'
        ' name=sys.argv[sys.argv.index("-n")+1]; out=sys.argv[sys.argv.index("-f")+1]\n'
        ' source=pathlib.Path(sys.argv[1]).parent/("coreboot.config" if name=="cdk2/coreboot-config" else "build/cdk2/.config")\n'
        ' shutil.copyfile(source,out)\n')
    cbfs.chmod(0o755)
    originals = {config: (producer_output / 'full.config').read_bytes(),
                 nested: (native_output / 'resolved.config').read_bytes(),
                 outer_header: (producer_output / 'build/config.h').read_bytes(),
                 inner_header: (native_output / 'include/cdk2/config.h').read_bytes()}
    def restore():
        for path, data in originals.items():
            path.write_bytes(data)
    def run(label, expected, selected=gate):
        result = subprocess.run(['sh', str(selected), str(rom), str(build), str(config), str(cbfs)],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode != expected:
            raise AssertionError((label, expected, result.returncode, result.stdout))
        print(label, result.returncode)
    restore()
    assert b'CONFIG_DRIVERS_EFI_VARIABLE_STORE=y\n' in config.read_bytes()
    run('genuine public reader remains admitted; CBFS modeled', 0)
    run('original gate causally rejects the required public reader', 1, old_gate)
    for symbol in ('SMMSTORE', 'DRIVERS_EFI_UPDATE_CAPSULES'):
        restore()
        config.write_bytes(config.read_bytes()+f'CONFIG_{symbol}=y\n'.encode())
        run('legacy outer '+symbol, 1)
        restore()
        old = f'#define CONFIG_{symbol} 0'.encode()
        assert old in outer_header.read_bytes()
        outer_header.write_bytes(outer_header.read_bytes().replace(old, old[:-1]+b'1'))
        run('generated outer '+symbol, 1)
    for symbol in ('CDK2_NATIVE_SMMSTORE_FVB', 'CDK2_NATIVE_FTW'):
        restore()
        old = f'#define CONFIG_{symbol} 0'.encode()
        assert old in inner_header.read_bytes()
        inner_header.write_bytes(inner_header.read_bytes().replace(old, old[:-1]+b'1'))
        run('generated native '+symbol, 1)
    restore()
    assert b'CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME=y\n' in nested.read_bytes()
    config.write_bytes(config.read_bytes().replace(
        b'CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED=y\n',
        b'# CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED is not set\n'))
    run('protected Runtime without source-attested MM route', 1)
assert snapshot() == before
print('PASS HOST policy pairing and exact input bookends; no menu ROM qualification')
