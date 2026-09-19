# QEMU Q35 DMA handoff test backend

`Q35_VTD_DMA_TEST_BACKEND` is a test-only implementation of the DMA handoff.
It allocates the modern `CBMEM_ID_DMA_HANDOFF` entry before table writing,
installs a VT-d root and context entry for domain 1, verifies translation is active, and keeps
the sole explicitly admitted QEMU EDU requester’s bus-master bit clear.  The
handoff is emitted only after revision 4 of the payload resource handoff has
been serialized with the same generation.

The exact-sized `CBMEM_ID_DMA_HANDOFF` allocation contains only the serialized
handoff.  A distinct `CBMEM_ID_Q35_VTD_TABLES` allocation contains six resident
pages: one root page, one context page, and a four-page requester hierarchy.  Root, context,
and upper-level page tables are linked to the requester and domain 1.  The
target leaf remains absent, so the requester has no mapping and cannot perform
DMA.  There is deliberately no identity-map fallback.  On a noncoherent VT-d
unit, every table cache line is written back before the root is installed.

After enrolling EDU in the deny domain, the QEMU-only self-test temporarily
asserts its bus-master bit and requests a DMA write.  Publication proceeds only
after VT-d reports the exact requester fault, the target remains unchanged,
the fault is acknowledged, and the bus-master bit is clear again.

This code proves the narrow QEMU contract only.  It is not an Intel or AMD
production backend: it has no chipset discovery, protected-memory carve-outs,
interrupt-remapping policy, multi-IOMMU scope handling, resume reconstruction,
or payload ownership transition that could enable bus mastering.
