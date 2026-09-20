# Capsule TPM anchor artifact generator

`capsule-tpm-provision` is an offline, deterministic artifact generator and
validator for the dormant capsule rollback-anchor contract. It never opens a
TPM, provisions an index, chooses a board or index, signs a digest, or accepts
authorization secrets. Normal coreboot execution does not invoke this tool.

The operator must explicitly provide:

- the full TPM NV handle (`0x01xxxxxx`);
- a nonzero initial epoch and the exact 32-byte capsule-manifest digest;
- an RSA-2048, RSA-3072 or RSA-4096 signing modulus; and
- a nonempty policy reference of at most 32 bytes.

The public exponent is canonically encoded as zero, meaning 65537. The key is
an unrestricted RSASSA-SHA256 signing key with a SHA-256 Name. No private key,
signature, password or owner authorization belongs on this command line.

Build and run the tool explicitly:

```
make -C util/capsule_tpm_provision
util/capsule_tpm_provision/capsule-tpm-provision generate \
  --index 0x01xxxxxx --epoch EPOCH \
  --manifest-sha256 HEX --rsa-modulus HEX --policy-ref HEX \
  --output NEW_EMPTY_DIRECTORY
```

`validate` takes the same inputs and recomputes every byte in an existing
artifact directory. It rejects missing, non-regular, symlinked, truncated,
extended or mismatching artifacts and any unknown directory entry. The bundle
must contain exactly the 17 named artifacts. `generate` creates a new
directory and never overwrites an existing path. A failed generation may leave
a partial directory; inspect it if useful, then choose a new empty output path.

## Exact policy

All TPM integers and command codes below use their canonical big-endian wire
encoding. `H` is SHA-256, `Z` is a 32-byte zero digest, and concatenation has
no implicit size fields. `Name(K)` is `0x000b || H(TPMT_PUBLIC(K))`.
These are the policy-update equations from the TPM 2.0 Library Part 3 command
definitions, not a tool-specific policy encoding.

The two static branches are:

```
A  = H(H(Z || TPM_CC_PolicyAuthorize || Name(K)) || policyRef)
W  = H(A || TPM_CC_PolicyCommandCode || TPM_CC_NV_Write)
L  = H(A || TPM_CC_PolicyCommandCode || TPM_CC_NV_WriteLock)
P  = H(Z || TPM_CC_PolicyOR || min(W,L) || max(W,L))
```

`P` is the NV index `authPolicy`. Lexicographic sorting makes the exact
`PolicyOR` digest list deterministic. The index public area has SHA-256 Name
algorithm, a 40-byte data size, and exactly `POLICYWRITE | WRITEALL |
WRITE_STCLEAR | AUTHREAD | NO_DA | PLATFORMCREATE` static attributes.

The initial anchor is little-endian `uint64_t epoch || manifest SHA-256`.
The generator computes the exact cpHash for an offset-zero, complete
`NV_Write` against the unwritten NV Name. Its signed pre-authorize transcript
is:

```
H(H(Z || TPM_CC_PolicyNvWritten || false) ||
  TPM_CC_PolicyCpHash || initial_write_cpHash)
```

After that write, the TPM-maintained `WRITTEN` bit changes the index Name. The
separate initial-lock cpHash uses that written Name. Its transcript substitutes
`PolicyNvWritten(true)` and the exact `NV_WriteLock` cpHash. The corresponding
`*-authorization-hash.bin` is `H(approvedPolicy || policyRef)`, the digest an
offline authority signs for `VerifySignature` and `PolicyAuthorize`.

## Deliberate provisioning-only boundary

This utility emits only initial define/write/lock review artifacts. It does
not generate authorization for a later update. The static policy is designed
to support a future ratcheting updater, but that updater must bind all of the
following before `PolicyAuthorize`:

1. `PolicyNvWritten(true)`;
2. `PolicyNV` equality against the exact current 40-byte anchor; and
3. `PolicyCpHash` for the exact replacement anchor's complete `NV_Write`.

It must create a distinct authorization for `NV_WriteLock`. An authorization
path that omits the old-anchor equality, permits a partial or arbitrary write,
or reuses a write digest for locking is outside this contract.

## Operator boundary

An external, physically authorized factory procedure must review
`descriptor.bin`, `rsa-public.bin`, `authority-name.bin`, `nv-public.bin`, and
the policy digests before using platform-hierarchy authorization to define the
exact index. It must obtain separate offline signatures for the initial write
and lock authorization hashes, execute the matching policy transcripts, write
the exact `anchor.bin`, and lock the index. Finally it must read back the
public area and data and require exact agreement, including `WRITTEN` and the
expected lock phase. Because the required public attributes include
`PLATFORMCREATE`, the index must be defined using explicit platform-hierarchy
authorization; owner-hierarchy definition is not compatible with these
artifacts. This repository intentionally supplies no automatic TPM executor,
private-key handling, default index, recovery bypass or runtime SMI endpoint.

Run the host tests with:

```
make -C util/capsule_tpm_provision test
```
