# Authenticated-variable presence lifecycle-close owner

`PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_OWNER` builds a dormant SMM-only
owner for closing the authenticated-variable presence authority. It is
default-off and no platform selects it. It installs no sender, command route,
table record, board policy or live lifecycle callsite.

Five exact source-owned operations represent pre-external-image, payload
failure or return, warm reset and S3 resume. They share one private helper;
there is no public source selector. A future provider must bind each operation
to its exact boundary. In particular, pre-external-image requires the exact
externally launched artifact and provenance. Internal firmware-driver image
events do not satisfy it.

The fifth operation is a late CLOSED reproof for CDK2 enter-runtime/EBS. It has
its own one-shot slot and source domain and is rejected unless this generation
has already completed pre-external-image closure. It therefore cannot conceal a
missing external-image boundary.

Provisioning requires and seals the canonical committed presence transaction
slot for the exact generation. That fact is publication evidence only; it does
not authorize a route. Provisioning also seals the expected initiating CPU and
active CPU count, claim and completion callbacks, and copied callback context
in protected SMM storage. The claim callback must
atomically transfer a fresh live protected invocation. That evidence provides
freshness and completion correlation, not authority to close a different
generation. The future adapter owns evidence publication, participant
departure and EOS consumption; this owner neither recreates those mechanisms
nor assumes that the existing terminal presence route can service it.

After a successful claim, every failure or ambiguity invokes the fixed
non-returning platform-wide SMM invocation fail-stop. The owner calls
`payload_mm_authvar_presence_authority_restrict()` with its sealed generation,
revalidates all protected owner facts, and calls it again. `CB_SUCCESS` from
that second exact-generation call is the synchronous CLOSED proof. Only then
does the owner complete the same invocation with a nonzero source-domain-
separated value. A completion error is terminal because closure and invocation
state can no longer be safely inferred by the caller.

Successful closure proves only that the protected authority is CLOSED for the
exact generation carried by this owner. It does not prove that any lifecycle
hook was installed, does not consume lifecycle installation evidence, cannot
serve as the producer's `lifecycle_sealed` callback, and never establishes
`LB_AUTHVAR_PRESENCE_LIFECYCLE_SEALED`. A later composition must supply and
review the four real boundary adapters before making that separate claim.
