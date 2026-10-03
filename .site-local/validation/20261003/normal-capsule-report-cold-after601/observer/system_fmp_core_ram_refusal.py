# SPDX-License-Identifier: GPL-2.0-only
"""HOST-only binding of three deliberately refused normal RAM capsules."""

import argparse
import sys
import uuid
from pathlib import Path

CASES = ("wrong-signer", "signed-byte", "below-floor")


def validate_refusal(tools, capsule, target, trust, reference, signer, case, attempt):
    sys.path.insert(0, str(tools))
    from validate_capsule import ValidationError, parse_capsule_details, validate, verify_signature

    if case not in CASES or reference is None:
        raise ValueError("known refusal and authenticated reference required")
    original = reference.read_bytes()
    image = target.read_bytes()
    expected_guid = uuid.UUID("00112233-4455-6677-8899-aabbccddeeff")
    validate(original, None, expected_guid, 0, 0x001a000a, 0x001a0009,
             image, ["COREBOOT"], str(trust), True, require_fmap=True)
    good = parse_capsule_details(original)
    candidate = capsule.read_bytes()
    details = parse_capsule_details(candidate)
    if case == "below-floor":
        if attempt != 0x001a0008:
            raise ValueError("below-floor fixture must attempt version8 below actual floor9")
        validate(candidate, None, expected_guid, 0, attempt, attempt, image,
                 ["COREBOOT"], str(trust), True, require_fmap=True)
    else:
        if attempt != 0x001a000a:
            raise ValueError("CMS refusal must retain the trusted reference attempt")
        if case == "signed-byte":
            changed = original[:-1] + bytes([original[-1] ^ 1])
            if candidate != changed:
                raise ValueError("signed-byte fixture is not the exact one-byte corruption")
            for key in good:
                if key not in ("authenticated", "payload", "image") and details[key] != good[key]:
                    raise ValueError("signed-byte corruption changed capsule framing")
        else:
            if signer is None:
                raise ValueError("wrong-signer fixture requires its own public certificate")
            validate(candidate, None, expected_guid, 0, attempt, 0x001a0009,
                     image, ["COREBOOT"], str(signer), True, require_fmap=True)
            for key in ("payload", "monotonic_count", "flags"):
                if details[key] != good[key]:
                    raise ValueError("wrong signer changed the trusted reference signed body")
        try:
            verify_signature(details, str(trust))
        except ValidationError as error:
            if str(error) != "capsule PKCS#7 verification failed":
                raise
        else:
            raise ValueError("CMS refusal unexpectedly verifies under actual producer trust")
    print(f"Actual capsule tools bound finite {case} refusal; producer public trust unchanged: PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("tools", "capsule", "target", "trust", "reference"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--signer", type=Path)
    parser.add_argument("--case", choices=CASES, required=True)
    parser.add_argument("--attempt", type=lambda text: int(text, 0), required=True)
    arguments = parser.parse_args()
    validate_refusal(arguments.tools, arguments.capsule, arguments.target,
                     arguments.trust, arguments.reference, arguments.signer,
                     arguments.case, arguments.attempt)


if __name__ == "__main__":
    main()
