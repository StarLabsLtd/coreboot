# StarBook MTL presence cold source

`STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_COLD_CLASSIFICATION` is a default-off
prerequisite for the authenticated-variable presence composition. The existing
romstage loader capture call receives the retained PMC power state immediately
after `pmc_fill_power_state()`. It runs the existing SoC classifier without
sampling cleared registers again.

The revision-2 loader source record carries that classification alongside its
existing lifecycle and one loader nonce. The protected-memory and PCI BME
boundaries, mirrored record, seal and one-shot consumption all apply to the
classification too. Ramstage installs it in the same authority workspace only
after committing the existing loader fanout. The strong presence handoff cold
predicate requires genuine cold evidence and the non-S3 lifecycle. Warm reset,
S3, S4 and unknown evidence cannot become presence cold authority merely because
the loader lifecycle says non-S3.

The option enables no publication, endpoint, SMI route or typed capsule
transport. Legacy MOR classification remains its existing non-S3 versus S3
policy; it does not acquire the narrower presence admission policy.

The complete cold composition still needs these activation dependencies:

1. Provision the handoff slot and receipt verifiers into protected SMM storage
   from the loader, carrying the same nonce and active CPU topology.
2. Supply protected ordinary-DRAM validation and the cold presence receiver
   installer using the existing checked MTL DMA receipt.
3. Supply cold route transaction claim/completion and lifecycle policy, then
   call `starbook_mtl_authvar_presence_route_composition_provision()` with those
   protected dependencies.
4. Install the private cold handoff and PREPARE/COMMIT/ABORT dispatch with
   all-CPU arm, retirement and EOS ordering before publishing the endpoint.
5. Supply ramstage publication composition, real transport callbacks and
   readiness predicates; publish both presence and lifecycle-close records only
   after successful installation.
6. Enable the typed capsule transport only after the complete broker and its
   authenticated-variable runtime authority are installed.

This prerequisite has host/configuration and board-build validation. Those
checks do not establish hardware readiness for the complete composition.
