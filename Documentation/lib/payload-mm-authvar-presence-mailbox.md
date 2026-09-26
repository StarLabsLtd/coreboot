# Payload-MM authenticated-variable presence mailbox lifetime

The public authenticated-variable presence endpoint still describes the same
80-byte request and response at offset zero. Its private backing is one aligned
4 KiB `BM_MEM_RESERVED` page. This dormant slice does not change the public
coreboot-table ABI, select a platform, install a route, or claim production
support.

The handoff receiver authenticates the exact page receipt and publishes a
one-use protected backing witness. Authority installation must consume that
exact witness before it can dereference or clean the page. A complete-page DMA
proof and all-CPU rendezvous proof run before installation, after the provision
callback, and before every later cleanup. An inability to prove safe cleanup
uses the platform's nonreturning fail-stop path.

Cleanup ownership is linear: reservation, resolved producer, then protected
authority. Private transaction acknowledgements distinguish transferred
ownership from an abort that has already cleaned the page. The current owner
performs a full-page scrub before publishing its terminal state. Dispatch and
lifecycle restriction coordinate through atomic requested/owner states, so a
restriction cannot publish completion while the executor still uses the page
and cannot lose a close when dispatch moves to its attempted state.

A response-producing dispatch deliberately leaves the page in `ATTEMPTED` so
the initiating consumer can read the 80-byte completion. A later exact
lifecycle restriction scrubs all 4 KiB. Malformed requests and other paths that
publish no response scrub immediately. Reset paths scrub before invoking the
reset callback. This slice does not yet define an explicit consumer
acknowledgement; platform composition must therefore arrange the later
lifecycle close and permanent CPU/DMA revocation before production selection.
