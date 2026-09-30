#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Host-side checks matching payload-mm's bounded RSA certificate policy."""

import pathlib
import re
import subprocess
import tempfile


MAX_CERTIFICATES = 8
MAX_CERTIFICATE_SIZE = 64 * 1024
MAX_CERTIFICATE_BYTES = 256 * 1024
MIN_RSA_BITS = 2048
MAX_RSA_BITS = 8192

_CERTIFICATE_PATTERN = re.compile(
    br"-----BEGIN CERTIFICATE-----\s+.*?-----END CERTIFICATE-----",
    re.DOTALL,
)
_RSA_BITS_PATTERN = re.compile(r"Public-Key: \(([0-9]+) bit\)")


class CryptoPolicyError(ValueError):
    """The certificate or CMS object cannot be accepted by payload-mm."""


def _pem_certificates(data, description):
    certificates = _CERTIFICATE_PATTERN.findall(data)
    if not certificates:
        raise CryptoPolicyError(f"{description} contains no certificate")
    return certificates


def _validate_certificates(certificates, description):
    if len(certificates) > MAX_CERTIFICATES:
        raise CryptoPolicyError(
            f"{description} contains more than {MAX_CERTIFICATES} certificates"
        )
    total = 0
    with tempfile.TemporaryDirectory(prefix="coreboot-certificate-") as temporary:
        for index, certificate in enumerate(certificates):
            pem = pathlib.Path(temporary, f"certificate-{index}.pem")
            der = pathlib.Path(temporary, f"certificate-{index}.der")
            public_key = pathlib.Path(temporary, f"public-key-{index}.pem")
            pem.write_bytes(certificate + b"\n")
            subprocess.run(
                ["openssl", "x509", "-in", str(pem), "-outform", "DER",
                 "-out", str(der)], check=True, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL)
            size = der.stat().st_size
            total += size
            if size == 0 or size > MAX_CERTIFICATE_SIZE or total > MAX_CERTIFICATE_BYTES:
                raise CryptoPolicyError(
                    f"{description} exceeds payload-mm certificate bounds"
                )
            with public_key.open("wb") as output:
                subprocess.run(
                    ["openssl", "x509", "-in", str(pem), "-pubkey", "-noout"],
                    check=True, stdout=output, stderr=subprocess.DEVNULL)
            result = subprocess.run(
                ["openssl", "rsa", "-pubin", "-in", str(public_key),
                 "-text", "-noout"], check=False, capture_output=True, text=True)
            match = _RSA_BITS_PATTERN.search(result.stdout)
            if result.returncode != 0 or match is None:
                raise CryptoPolicyError(
                    f"{description} certificate {index + 1} is not RSA"
                )
            bits = int(match.group(1))
            if not MIN_RSA_BITS <= bits <= MAX_RSA_BITS:
                raise CryptoPolicyError(
                    f"{description} certificate {index + 1} has unsupported "
                    f"RSA size {bits}"
                )


def validate_pem_bundle(path, description):
    """Validate every certificate in a PEM file against runtime bounds."""
    data = pathlib.Path(path).read_bytes()
    _validate_certificates(_pem_certificates(data, description), description)


def validate_cms_certificates(cms):
    """Validate the exact certificate set embedded in a DER PKCS#7 object."""
    with tempfile.TemporaryDirectory(prefix="coreboot-cms-policy-") as temporary:
        signed_data = pathlib.Path(temporary, "signed-data.der")
        certificates = pathlib.Path(temporary, "certificates.pem")
        signed_data.write_bytes(cms)
        subprocess.run(
            ["openssl", "pkcs7", "-inform", "DER", "-in", str(signed_data),
             "-print_certs", "-out", str(certificates)], check=True,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        _validate_certificates(
            _pem_certificates(certificates.read_bytes(), "CMS"), "CMS")
