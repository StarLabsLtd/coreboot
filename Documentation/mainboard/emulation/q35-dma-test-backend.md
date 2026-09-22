# QEMU Q35 DMA test backend

`Q35_VTD_DMA_TEST_BACKEND` is a test-only VT-d implementation. It installs a
VT-d root and context entry for domain 1, verifies translation is active, and
keeps QEMU EDU's bus-master bit clear. EDU is a synthetic fault and
positive-control oracle, not a payload boot controller. The backend therefore
does not select `PAYLOAD_DMA_HANDOFF`, allocate `CBMEM_ID_DMA_HANDOFF`, or emit
an `LB_TAG_DMA_HANDOFF` record for EDU. A separate real-requester producer must
map the controllers selected by revision 4 of the payload resource handoff.

The private `CBMEM_ID_Q35_VTD_TABLES` allocation contains six resident
pages: one root page, one context page, and a four-page requester hierarchy.  Root, context,
and upper-level page tables are linked to the requester and domain 1. One
identity-mapped page is the requester's immutable DMA arena. Every other leaf
remains absent, so the requester cannot perform DMA outside that arena. There
is deliberately no broad identity-map fallback. On a noncoherent VT-d
unit, every table cache line is written back before the root is installed.

After enrolling EDU in the deny domain, the QEMU-only self-test temporarily
asserts its bus-master bit and requests a DMA write.  Publication proceeds only
after VT-d reports the exact requester fault, the target remains unchanged,
the fault is acknowledged, and the bus-master bit is clear again.

`Q35_CAPSULE_DMA_TEST_PROOF` additionally selects the fixed capsule buffers and
turns this narrow backend into an exact-range test oracle.  It scans every
present function in segment zero's complete Q35 ECAM aperture after clearing
every bus-master-enable bit.  Every proof
call then requires the same complete requester inventory with BME still clear,
the exact root-table address, root-pointer and translation status, unchanged
root/context/page tables, and unchanged disabled protected-memory-region
registers.  It walks every page covered by the complete 4 KiB communication
reservation and bounded staging allocation and rejects any present mapping.

Each proof call also asks EDU to write into both protected allocations.  The
call succeeds only when each request produces a fault for EDU's exact source
ID and destination page and leaves the target bytes unchanged.  A separate
pre-mapped arena page is the positive control; EDU must change its contents
without raising a fault or changing any VT-d table. The default-deny state is
then reread.
Only a request for the exact 168-byte transport or complete staging geometry
can invoke this proof; the actual DMA checks cover the full communication
reservation in either case.

The option is default-off and still installs no capsule broker, SMI route or
coreboot-table endpoint.  EDU is the only QEMU device used for a real DMA fault
and positive-control transaction.  Other enabled PCI functions are covered by
the closed default-deny root/context topology and complete BME inventory, not
by device-specific DMA engines.  Consequently this remains QEMU evidence, not
a production all-device or hardware DMA-protection claim.

The standalone QEMU harness proves the private VT-d backend with a protected
run and rejects missing IOMMU, missing EDU, and duplicate EDU topologies. It
also rejects any run that emits a public DMA handoff for the synthetic device.
Capsule-buffer fault injection remains a separate opt-in proof layered on this
backend.

This code proves the narrow QEMU contract only.  It is not an Intel or AMD
production backend: it has no chipset discovery, protected-memory carve-outs,
interrupt-remapping policy, multi-IOMMU scope handling, resume reconstruction,
or payload ownership transition that could enable bus mastering.
