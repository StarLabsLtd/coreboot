# PCI bus-master quiescence

`PCI_BME_QUIESCE` builds a dormant, stage-neutral helper for disabling PCI bus
mastering before a caller changes DMA protection. It has no boot hook and does
not by itself establish a DMA-protection policy.

The caller supplies allocation-free segment-zero ECAM accessors, a fixed
snapshot, and a distinct fixed workspace. `pci_bme_quiesce()` walks every
function on every requested bus, clears Bus Master Enable on each present
function, reads the command register back, and records the complete topology,
identity, and original non-BME command bits. A write which changes another
command bit, more than 512 present functions, a retained Bus Master Enable bit,
an empty topology, or callback mutation makes the snapshot unusable. The
workspace keeps the roughly 6 KiB topology out of the stage stack and allows
the completed initial observation to be checked before it is published.

`pci_bme_quiesce_revalidate()` repeats the exhaustive walk and accepts only the
same bus count, topology, identity, class, non-BME command bits, and disabled
Bus Master Enable state. It marks the snapshot failed on any late mismatch.
Callers must revalidate immediately before changing DMA protection and again
before consuming any result which relies on that transition.

The fixed terminal helper performs two exhaustive clear-and-readback passes.
It is best-effort because it has no result channel; a caller must not use it as
evidence that DMA is contained.

The helper covers PCI requesters whose DMA authority is gated by the PCI
command register. Platform code remains responsible for handling non-PCI DMA
requesters and for proving the surrounding protection transition.
