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

There is still no producer because the tree does not yet have:

* a signed `PolicyAuthorize` input and platform provider bound to the capsule
  authority and exact NV write.

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
