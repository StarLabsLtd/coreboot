# Authenticated-variable presence tuple sender

`PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER` is a hidden, default-off i386
ramstage adapter between the existing presence producer callbacks and the
private SMM invocation tuple. It owns no lifecycle or authority state. Its
immutable descriptor names only the dedicated transaction page; the producer
already serializes `PREPARE` followed by exactly one `COMMIT` or `ABORT`, and
the protected receiver owns receipt consumption and authority transitions.

For each accepted callback the sender scrubs the complete page, publishes one
canonical request, and issues exactly one tuple trigger. It snapshots and
rechecks the response fields and scans the complete reserved area twice before
scrubbing the shared page. Success requires a zeroed request and reserved area,
an exact acknowledgement for the requested decision, and the matching returned
logical value. The callback never retries, times out, interprets an unchanged
sentinel as success, or synthesizes an acknowledgement.

An exact receiver-generated `ABORT` response to `PREPARE` is preserved for the
producer and returned as an error. This proves that protected authority already
cleaned the backing and prevents a redundant abort. After structurally valid,
non-aliasing preflight, every other malformed prepare response leaves a zero
acknowledgement and the canonical sentinel result so the producer issues its one
abort. Invalid output objects or any alias rejection leave outputs untouched.
Ambiguous commit or abort results reach the producer's existing platform
fail-stop path.

The transaction page must be aligned, disjoint from every callback object, and
disjoint from the seed's communication and backing ranges. Inputs and the
descriptor are compared before and after the synchronous trigger. A rejected
pre-trigger call neither accesses the page nor issues an SMI; every accepted
page is fully scrubbed before return.

This slice adds no reservation signer, lifecycle provider or arbiter, physical
presence gesture, SMM handler, command selector, public mailbox or coreboot
table record, platform selection, or production callsite.
