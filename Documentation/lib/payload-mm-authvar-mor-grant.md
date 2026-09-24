# Payload-MM MOR completion grant

`PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT` builds a dormant validator and
one-shot SMM authority for a fixed, pointer-free memory-clear completion
receipt. It does not enable MOR, advertise support, clear memory, add an SMI
route or provide a producer.

The receipt binds one nonzero cold-boot generation and the exact canonical MOR
Control entry captured by the read-only entry probe. `present` must be one,
reserved bits must be zero, and Control bit zero must be set; other Control
bits remain part of the binding. Exact flags assert that DMA was held before
the clear and revalidated afterwards, cache writeback fencing completed, and
zero readback completed. Nonzero generations and opaque 256-bit identities
name both the DMA policy and the reviewed memory inventory. Revision 1 only
requires these identities to be nonzero and compares them byte-for-byte; it
does not define a hash algorithm or canonical preimage. A future producer must
define those semantics or revise the receipt before claiming cryptographic
verification.

At most 15 sorted, nonoverlapping byte-granular spans describe the complete
inventory. Every span uses byte address and byte size units; unaligned physical
ranges are deliberately representable. Each has a nonzero, nonwrapping range
and is explicitly cleared or excluded. At least one byte and one span must be
cleared, so an all-excluded inventory cannot certify completion.
Cleared spans have no exclusion reason. Excluded spans use the closed reason
enumeration. Adjacent spans with identical class and reason must be merged.
The span counts and byte sums must exactly match their fixed summary fields;
all unused entries and reserved fields are zero. The fixed bound intentionally
fails closed when a platform inventory cannot be represented without losing
detail.

SMM may copy one already trusted receipt into independently proven protected
storage. The first install attempt and the first consume attempt after a
successful installation are terminal.
Installation snapshots and validates the complete receipt, verifies protected
storage, and rejects aliases or source mutation. Consumption compares the
whole expected immutable receipt, then scrubs the protected copy; mismatch
also poisons it.

Trusted SMM initialization may instead close the unused install opportunity.
Close is terminal: it succeeds only before any installation attempt, poisons
the authority, and scrubs both receipt buffers. Repeated close and every later
install fail. A close attempted after successful installation also fails
without changing or destroying the ready protected receipt. This primitive is
dormant and supplies no transport, boot hook, platform selector, or support
claim.

The receipt flags and identities are assertions, not authentication or evidence
by themselves. This slice has no cross-stage seal, trusted cold-boot generator,
DMA verifier, inventory producer or memory-clear accounting producer. Mutable
CBMEM is not a valid source after installation. Until those prerequisites are
composed through a trusted in-SMM installer, readiness and MOR support remain
false.
