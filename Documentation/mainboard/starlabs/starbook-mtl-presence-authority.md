# StarBook MTL protected presence authority

The canonical transaction installs the existing one-shot presence authority;
there is no second seed transport or mailbox ownership ledger. The checked
loader bootstrap supplies the binding only after authenticating the reserved
mailbox backing and its actual cold-loader identity. The authority consumes
that same existing backing evidence during PREPARE.

The board callback provider uses the existing protected DMA receipt owner and
its live VT-d requester/translation verifier, not the S3-only epoch. Cold range
checks require the exact NON_S3 loader nonce, a later fully claimed presence
invocation, every active CPU, and a page outside the receipt's DMA-visible
ranges. The receipt workspace is serialized and both the complete invocation
token and loader/topology identities are checked again after live hardware
verification. Failure poisons the receipt owner. The CPU evidence reconstructs the token
from the retained loader identity, all participants and their acknowledgements;
the provider requires the exact CPU count and initiating BSP from the binding.
Neither a configuration assertion nor an injected payload proof substitutes
for these facts.

PREPARE retains only a transient capability and its seal, then scrubs both
after the authority installs its private copy. An installed authority cannot
serve an operator while the provider remains PREPARED. COMMIT opens that gate
only under a fresh claimed invocation. ABORT admits only the exact generation,
uses a controlled cleanup phase, and asks the existing authority to scrub and
close its mailbox. A failed installation or ambiguous restriction does not
produce a successful CLEANED acknowledgement: the transaction owner's existing
terminal failure path must keep the invocation held and reset the platform.

The cold-reset callback performs the existing prepared CF9 full reset. If that
returns, the existing platform-wide failure reset and watchdog path is used;
the provider never returns to the payload after requesting a cold reset.

## Validation and activation limits

The callback state tests run at O0/O2 and with ASan/UBSan. They cover generation,
binding/phase corruption, aliases, alignment, missing bootstrap, exact CPU count,
capability scrubbing, PREPARE/COMMIT/ABORT ordering and failed installation.
Their runtime, DMA and authority boundaries are mocks, not platform validation.
The claimed-snapshot accessor is also exercised by the existing full invocation
evidence suite, including its hostile mutation and concurrency fixtures.
The cold receipt fixtures check missing claims, S3/nonce mismatches, partial CPU
participation, generation backsteps, denied live translations, claim drift and
changed reservation ownership. They mock hardware boundaries; they do not
establish an installed cold hardware route.

This provider is built only when the existing board route owner, canonical tuple
sender, runtime binding, DMA receipt provider and protected presence authority
are all selected. That closure is not itself activation evidence. The actual
bootstrap receipt import, live DMA range composition, public operator dispatcher,
and final composed image still require integrated tests. General authenticated
variable runtime services and their endpoint publication are separate open gates.
