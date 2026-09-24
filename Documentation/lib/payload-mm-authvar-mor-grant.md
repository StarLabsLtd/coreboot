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

An SMM-only take operation avoids retaining a second expected receipt after
the private seal transport has been scrubbed. It accepts only aligned,
initially-zero output independently attested as protected and disjoint from the
authority, callback, and bounded immutable callback context. It rechecks the
complete authority, output, callback context, and one-shot state before
publishing, then terminally consumes and scrubs the internal receipt. Failure
never publishes receipt bytes and poisons a ready grant take opportunity.

Before take, protected SMM code may instead discard either a ready grant or a
still-unused install slot without supplying an external receipt copy. Discard
attests its callback, bounded immutable context, and the complete private
authority; malformed, aliased, mutated, or reentrant attempts fail closed.
Every terminal path scrubs both private receipt buffers. A completed discard
is idempotent, while a poisoned or already-consumed authority remains an
error. Discard performs no variable-media operation and publishes no endpoint.

This take operation is only a prerequisite for a future protected MOR
consumer. That consumer must acquire the authvar executor's exclusive media
lease, recover and scan the current store, verify the exact canonical Control
record, consume the grant immediately before the first mutation, and perform
durable commit, sync, readback, rescan, and lease release. Consuming outside
that lease and then issuing an ordinary SET would race a newer request; writing
before consuming could clear Control without proof of completed memory erase.
Neither composition is supplied here.

Trusted SMM initialization may instead close the unused install opportunity.
Close is terminal: it succeeds only before any installation attempt, poisons
the authority, and scrubs both receipt buffers. Repeated close and every later
install fail. A close attempted after successful installation also fails
without changing or destroying the ready protected receipt. This primitive is
dormant and supplies no transport, boot hook, platform selector, or support
claim.

## Private completion seal adapter

`PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_SEAL` adds a dormant, fixed-size adapter
between a trusted ramstage producer and the SMM grant authority. It is not an
APM or SMMSTORE command and installs no dispatcher or boot-state hook. A future
platform composition must privately provision the same 64-byte channel
descriptor to both stages and supply its own authenticated trigger.

The channel fixes the only accepted 552-byte transport range, a boot-local
256-bit capability, and the independently observed caller and caller-context
values expected by SMM. Those observations are inputs from the future platform
handler; they never come from the request. SMM accepts one channel provisioning
attempt and one request attempt. An exact authenticated install delegates to
the protected grant authority, while an exact close consumes the unused slot.
Every malformed, unknown, incorrectly ranged, or incorrectly authenticated
first request terminally closes the grant opportunity. The shared transport
and protected candidate are scrubbed on every terminal request path. SMM uses
the pre-callback fixed transport address for scrubbing, rechecks the retained
channel and callbacks inside the grant protection callback before allowing an
install to commit, then erases the retained capability, channel, callbacks, and
candidate. A callback mutation therefore cannot redirect cleanup or leave a
ready grant behind an adapter failure.

The ramstage sender publishes only a canonical install or close message into
the fixed range, calls the platform-private trigger, requires SMM to have
scrubbed the range, and then scrubs both the range and its mutable channel copy.
This slice deliberately supplies no capability generator or distributor,
platform selector, SMI handler, OS endpoint, MOR support claim, or lifecycle
call site. Those composition steps must ensure the no-request path invokes
close before handing control to less-trusted software.

The receipt flags and identities are assertions, not authentication or evidence
by themselves. This slice has no cross-stage seal, trusted cold-boot generator,
DMA verifier, inventory producer or memory-clear accounting producer. Mutable
CBMEM is not a valid source after installation. Until those prerequisites are
composed through a trusted in-SMM installer, readiness and MOR support remain
false.
