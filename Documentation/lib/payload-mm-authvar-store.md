# Read-only authenticated-variable store scanner

The scanner validates an EDK2 authenticated-variable store and either builds a
caller-owned, bounded in-memory index or returns one exact-key winner without
an index allocation. It is built only when
`PAYLOAD_MM_AUTHVAR_STORE_SCANNER` is selected.  The option is off by default
and does not publish a coreboot table, install an SMI handler, expose a write
path, or change the authenticated-variable service ABI.

The input starts at the 28-byte `VARIABLE_STORE_HEADER`; firmware-volume and
fault-tolerant-write headers are outside this interface.  The scanner accepts
the authenticated-store GUID, a formatted and healthy header, packed 60-byte
authenticated record headers, independent four-byte name/data padding, and
four-byte record alignment.  All offsets and sizes are checked before use.  The
caller supplies limits for the store, names, data, records, and index entries.
Names are non-empty, terminated UTF-16 byte strings without an earlier
terminator.  Visible records also carry nonempty data; a zero-data committed
record is an impossible EDK2 state and is rejected rather than exposed as a
successful GET.  Record padding and the unused store tail must remain erased.  An
EFI GUID is an opaque 128-bit value, so zero and all-ones values are legal keys.

A time-authenticated record may contain the all-zero EFI_TIME sentinel used by
EDK2 when it initializes variables such as its certificate database and
`VendorKeysNv` without a signed update. Nonzero timestamps retain the scanner's
stricter calendar and reserved-field validation. EDK2 26.09's store walk does
not range-check those calendar fields, so this remains an intentional malformed
store admission difference rather than a claim of byte-for-byte parser parity.

The index contains visible `VAR_ADDED` records in physical store order.  An
`VAR_IN_DELETED_TRANSITION & VAR_ADDED` record is a fallback, as in EDK2 26.09;
a later added record with the same key replaces it at the later physical
position.  Deleted and header-only records are not visible.  Two added records,
two transition records, or another ambiguous live-key history invalidate the
whole store.  Lookup compares the complete vendor GUID and UTF-16 name.

`payload_mm_authvar_store_find_one()` applies the same complete-store decoder
and winner rules without allocating the full index. It performs bounded prefix
rescans to detect ambiguous histories for every key, including keys unrelated
to the requested result. This constant-memory path is intended for early,
read-only consumers which need one fixed variable and cannot allocate an index;
it does not make malformed unrelated records ignorable. Query and output
pointers are caller-owned. Overlapping inputs and outputs are rejected without
modifying the inputs, and safe disjoint outputs are cleared on failure.

The semantic oracle is pinned in
`tests/lib/payload_mm_authvar_edk2_2609_semantics.tsv`.  Store scanning is
intentionally stricter than EDK2 recovery: truncated, overlapping, malformed,
or non-erased records invalidate the complete index instead of searching for a
later marker.  Authentication verification, authenticated update policy,
crash-safe writes, journalling, reclaim execution, endpoint dispatch, and
service publication are separate future gates.  The read-only GET, NEXT and
QUERY semantics and deterministic
reclaim plan are described in `payload-mm-authvar-store-semantics.md`.

`tests/lib/payload_mm_authvar_store_edk2_2609_fixture.h` is an independent byte
fixture derived from the pinned EDK2 format and parsing sources.  The host test
parses it directly and verifies its zero GUID, padded name, data offset, data,
record count, and lookup result; it does not recreate the fixture through the
test record builder.
