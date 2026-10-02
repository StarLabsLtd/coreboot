#!/usr/bin/env python3
"""HOST-only exact signed fixture binding; no producer trust/state changes."""
import hashlib
from pathlib import Path
import sys

tools = Path('/home/sean/Documents/.coreboot-worktrees/q35-exclusive-e8-dma-scope-after385/util/efi_capsule')
sys.path.insert(0, str(tools))
from validate_capsule import ValidationError, parse_capsule_details, verify_signature

root = Path(__file__).resolve().parent
reference = Path('/home/sean/system-fmp-provider-session-final.GCQCjf/signed-capsule.bin').read_bytes()
target = Path('/home/sean/system-fmp-provider-session-final.GCQCjf/newer-pristine-full-core.rom').read_bytes()
trust = '/home/sean/q35-capsule-native-signing.PuAsza/trust.pem'
details = parse_capsule_details(reference)
assert details['image'] == target and details['fw_version'] == 0x001a000a
verify_signature(details, trust)

def must_refuse_signature(parsed):
    try:
        verify_signature(parsed, trust)
    except ValidationError as error:
        assert str(error) == 'capsule PKCS#7 verification failed'
    else:
        raise AssertionError('unexpected configured-trust verification')

wrong = parse_capsule_details((root / 'wrong-signer-a.cap').read_bytes())
assert wrong['image'] == target and wrong['fw_version'] == 0x001a000a
assert wrong['lsv'] == 0x001a0009 and wrong['flags'] == 0x50000
verify_signature(wrong, str(root / 'wrong-signer.pem'))
must_refuse_signature(wrong)
below = parse_capsule_details((root / 'signed-below-floor-8.cap').read_bytes())
assert below['image'] == target and below['fw_version'] == 0x001a0008
assert below['lsv'] == 0x001a0008 and below['flags'] == 0x50000
verify_signature(below, trust)

changed = reference[:-1] + bytes([reference[-1] ^ 1])
corrupt = parse_capsule_details(changed)
for key in details:
    if key in ('authenticated', 'payload', 'image'):
        assert corrupt[key] == details[key][:-1] + bytes([details[key][-1] ^ 1])
    else:
        assert corrupt[key] == details[key]
must_refuse_signature(corrupt)
(root / 'signed-byte-corrupt-a.cap').write_bytes(changed)
for name in ('wrong-signer-a.cap', 'signed-below-floor-8.cap', 'signed-byte-corrupt-a.cap'):
    print(name, hashlib.sha256((root / name).read_bytes()).hexdigest())
print('Actual capsule parser/CMS binding: wrong signer, one signed byte, genuine below-floor signature PASS')
