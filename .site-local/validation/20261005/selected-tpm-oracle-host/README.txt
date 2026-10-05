PR705 selected-boot TPM oracle — bounded HOST tests

Signed source 3e14163db35cdee8250cc7a644617a94af23c8a2, parent
649e12efcc641f84d544d8453f8db1e4d11069ad. Exact four-file binary delta
1b6f467d4a1dcdde7ff8ed000425bb0ca680384c66105ec03c1e5b0b59522840.
Literal Root records: selected-tpm-root-host.XvPjz0ZM under
/home/sean/current693-fwui-qemu.PMINOC; author-oracle files copy literal
selected-tpm-author-host.1frzxTyS/oracle.log and oracle.status.

Root oracle raw zero/PASS, stable lint raw zero/all sixteen checks, Linux
init shell syntax raw zero/empty log; independent author oracle raw zero.
Independent reviewer reads all changed bodies, existing firmware measurement
and oracle contexts and actual gate records before source publication.

Explicit independent expected fixture verifies BootOrder then listed
Boot#### entries including repetitions, exact global GUID/header/name/data,
PCR1 phase and every active bank's legacy variable-DATA-only digest. Selected
response has PCR1/4/5 mask32 and twelve digests; ordinary fallback keeps its
separate mask30 response and prior checks. Hostile cases include matching
live-PCR replay with wrong full-event hashes or BOOT2 events, malformed
wire/order/cardinality/banks/responses, source-HEAD/path/hash/run-manifest
binding refusals and wrong-mode CLI refusal. Test expectations are synthetic
HOST fixtures, not guest-written variables or evidence of actual selection.

The telemetry source change does not rebuild or repin an existing UKI. A
legitimate variable writer, current rebuilt guest identity, real cold-media
chain, selected-option/loaded-image proof, genuine TPM-enabled producer and
actual guest/live-PCR replay remain open. Neither normal PR702 nor PR703
is TPM proof. No firmware build, QEMU, hardware or universal closure claim
is made by this finite packet. Signed source and originals remain retained.
