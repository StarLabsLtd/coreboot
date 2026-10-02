#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Package one public capsule certificate without accepting private material."""

import argparse
import pathlib
import re
import subprocess
import tempfile

from crypto_policy import CryptoPolicyError, MAX_CERTIFICATE_BYTES, validate_pem_bundle


def package(certificate, output):
    certificate = pathlib.Path(certificate)
    output = pathlib.Path(output)
    if certificate.resolve() == output.resolve() or (
        certificate.exists() and output.exists() and certificate.samefile(output)
    ):
        raise CryptoPolicyError("trust output must not alias the public certificate")
    # A failed rebuild must not leave an older trust artifact usable.
    output.unlink(missing_ok=True)
    data = certificate.read_bytes()
    if re.fullmatch(
        br"\s*-----BEGIN CERTIFICATE-----\s+[A-Za-z0-9+/=\r\n]+"
        br"-----END CERTIFICATE-----\s*", data
    ) is None:
        raise CryptoPolicyError("trust input must contain exactly one public certificate")
    with tempfile.TemporaryDirectory(prefix=".trust-", dir=output.parent) as temporary:
        pem = pathlib.Path(temporary, "trust.pem")
        der = pathlib.Path(temporary, "trust.der")
        pem.write_bytes(data)
        validate_pem_bundle(pem, "capsule trust")
        subprocess.run(
            ["openssl", "x509", "-in", str(pem), "-outform", "DER", "-out", str(der)],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        # The installed policy also needs a length word and four-byte padding.
        if 4 + ((der.stat().st_size + 3) & ~3) > MAX_CERTIFICATE_BYTES:
            raise CryptoPolicyError("trust certificate exceeds protected XDR capacity")
        der.replace(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--certificate", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        package(args.certificate, args.output)
    except (CryptoPolicyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"capsule trust packaging failed: {error}\n")


if __name__ == "__main__":
    main()
