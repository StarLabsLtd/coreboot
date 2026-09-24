# Payload-MM MOR entry probe

`PAYLOAD_MM_AUTHVAR_MOR_ENTRY_PROBE` builds a dormant ramstage reader for the
entry value of the UEFI memory-overwrite-request Control variable. It obtains
the fixed, read-only `SMMSTORE` FMAP view, requires the EDK2 FV/FTW decoder to
report a clean transaction state, validates the complete variable store, and
accepts only the exact Control key with `NV|BS|RT` attributes and one data byte.

Absence is a successful result. Any recovery residue, malformed or ambiguous
store, noncanonical record, geometry error, mapping error, or unmapping error
fails closed and leaves a zero result. The result is published only after the
read-only mapping has been released.

This option does not enable MOR support, clear memory, produce a completion
grant, install an SMI handler, or publish an endpoint. A later platform-owned
composition must establish a trusted cold-boot classification and prepare its
DMA guard before invoking the probe. It must then prove that the entry request
was acted on before Payload-MM may advertise MOR support. This prerequisite
does not add that caller or produce a cold-boot generation.
