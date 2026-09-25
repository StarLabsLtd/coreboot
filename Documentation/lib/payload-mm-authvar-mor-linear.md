# Linear MOR boot coordinator

`PAYLOAD_MM_AUTHVAR_MOR_LINEAR_ORCHESTRATOR` builds a dormant two-phase
ramstage coordinator. It is hidden and default-off. No board selects it, it
publishes no endpoint, and it does not provide a flash-write path.

The entry callback of `BS_OS_RESUME_CHECK` is the last common cold/S3 point
before coreboot branches directly to S3 resume. It is also before
`write_tables()` initializes bootmem. At that point permanent SMM has already
been loaded during CPU initialization, but the probe does not rely on it:
`payload_mm_authvar_mor_probe_entry()` maps the read-only boot device's
SMMSTORE FMAP region and parses a clean store in ramstage. A probe error is
fatal and is never treated as an absent request.

The coordinator first asks the platform to revalidate its retained cold-boot
and early-DMA classification. S3 closes the private authority and performs no
probe, reservation, clear, or media mutation. A missing Control variable or a
Control value with bit zero clear also closes the authority without registering
reservations. Only an exact present, set request may register the platform's
aligned bootmem reservations.

The exit callback of `BS_WRITE_TABLES` runs after the final bootmem map and its
conditional reservations exist. A platform callback resolves the addresses,
revalidates the early and live DMA state, composes the final inventory, and
returns a bound clear plan and executor operations. The generic executor then
clears, flushes, fences, reads back, and emits the completion grant. A typed
private completion callback must synchronously install that grant in protected
SMM, execute the durable Control transaction, and return success only after the
grant and private channel are terminal. There is no generic or public write
fallback.

Any classification, probe, reservation, binding, clear, readback, or private
completion failure is terminal. The boot hook prints the exact failing phase
and halts instead of allowing a partially cleared boot to continue. Dedicated
timestamps cover discovery, clearing, and the private completion commit.

Before inspecting caller state or invoking the first provider callback, the
coordinator atomically claims a singleton lifecycle for that exact state
address. It advances the lifecycle before every probe or callback and checks
the owner, phase, cold-boot generation, and frozen public state on return. A
concurrent call, callback re-entry, replay, or substituted state terminally
poisons the transaction. A rejected entrant never writes an owner state it may
alias; consequently the poisoned outer invocation observes the failure at its
next boundary and performs no later probe, reservation, clear, or commit.

There is deliberately no Q35 runtime provider. Q35 does not yet supply the
accepted retained early-DMA classification or the private completion service;
fabricating either in a board hook would test a different trust model. The
generic callback boundary is covered by the hostile injected-provider suite.
