# QEMU Q35 DMA test backend

`Q35_VTD_DMA_TEST_BACKEND` is a test-only implementation of the DMA handoff.
It allocates the modern `CBMEM_ID_DMA_HANDOFF` entry before table writing,
installs a VT-d root with exact NVMe and XHCI context entries in domains 1 and
2, verifies translation is active, and keeps every PCI bus-master bit clear. The
handoff is emitted only after revision 4 of the payload resource handoff has
been serialized with the same generation.

The exact-sized `CBMEM_ID_DMA_HANDOFF` allocation contains only the serialized
handoff. A distinct `CBMEM_ID_Q35_VTD_TABLES` allocation contains ten resident
pages: one root page, one shared bus-zero context page, and a four-page
hierarchy for each admitted requester. `CBMEM_ID_Q35_DMA_ARENAS` reserves the
32-page NVMe and 128-page XHCI CPU arenas. They are mapped at fixed IOVAs
`0x80000000` and `0x90000000`; every other leaf remains absent, so neither
requester can perform DMA outside its arena. There is deliberately no broad
identity-map fallback. On a noncoherent VT-d unit, every table cache line is
written back before the root is installed.

EDU is deliberately unlisted and has no context entry. The QEMU-only self-test
temporarily asserts its bus-master bit and requests a DMA write. Publication
proceeds only after VT-d reports the exact requester fault, the target remains
unchanged, the fault is acknowledged, and the bus-master bit is clear again.

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
ID and destination page and leaves the target bytes unchanged. Both admitted
requester hierarchies must also lack every protected page. The default-deny
state is then reread. Real NVMe and XHCI traffic is a separate payload-level
positive control rather than being impersonated by EDU.
Only a request for the exact 168-byte transport or complete staging geometry
can invoke this proof; the actual DMA checks cover the full communication
reservation in either case.

The option is default-off and still installs no capsule broker, SMI route or
coreboot-table endpoint. EDU supplies the active denial transaction; a paired
payload must exercise NVMe and XHCI through their bounded arenas for a positive
control. Other PCI functions are covered by the closed default-deny
root/context topology and complete BME inventory. Consequently this remains
QEMU evidence, not a production all-device or hardware DMA-protection claim.

The standalone QEMU harness proves the real-requester producer and rejects a
missing IOMMU plus missing or duplicate NVMe, XHCI, and EDU topologies. It
requires the public handoff only for the exact NVMe and XHCI boot intent.
Capsule-buffer fault injection remains a separate opt-in proof layered on this
backend.

This code proves the narrow QEMU contract only.  It is not an Intel or AMD
production backend: it has no chipset discovery, protected-memory carve-outs,
interrupt-remapping policy, multi-IOMMU scope handling, resume reconstruction,
or payload ownership transition that could enable bus mastering.
