# BOOT-private buffer table publication

`LB_TAG_PAYLOAD_BOOT_PRIVATE_BUFFER` (local tag `0x0058`) describes one reserved
span containing three consecutive 64 KiB slots: request, response, policy. Its
existing revision-1 record is 40 bytes, with a packed 64-bit physical address,
4 KiB alignment and zero reserved fields. Metadata supplies no origin, DMA
protection, receipt secret, generation authority or permission to use the slots.

The StarBook MTL RAMstage publisher appends this record only when both
`PAYLOAD_BOOT_PRIVATE_BUFFER` and `PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED` are
enabled. It first validates the canonical revision-3, 472-byte BOOT reply and
exact equality of all originally emitted receipt bytes. It resolves the
original opaque handle through the committed bootmem reservation owner and
requires matching base, size and `BM_MEM_RESERVED` kind. Both the service endpoint
and private span are checked before either table record is appended. The common
typed writer fixes the existing geometry; it does not perform allocation or
authenticate the board's admission decision.

The ordinary memory table is emitted before the board records. Reservation
registration occurs before bootmem commits that map, so both records describe
the same actual reserved backing. Table checksums cover the appended metadata;
checksums are not origin authentication.

## Paired component test

Run `tests/lib/payload_boot_private_table_delivery_test.sh` with
`BOOT_PRIVATE_TABLE_CDK2_SOURCE` naming a reviewed CDK2 checkout and its generated
`build/cdk2/include/cdk2/config.h`. `BOOT_PRIVATE_TABLE_CDK2_REVISION` optionally
pins a commit instead of HEAD. The test archives that exact committed consumer
source and captures its generated config before compiling, reporting the commit
and config digest. It links the actual allocator, receipt
signer/verifier, combined sender, board publisher, table initializer/writer and
checksum finalizer. The opposing component parses those exact file bytes through
CDK2's real parser, importer and HOB builder, then appends the real reserved-memory
allocation HOB. Both components run at O0/O2 with ASan and UBSan. Failed ACKs,
mixed revisions and changed receipts cannot append either board record. Mutated
tables with valid checksums but nonreserved backing, incorrect slot geometry or
duplicate metadata fail import/reservation without changing outputs or HOBs.

Loader identity, entropy/topology and the trigger's BOOT/provider admission are
explicit host models. The trigger consumes the real protected-slot receipt, but
does not reproduce the separately tested all-CPU finalizer/wave. Unrelated table
producers are discarded in this component link; no authority owner is replaced
by a success stub. File transfer is a fixture, not proof of trustworthy entry.

## Remaining activation gates

There is no selected CDK2 consumer hook, policy registration or public runtime
variable cutover here. Genuine protected first-entry delivery, a DMA-excluded
immutable policy destination, resumed-CPU copy coherence and a protected policy
epoch remain prerequisites. The terminal lifetime must close before the first
external `LoadImage`, as well as `StartImage`, EBS and S3; closing only at
`StartImage` is too late. Metadata and an SMM-only held wave cannot establish
those properties. A policy larger than the fixed slot must be refused, never
silently truncated or sent to a general runtime allocator.
