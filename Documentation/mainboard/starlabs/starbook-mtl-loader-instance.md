# StarBook MTL loader-instance authority

The default-off `STARLABS_STARBOOK_MTL_LOADER_INSTANCE_SOURCE` capability
captures whether the current loader run is a non-S3 load or an S3 reload. Once
VT-d protects low memory and PCI bus mastering is quiesced, romstage samples
one 128-bit correlation nonce and release-publishes the lifecycle and nonce in
a mirrored CBMEM record. `nonce.low` must be nonzero because the legacy MOR
adapter uses it as its generation; `nonce.high` may be zero. No failure is
resampled.

The nonce proves probabilistic correlation only. It is not monotonic,
rollback-resistant or guaranteed unique. A non-S3 load includes cold boot,
warm and global reset, S4 and S5. An S3 firmware/SMM reload requires a new
`S3_RELOAD` nonce. S0ix does not reload firmware and does not republish.

Publication owns the record with atomic state transitions. Before replacing a
stale S3 or non-S3 record, the owner proves its complete range is protected and
quiesces bus mastering; later failure leaves the owned record scrubbed. The
random callback is invoked exactly once, after which bus mastering and the
protected range are checked again. Ramstage consumes the source once and
scrubs it before exposing the tuple.

The lifecycle capture is made once by the linear romstage path before FSP-M.
Its atomic claim still rejects duplicate or concurrent publication rather than
letting a second caller replace the first loader instance.

`STARLABS_STARBOOK_MTL_LOADER_INSTANCE_AUTHORITY` atomically fans that one
tuple out to an immutable, repeatable legacy generation and a one-shot generic
loader-instance seed. The aggregate must lie wholly in protected memory. It is
published only after every selected adapter has completed and the protected
range is rechecked. A failed owner scrubs and poisons the aggregate; a failed
contender cannot alter the owner. The generic consumer ends in `CONSUMED` and
cannot reprovision the authority.

Fanout provisioning returns a transient owner handle bound to the exact
aggregate, the exact owner-object address and a nonzero attempt. Commit and
abort consume that handle. An exact byte-for-byte clone at another address, a
contender without the winning handle, a stale post-commit copy, and a wrapped
attempt cannot commit, abort, or scrub another owner's state. The protected
owner address is scrubbed before terminal publication. An ambiguous owner
commit poisons and scrubs before publishing a terminal state.

The live owner object is part of the same protected authority workspace as the
fanout, never a ramstage stack token. Begin proves its complete aligned range
against the protected base, size and limit; commit, abort and poison re-prove
the sealed range and reject an outside or straddling owner.

The generic take is inseparable from a fresh post-FSP-S scan of every function
in every configured ECAM bus. That boundary clears BME, validates the complete
sorted snapshot and protected range, then scrubs its scratch space before the
one-shot take. Failure leaves the committed authority available for a safe
retry and returns a zero seed; no generic tuple crosses an unverified BME
boundary.

The MOR early-DMA adapter is optional and alone owns its early-DMA CBMEM
record. Provider-only builds neither allocate nor consume that record. When
the authority is enabled, source publication failure is fatal even on S3: a
stale record cannot be allowed to reach ramstage after freshness failed.

The default-off board selector binds this authority into the composition. It is
the final fallible SMM-loader operation. That composition snapshots protected
topology, takes the nonce seed once, publishes and rechecks the generic loader
descriptor, provisions
and rechecks invocation evidence, and unwinds evidence, instance and topology
in reverse order on failure. It adds no command route, public table or payload
ABI. ADL and GLK remain generic compile profiles and have no MTL provider.
