# AMD IOMMU DMA handoff

An AMD early-DMA handoff must describe protection that firmware really owns.
The generic decoder only observes FSP-owned state; observation does not make
that memory trustworthy.  Cezanne can optionally replace it with fresh,
coreboot-owned tables by enabling `SOC_AMD_CEZANNE_DMA_HANDOFF` and supplying
an exact mainboard boot-controller policy.

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
absent from a sparse table segment.  Cezanne uses that decoder only to validate
the precondition that FSP left active, unsegmented translation.  It never
adopts the opaque FSP tables.

The Cezanne producer allocates a zero-default device table, one page table and
one immutable CBMEM arena per selected requester, and the public handoff blob.
It takes an identity snapshot (BDF, vendor, device, class, and revision) of
every function in the configured segment-0 ECAM aperture, clears every present
function's bus-master-enable bit, and repeats that full scan after each IOMMU
transition step.  A missing, newly visible, or identity-swapped function, a
restored BME, or an unexpected register value terminates in `die()` while the
bus masters remain clear.

PCI class discovery is not boot policy.  A mainboard opting in must implement
`mainboard_cezanne_dma_boot_controller()` and return a typed decision only for
each exact payload-owned controller.  The producer freezes those device
pointers and priorities before changing hardware, and the revision-4 resource
handoff must later publish the same set and generation.

This backend currently supports one segment and natural requester IDs for
NVMe, xHCI, and AHCI controllers.  A platform that needs requester aliases,
multiple IOMMU segments, or another controller type must extend and test the
typed policy and table construction before enabling the option.  The code has
host and 32-bit ramstage build coverage; that is not hardware validation.
