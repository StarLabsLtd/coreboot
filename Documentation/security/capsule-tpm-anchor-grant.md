# Capsule TPM rollback-anchor grant

`CAPSULE_TPM_ANCHOR_GRANT` is a default-off prerequisite for splitting a
reset-safe capsule journal transaction across the pre-OS TPM owner and the SMM
media owner. It adds no platform selection, TPM command, SMI route or published
interface.

The fixed 120-byte grant contains no pointers or callbacks. It binds one boot
generation and transaction to an exact transition between two 40-byte anchor
values, the already validated NV index and its immutable policy revision. The
candidate epoch must be exactly the current epoch plus one, both digests must be
nonzero and distinct, the NV index must already be write-locked for this boot,
and all four ordering flags are mandatory. SMM snapshots
the grant and descriptor binding once into storage independently proved to be
protected, then permits only one exact consume attempt. A malformed install,
mutation during installation, mismatched consume, alias or replay fails closed;
the grant cannot be retried or redirected.

The flags are assertions made by a future trusted producer, not evidence by
themselves. The owner journal now supplies the storage-side prerequisite. It
keeps its version-1 manifest and 40-byte anchor ABI unchanged and writes a
fixed 120-byte companion after a candidate manifest. An erased companion keeps
an existing slot's legacy meaning. A valid companion binds `PREPARED` to the
exact generation, transaction, current anchor and candidate anchor. The
prepared path requires a slot large enough for both records; smaller legacy
layouts retain their existing behavior but cannot enter this path.

Preparation writes, reads back and synchronizes the candidate before writing,
synchronizing and reading back its companion. The old anchor remains
authoritative, and ordinary owner commits are refused while one exact prepared
transition exists. Reconciliation accepts exactly one prepared transition,
requires the old manifest still to exist, consumes the one-shot grant, requires
the protected anchor view to name the exact candidate, and then changes only a
one-way `PREPARED` state bit to `COMMITTED`. Recovery never exposes a prepared
candidate merely because its anchor has advanced. A torn or malformed
companion is ignored while the old anchor remains authoritative; ambiguous
commit-marker writes are accepted only after exact readback.

The default-off `CAPSULE_TPM_ANCHOR_TRANSITION` option adds the bounded pre-OS
coordinator contract for this ordering. Its revision-2 fixed 1952-byte
authorization snapshot binds the descriptor policy revision and NV index,
boot generation, transaction, exact current and candidate anchors, authority
Name, policy reference and RSA public modulus. Two fixed 624-byte records carry
distinct approved policies, cpHashes, caller nonces and RSASSA signatures for
the complete replacement `NV_Write` and the following `NV_WriteLock`. Only
RSA-2048, RSA-3072 and RSA-4096 material is admitted; unused tails and reserved
fields must be zero. The structure contains no private key, password, address
or callable object.

The coordinator is atomic and one-shot. It snapshots every caller-owned input,
requires a synchronous provider to prove the exact durable `PREPARED` record,
and permits TPM traffic only through an acquired pre-OS lifecycle token. It
checks that the provider did not alter its proof argument, discards that
argument, and rebuilds the final grant from the immutable authorization
snapshot. Every read returns both the exact anchor and freshly observed public
lock state. Current plus unlocked permits one authorized replacement-write
attempt. Candidate plus unlocked, including reset recovery after an ambiguous
write, skips the write and permits one separately authorized lock attempt.
Candidate plus locked is complete recovery and replays neither operation.
Current plus locked and every other state fail closed. Operation status is
advisory: authoritative readback must prove candidate plus locked before a
grant can be produced.

The lifecycle is ended, quiesced and released before the exact grant and
binding are handed to the provider for one protected SMM installation. Invalid
input, failed durable proof, mutation, stale ownership, an unexpected anchor,
failed readback or replay produces no grant. The coordinator is excluded from
SMM and neither SMM nor the grant consumer can access the TPM.

The coordinator includes a default-off, read-only proof component for the
first item. A platform gives it exactly two already bounded `region_device`
children for the validated owner-journal layout, the layout itself, a SHA-256
provider, and an immutable current-anchor snapshot obtained by the pre-OS TPM
owner. It hashes the layout rather than trusting a supplied storage-domain
value, scans every bounded slot, and requires the exact current manifest,
candidate manifest and one `PREPARED` companion named by the coordinator's
grant argument. Current and candidate must retain one identity binding. The
generation, transaction, current and candidate tuple must match exactly.
Every non-erased malformed companion, ambiguous candidate, stale prepared
tuple, short read or changed sealed input fails closed. The exact current and
candidate slots are read back before the one proof attempt is consumed.

