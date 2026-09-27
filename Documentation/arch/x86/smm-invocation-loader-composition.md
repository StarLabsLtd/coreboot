# SMM invocation loader composition

The SMM module loader composes the invocation bundle as its final fallible
operation. It snapshots the published CPU topology, takes one platform nonce,
publishes the loader instance, rechecks both sources, constructs the evidence
seed, provisions evidence, and rechecks all three objects before publishing the
composition `READY` with release ordering.

The permanent handler cannot consume provisional evidence. Coreboot completes
`smm_load_module()` before `install_permanent_handler()` permits SMM relocation;
`trigger_smm_relocation()` therefore occurs only after successful composition.
If loading fails, the MP setup path disables SMM instead of installing the
handler. No SMI, APMC, callback, or public-record route is added by this code.

Every future invocation route must first call
`smm_invocation_loader_composition_evidence()`. The acquire-ordered accessor
requires a `READY` composition and the exact evidence identity sealed into that
composition. Code must not route directly through the lower evidence API. Once
authorized, the evidence state may advance through its normal rendezvous phases;
the immutable composition remains the bundle visibility gate.

On failure, the winning composer changes `COMPOSING` to `UNWINDING`, rolls back
only evidence named by its exact loader receipt, then scrubs the loader instance
and topology before publishing `FAILED`. A stale or losing composer cannot scrub
a `READY` bundle or evidence whose terminal state no longer matches its receipt.
