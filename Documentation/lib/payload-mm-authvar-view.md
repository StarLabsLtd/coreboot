# Synthetic authenticated-variable view

`PAYLOAD_MM_AUTHVAR_VOLATILE_VIEW` builds a pure immutable read view over one
successfully scanned persistent authenticated-variable store. It performs no
media access, installs no executor or provider, and publishes no service or
endpoint.

The view presents the five volatile variables created by EDK2 26.09 before the
persistent store, in this exact order:

1. `SetupMode`, global-variable GUID, BS+RT, one byte;
2. `SignatureSupport`, global-variable GUID, BS+RT, the SHA-1, SHA-256,
   SHA-384, SHA-512, RSA-2048 and X.509 certificate-type GUIDs in that order;
3. `SecureBoot`, global-variable GUID, BS+RT, one byte;
4. `certdbv`, certificate-database GUID, BS+RT+time-authenticated, LE32(4);
5. `VendorKeys`, global-variable GUID, BS+RT, one byte.

The three scalar values come only from the caller's already-authoritative
packed mode projection. The view never reads `PK`, `SecureBootEnable` or
`VendorKeysNv` to derive them. Before runtime the executor must supply its
validated boot projection. At runtime it must supply the sealed projection, so
deleting `PK` can expose `SetupMode=1` while the boot-time `SecureBoot=1` value
remains frozen. All combinations of the three known bits are representable;
the coordinator and executor, rather than this read model, own lifecycle
validity.

GET and NEXT return immutable direct spans. Synthetic spans have static
lifetime and persistent spans borrow the immutable store/index lifetime. No
synthetic `payload_mm_authvar_store_entry` is manufactured. NEXT emits the five
synthetic variables first, then delegates to the existing persistent NEXT
helper, preserving its physical order, transition-record rule and runtime
visibility. QUERY delegates unchanged to the persistent semantic helper;
synthetic variables consume no persistent quota.

Initialization rejects an invalid index, unknown mode bits, output/input
aliasing, and any live persistent winner with one of the five synthetic
identities. Deleted history is not a collision. The same identity matcher is
used by bundle SET policy, while its other reserved variables remain unchanged.
This prevents a persistent write from shadowing the synthetic read model.

The host proof uses literal EDK2-derived names, GUID bytes, attributes and
payloads rather than production tables. O0/O2 ASan/UBSan tests cover all eight
mode projections, every GET/NEXT boundary, persistent delegation and runtime
filtering, collisions and deleted history, QUERY equivalence, hostile aliases
and atomic output. Directed compile-success mutants alter identities, order,
attributes, values, mode bits, collision handling, persistent ordering, QUERY
delegation and SET reservation.

With the dormant coordinator selected, the executor constructs this view only
after same-lease FTW recovery and mode derivation or reconciliation. The first
boot read that completes those stages seals the validated projection before
ending the media lease; external bytes and metadata are still published only
after a successful end. Runtime construction requires the sealed projection
and preserves its frozen SecureBoot bit while SetupMode and VendorKeys are
reconciled from durable state. Reconciliation is cleared only after successful
media end, including for expected semantic read errors. This integration
remains internal: it adds no backend, dispatcher, service descriptor or
endpoint.
