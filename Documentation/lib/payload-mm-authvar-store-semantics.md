# Read-only authenticated-variable store semantics

`PAYLOAD_MM_AUTHVAR_STORE_SEMANTICS` builds pure GET, NEXT and QUERY helpers
over a successfully scanned authenticated-variable store. It also builds a
deterministic append-or-reclaim space planner. The option is off by default and
adds no media callback, write path, FTW executor, SMI route, shared-memory result
writer or service endpoint.

The helpers return fixed-width EFI status values without importing EDK2 types.
GET resolves one exact GUID and UTF-16 name, applies runtime visibility, and
reports attributes and the required data size on both success and
`BUFFER_TOO_SMALL`. A structurally valid empty UTF-16 GET name returns
`NOT_FOUND`, as EDK2 26.09 does. The current service request validator does not
admit that wire request, so a later CDK2 proxy or ABI composition gate must
handle it before parity can be claimed end to end.

NEXT requires the canonical zero-size, zero-GUID cursor for the first entry.
A nonempty cursor must identify a currently visible variable or the result is
`INVALID_PARAMETER`; reaching the end is `NOT_FOUND`. Enumeration retains the
scanner's physical order and skips boot-service-only variables at runtime. It
does not sort names or GUIDs. For the initial cursor, EDK2 26.09
`FindVariableEx()` returns the first visible `VAR_ADDED` record, or the last
visible transition record when no added record exists. The helper preserves
that otherwise non-obvious selection rule.

QUERY covers only the persistent common-variable class represented by this
single store. It does not invent volatile, HOB, hardware-error or user quota
policy. Before runtime, remaining capacity counts only live records because
dead records can be reclaimed. At runtime, every parsed physical record counts
because reclaim is unavailable. Maximum storage excludes the 28-byte store
header. Maximum variable name-and-data capacity is bounded by the configured
record maximum and the remaining capacity after one 60-byte authenticated
record header.

QUERY applies EDK2's recognized-attribute mask: unknown bits do not by
themselves invalidate an otherwise supported class, while a request containing
only unknown bits is unsupported. The deprecated authenticated-write bit has
highest-priority `UNSUPPORTED` handling, and APPEND is a recognized attribute.
An index with a dirty tail returns `DEVICE_ERROR` with a zero result. Recovery
and reclaim must canonicalize that store before QUERY, so neither a complete
uncommitted record nor arbitrary partial tail bytes can affect quota output.
The dormant service-frame validator is a separate composition boundary and
still rejects APPEND and unknown bits; it must be aligned before any endpoint
can claim these helper semantics end to end.

The planner chooses append without requiring reclaim scratch when the new
record fits. Otherwise, before runtime, it describes a compact image using
caller-owned fixed-capacity entries. It selects physical `VAR_ADDED` records
first, then lone transition records in physical order, promotes each selected
transition in destination metadata, excludes the update target, and places the
new record last. This ordering deliberately does not reuse the scanner index:
for `transition A, transition B, added A`, the initial enumeration result is
the added A, while reclaim output is `A, B`. At runtime, an append that does not
fit returns out-of-resources rather than planning reclaim.

The planner validates only a representable, nonempty name/data footprint. It
does not validate attributes, payload contents, authentication, timestamps,
monotonicity, deletion or append authorization, or any update policy.

The scanner remains deliberately stricter than EDK2 for malformed content and
ambiguous duplicate histories. The semantic layer operates only on a successful
scan and does not claim permissive recovery parity. The plan contains source and
destination offsets, exact copy sizes and transition-promotion intent only. It
can be discarded or recomputed without changing media. A later reviewed gate
must build the complete spare image, execute the FTW transaction through an
SMM-only backend, verify every write, and test reset injection at each erase and
program boundary.

Pinned source identities and behavioral decisions are recorded in
`tests/lib/payload_mm_authvar_store_semantics_edk2_2609.tsv`. Host tests cover
strict O0/O2 compilation, ASan, UBSan, GET buffer sizing, runtime visibility,
the initial NEXT added/transition selection, QUERY masks and dirty-tail
rejection, append without scratch,
reclaim ordering, transition promotion, replacement exclusion and runtime
out-of-resources behavior.
