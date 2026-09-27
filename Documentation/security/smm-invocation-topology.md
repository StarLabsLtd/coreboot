# Protected SMM invocation topology

`SMM_INVOCATION_TOPOLOGY` is a dormant, default-off loader primitive. It
copies the exact ordered, full-width initial APIC ID map installed in the SMM
entry stub into an aligned tail of the protected SMM module parameters. The
same device-list read supplies both copies. The loader also records the one
logical index whose APIC ID uniquely matches `initial_lapicid()`. That index
must be zero, matching the existing logical CPU and save-state ordering.

The descriptor is pointer-free POD. `READY` is release-published only after:

* the active count is in the range 1 through 64 and equals the existing SMM
  runtime count;
* every enabled CPU contributed one distinct full-width APIC ID;
* exactly one ID is the loader BSP;
* the installed stub map is byte-identical to the protected map; and
* revision, size and reserved-zero fields remain exact.

The logical index names the same node in the installed stub map, the protected
APIC map, and the existing `save_state_top[]` runtime array. The topology does
not duplicate save-state addresses or sizes.

All failure paths scrub the descriptor back to `EMPTY`. A later clean loader
attempt first scrubs stale or partially populated bytes. There is one loader
writer while APs are blocked; this primitive does not invent a multi-writer
protocol. The loader's common result path also scrubs a previously published
descriptor if a later independent SMM provisioning step fails.

This slice does not publish an LB record, CBMEM object, command, selector,
callback, address or payload endpoint. It does not create a loader-instance
descriptor, infer
cold versus resume, initialize invocation evidence or the Intel save-state
adapter, or attest a reset provider. Those facts require a later platform
composition with an honest lifecycle and SMM-linked reset source.
