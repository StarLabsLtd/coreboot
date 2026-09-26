# Authenticated bootmem reservation receipt

`BOOTMEM_ALIGNED_RESERVATION_RECEIPT` builds a dormant, one-shot boundary
between the final ramstage bootmem map and a later protected consumer. It does
not publish a coreboot table, CBMEM record, SMI command, or operating-system
interface.

Before bootmem initialization, trusted platform composition registers an
aligned reservation and retains its opaque handle. While loading the permanent
SMM module, it generates a fresh 256-bit secret and provisions equal signer and
verifier authorities with that handle and the trusted cold-boot generation.
Provisioning consumes and wipes the source secret on success or failure. The
verifier is published before the signer, so reentry cannot consume a signer
whose verifier is not ready. A close racing publication requests an abort; the
publisher that owns the body performs the terminal wipe without exposing an
empty state or racing a bulk copy of the atomic state byte.
Provisioning rejects every boot kind except an explicit cold boot, including
S3 resume.
The verifier destination must already be protected; the receipt code does not
infer that property from a caller-supplied address.

After `process_aligned_reservations()` commits the final map,
`bootmem_aligned_reservation_receipt_emit()` is the table-only signing route. It
looks up bootmem's private registration state and checks the original request,
opaque handle, final base, size, alignment, limit, `BM_MEM_TABLE` tag, and both
the firmware and OS views. It then authenticates a sequence-one
`ACTIVE_FIRMWARE` receipt with the fixed HMAC-SHA-256 implementation. The
signer is terminally wiped whether signing succeeds or fails.

The exact-tag variants additionally admit `BM_MEM_RESERVED`, for protected
firmware communication pages which must not become payload RAM. The caller
must demand one exact tag; no other tag is accepted. Emission checks that same
tag in the original request, resolved result, firmware map and OS map. The
legacy emit and verify wrappers remain strictly `BM_MEM_TABLE`-only.

The protected consumer verifies the HMAC and every bound field, consumes the
receipt exactly once, and wipes both the input receipt and verifier authority
on every well-formed call. An unused authority must be closed before payload
handoff.

This primitive deliberately supplies no memory-overwrite transport. That
belongs to the platform composition that owns permanent SMM loading and the
private self-SMI lifecycle.
