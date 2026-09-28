# Authenticated-variable presence route session

`PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION` is a dormant SMM-only mechanism.
It composes one protected loader lifecycle, the presence publication arm, the
generic presence transaction, invocation evidence, topology, and the reserved
`0xff` APMC owner. It installs no platform provider or handler callsite. The
separate `SMM_APMC_ROUTE_AUTHVAR_PRESENCE` capability is also default-off and
unselected, so building the mechanism alone leaves `0xff` reserved-disabled.

Provisioning accepts only one clean session and a minimal authority policy of
PREPARE, COMMIT, ABORT, DMA-protection, and fail-stop callbacks. It seals exact
loader, topology, binding, save-state, callback, context, slot, arm, and page
facts before composing the arm and generic receiver. The session is published
ready immediately before that lower publication; after the generic receiver
is published, no fallible route-readiness step remains. While the route is
still provisioning it binds a one-shot arm proof delegation. The arm's
original sealed proof authenticates that delegation, and every later arm or
receiver proof passes through both the arm guard and the route guard. The
route guard snapshots the session and the current save-state context around
each proof, so a successful lower proof cannot alter either authority.

Save-state adapter context is external and intentionally mutable during an
invocation. Provisioning nevertheless bounds it to 1024 bytes, snapshots it
across every mutation-capable protection proof, and then discards that
temporary snapshot before publication. Current 32-bit Intel adapters are
compile-time checked against this generic bound.

Invocation admission treats a second valid owner collision as bounded
contention, not corruption. Both arrival and rendezvous-ack admission return
`TRY_RETRY` after their one local resample when another exact owner completes
in the same interval; the entry policy's existing poll bound retains terminal
fail-stop authority. Generation publication remains atomic under the OPENING
owner so a losing arrival cannot sample a partially published generation.

Every CPU first enters the invocation rendezvous. The handler-lock owner keeps
the exact BSP ticket and an earlier registry selection receipt. After all
returnable route checks and the session ownership transition, exact registry
consume is the last fallible registry action. The literal next statement is a
direct call to the generic presence transaction dispatcher. The route never
calls registry `finish()` and never interprets an owner return as permission
to fall through.

The composed claim callback binds the exact `0xff` command, loader nonce and
lifecycle, topology, BSP ticket generation, sentinel logical value, and
save-state node. Its completion callback publishes the exact PREPARE, COMMIT,
or ABORT logical result and
requests evidence close through the strong one-shot evidence primitive. The
generic receiver then publishes the exact page acknowledgement. A receiver-
owned validator checks the canonical receiver end state and stable clean page
before the route exposes its pre-unlock departure marker.

After the platform releases its handler lock, every admitted CPU departs. The
BSP bounded-waits for evidence `READY`, consumes logical EOS exactly once,
then performs only infallible session scrub and the final release-store. A
successful PREPARE admits one strictly newer decision invocation; COMMIT and
ABORT permanently close and scrub the session. The returned BSP-EOS marker is
for a future live callsite whose literal next action must set hardware EOS.
No such callsite is installed in this slice.

The production-SMM Q35 and Meteor Lake profiles build with their selected
32-bit coreboot compiler flags and measure the resolved owned stack depth
against a 12 KiB limit. The large one-use validation and delegate-binding
images are confined to a no-inline phase that returns before lower transaction
publication, so both profiles currently resolve to 11,256 bytes and retain
more than 5 KiB of the 16 KiB SMM stack. They explicitly stop at the dormant
authority, protection, platform fail-stop, and save-state callback boundaries;
the future live platform composition must bind those concrete targets and
prove the complete path within the 16 KiB SMM stack before enabling the route.
