# AMD IOMMU DMA handoff prerequisites

An AMD early-DMA handoff must describe protection that firmware really owns.
The current Cezanne initialization does not meet that requirement, so coreboot
does not publish an AMD DMA handoff yet.

AMD's device-table and page-table layout differs from Intel VT-d.  In
particular, the existing coreboot DMA handoff validator requires the VT-d
root/context-table geometry and cannot describe an AMD device-table entry
without changing the ABI.

On Cezanne, AGESA/FSP owns the active AMD IOMMU device table and the page tables
referenced by it.  coreboot does not allocate those objects, cannot prove that
they are immutable through payload execution, and therefore cannot promise an
exact-requester arena backed by them.  StarLabs Cezanne boards also select an
NVMe or SATA storage topology at runtime, and PCI endpoint bus numbers are
assigned during enumeration.  Source-level root-port numbers are not a valid
substitute for the endpoint requester IDs observed by the IOMMU.

The AMD common IOMMU block provides a strict decoder for device-table register
values read from the live hardware.  It rejects disabled, reserved,
unsupported, overlapping, and oversized state, and rejects a runtime DeviceID
absent from a sparse table segment.  This is discovery infrastructure only; it
neither reads nor programs hardware and does not establish ownership.
It is intentionally not linked into production firmware until a consumer can
meet the ownership requirements below.

A production handoff additionally needs:

- an architecture-neutral ABI capable of representing AMD device tables;
- coreboot-owned device and page tables whose lifetime reaches payload exit;
- runtime enumeration of every bus-master requester and its aliases;
- exact arena mappings for the requesters that remain enabled; and
- fail-closed verification after the final bus-master-enable inventory.
