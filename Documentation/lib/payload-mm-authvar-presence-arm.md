# Authenticated-variable presence publication arm

`PAYLOAD_MM_AUTHVAR_PRESENCE_ARM` is a dormant SMM-only composition
prerequisite. It does not install an APM route, select a board, consume a
runtime command, or dispatch the presence authority.

The loader provisions one protected arm from the exact invocation loader
composition, loader instance, and evidence object. Provisioning proves that
the objects and callbacks are protected and non-overlapping, records the
loader nonce and lifecycle, and permits only one non-wrapping ownership
attempt. There is no reset or rearm API.

One route-specific operation seals the complete transaction policy and
binding, atomically claims the reservation verifier, creates the private
wrappers, releases `BOUND`, and immediately calls the generic transaction
provisioner with that same protected binding. A private protection wrapper
exactly checks the complete arm image around every proof callback made by the
generic provisioner. No wrapped callback capability is returned. Generic
provisioning failure poisons and scrubs the arm; success returns without
another validation or state write. The binding must name the loader-sealed
active CPU count and BSP.

PREPARE accepts only a fully valid endpoint and backing bound to the exact
transaction generation. Its transport is fixed to an 8-bit write of `0xff`
to APM control port `0xb2`. The audit distinguishes rejection before entering
the original PREPARE from a call which entered it, so ABORT calls the original
authority at most once and only when ownership may have transferred.
PREPARE and its following COMMIT or ABORT each require a separate, exact
invocation claim and exact logical-value completion. The decision invocation generation
must be newer than the completed PREPARE generation.

COMMIT calls the original authority first. Any error or changed protected
fact after that point is ambiguous and invokes the platform fail-stop. After
successful revalidation, the adapter records COMMITTED but remains `BOUND`.
The following exact COMMIT completion invokes the original completion once,
scrubs its ephemeral invocation, and releases `BOUND -> READY` as the literal
final adapter write. The generic receiver publishes its acknowledgement next.

ABORT similarly becomes terminal only after the exact ABORT completion. It
then scrubs the seed, full binding and capabilities, all authority callbacks,
and their contexts before releasing `ABORTED`. Only the immutable canonical
arm identity, protection proof, and minimal fail-stop callback closure remain,
so later generic receiver corruption can still reset the platform.

The existing producer publication receipt remains the sole public closure.
The arm holds the same full endpoint and transaction binding, including the
transaction nonce, that the producer's PREPARE and COMMIT callbacks used.
Consequently readiness is transitive through that receipt; this adapter adds
no second receipt and exposes no readiness query. Runtime route consumption,
invocation-session ownership, participant departure, and BSP EOS closure are
deliberately outside this prerequisite.
