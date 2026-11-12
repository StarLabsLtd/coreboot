#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Resolve native profiles through make and assert their effective identities."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

root = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)


def config(path):
    values = {}
    for line in path.read_text().splitlines():
        match = re.fullmatch(r'(CONFIG_[A-Z0-9_]+)=(.*)', line)
        if match:
            key, value = match.groups()
            assert key not in values, f'{path}: duplicate {key}'
            values[key] = value
        else:
            match = re.fullmatch(r'# (CONFIG_[A-Z0-9_]+) is not set', line)
            if match:
                values[match[1]] = 'n'
    return values


profiles = [(path, config(path)) for path in sorted((root / 'configs').glob('config.starlabs_*'))]
profiles = [(path, requested) for path, requested in profiles
            if requested.get('CONFIG_PAYLOAD_CDK2') == 'y']
assert len(profiles) == 19, f'Native profile inventory changed: {len(profiles)}'
# These board-owned defaults are absent from their defconfigs. Assert the
# intended identity rather than accepting whichever default Kconfig resolved.
default_guids = {
    'CONFIG_BOARD_STARLABS_ADL_HORIZON': '"634fefb2-7099-4e5c-8b85-bd458132f51e"',
    'CONFIG_BOARD_STARLABS_STARBOOK_RPL_U': '"41627bd8-70a0-490d-b834-5de90ccfb56d"',
    'CONFIG_BOARD_STARLABS_STARFIGHTER_MTL': '"75b29ff6-e882-41c7-903f-5af89e004113"',
}
for path, requested in profiles:
    directory = out / path.name
    directory.mkdir(exist_ok=True)
    resolved = directory / 'config'
    shutil.copyfile(path, resolved)
    command = ['make', 'olddefconfig', f'DOTCONFIG={resolved}', f'obj={directory / "build"}']
    with (directory / 'resolve.log').open('w') as log:
        subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
    actual = config(resolved)
    boards = [key for key, value in requested.items() if key.startswith('CONFIG_BOARD_') and value == 'y']
    assert len(boards) == 1, (path, boards)
    for key in (boards[0], 'CONFIG_ARCH_X86', 'CONFIG_PAYLOAD_CDK2', 'CONFIG_SMMSTORE',
                'CONFIG_CDK2_SMMSTORE_VARIABLE_RUNTIME'):
        assert actual.get(key) == 'y', f'{path}: {key} resolved to {actual.get(key)}'
    for key in ('CONFIG_SMM_AUTHVAR', 'CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME'):
        assert actual.get(key, 'n') == 'n', f'{path}: unexpected protected owner {key}'
    guid = 'CONFIG_DRIVERS_EFI_MAIN_FW_GUID'
    expected_guid = requested.get(guid, default_guids.get(boards[0]))
    if expected_guid is not None:
        assert actual.get(guid) == expected_guid, f'{path}: firmware GUID changed'
    else:
        assert actual.get(guid, '""') == '""', f'{path}: unexpected hardware update GUID'
        assert actual.get('CONFIG_DRIVERS_EFI_UPDATE_CAPSULES', 'n') == 'n'
    print(f'PASS {path.name}: {boards[0]}, x86, CDK2, ordinary SMMSTORE, GUID={actual.get(guid, "absent")}', flush=True)
assert not (root / 'configs/config.starlabs_starfighter_phx').exists(), 'Unsupported Phoenix profile returned'