The version-1 journal format has no CRC field. Manifest integrity is the
anchor's SHA-256 digest; companion integrity is its exact fixed tuple, one-way
state encoding and durable readback. The reader preserves this ABI rather than
inventing an incompatible checksum interpretation for the reserved field.

The default-off `CAPSULE_TPM_ANCHOR_POLICY_PROVIDER` supplies the TPM half of
that contract. It derives the provisioned index policy and exact NV Name,
recomputes the complete replacement-write or write-lock cpHash, and rejects a
different value before opening a policy session. Each operation executes
`PolicyNvWritten(true)`, exact-current `PolicyNV`, the recomputed
`PolicyCpHash`, external RSA key loading and signature verification,
`PolicyAuthorize`, the operation-specific `PolicyCommandCode`, the fixed
two-branch `PolicyOR`, and finally exactly one NV command. Write and lock use
separate approved policies, cpHashes, nonces and signatures. Readback verifies
the complete public area admitted by the fixed policy and returns its observed
write-lock state and value. Provisioning must set `AUTHREAD` with an empty
`authValue`; mutation remains policy-authorized only. A well-formed TPM error
delivered for the terminal command remains advisory only when both
transient-object and policy-session cleanup succeeded, so the coordinator can
resolve command-level ambiguity by exact readback. A malformed reply or
transport loss fails the pre-OS lifecycle and is not claimed recoverable in
this boot.

This is still not a production composition. No platform selects the provider,
supplies authorization material or durable-PREPARED callbacks, installs the
grant into protected SMM storage, or publishes an SMI endpoint. Factory
provisioning and authority-key management remain external. The provider uses
the existing pre-OS lifecycle transport and makes no claim that a platform has
protected its authorization input or later grant route. Enabling it requires a
ramstage stack of at least 12 KiB for the coordinator, immutable snapshots,
policy command encoders and platform TPM transport.

Advancing the TPM first would create a power-loss interval in which the only
authoritative anchor names a manifest that does not exist. A production split
transaction must instead use this ordering:

1. SMM writes and reads back the candidate manifest, records `PREPARED`, and
   leaves the current TPM anchor unchanged.
2. After reset, the pre-OS owner validates the provisioned index and reads the
   exact current anchor. It independently verifies that the candidate manifest
   is durable and matches the proposed candidate anchor.
3. The pre-OS owner obtains signed authorization bound to that exact transition,
   advances the NV value and reads it back. An ambiguous TPM result is resolved
   only by an authoritative read: current means retry is possible, candidate
   means the advance completed, and every other value fails closed.
4. It relinquishes TPM locality and transport, then hands the sealed grant into
   protected SMM storage before any payload, option ROM or other external code.
5. SMM consumes the grant only to reconcile the already durable candidate. It
   never calls the TPM.

Power loss before the NV advance leaves the old manifest authoritative. Power
loss during the command is resolved by the next pre-OS read. Power loss after
the advance is safe because the matching candidate was durable before the TPM
changed. A grant is volatile and never substitutes for either durable state.

The legacy journal commit continues to perform its abstract anchor advance in
one SMM call. The new prepared/reconcile entry points are compiled only with
this default-off option and are not selected by a platform. Factory
provisioning remains a separate operation. No TPM call, SMI, table publication,
platform route or raw-flash access is added.

Host coverage for the reader is in
`tests/lib/payload_mm_fmp_owner_prepared_reader_test.sh`. It runs O0, O2,
strict-warning, ASan and UBSan builds over exact success, one-shot replay,
torn companions, tuple replay, stale anchors, duplicate preparation, short
reads, media mutation, split identity binding and committed-only media.
Provider coverage is in `tests/lib/capsule_tpm_anchor_policy_test.sh` and
`tests/lib/capsule_tpm_anchor_policy_transcript_test.sh`. Both run O0, O2,
ASan and UBSan builds. The first covers fixed tails, exact public-state
failures, readback recovery, one-shot replay, input and callback mutation,
attempt sealing and reentry. The second links the production TPM encoders and
parsers and checks the exact ordered read, write and lock command transcripts
with real RSA test material. It distinguishes a delivered terminal TPM error
from malformed replies, transport loss and cleanup failure.
