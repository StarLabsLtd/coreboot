# Payload-MM memory-overwrite-request policy

`PAYLOAD_MM_AUTHVAR_MOR_POLICY` builds an EDK2 26.09-derived dormant policy for
`MemoryOverwriteRequestControl` and `MemoryOverwriteRequestControlLock`. It
publishes no endpoint and performs no media access.

The protected state models the public lock, optional eight-byte key, boot-entry
Control bit, intervening writes, ReadyToBoot completion, and a generation.
Support, entry state, and dirty state are sealed facts supplied only by trusted
coreboot lifecycle code. TPM presence is not MOR support: this tree has no
pre-payload MOR consumer, so production must seal support false until coreboot
provides an authenticated MOR-consumed fact or a proven always-clear policy.

Initialization runs at EndOfDxe, with ReadyToBoot fallback only if missed.
Supported initialization validates canonical records, creates absent Control
as zero, and resets public Lock to zero. Unsupported initialization deletes both
records and prevents recreation. Unlike EDK2, an absent Control is repaired
while establishing the lifecycle; malformed records fail closed.

SET policy runs after generic envelope parsing and authenticated-payload
stripping, but before fixed VarCheck, store lookup, runtime/attribute checks,
and AuthVariable verification. Records require exact NV|BS|RT attributes;
APPEND is rejected. Control accepts any byte while unlocked. Lock accepts public
0/1 or any eight-byte key, including all zeroes. A wrong key destroys the
volatile key and requires durable public Lock=1 while returning `ACCESS_DENIED`.
If persistence fails, key destruction is still published and production must
poison the service, a fail-closed hardening beyond EDK2's assert/log handling.

ReadyToBoot uses the captured entry bit. A new request in the same boot survives.
An eligible clear re-reads the current canonical byte under the media lease and
clears only bit 0. A lock denial is a terminal diagnostic while outer lifecycle
completion succeeds. As in EDK2, a failed clear terminalizes this boot and leaves
durable bit 0 set for the next boot's wipe; it is not retried this boot.

Finalization receives the same retained sealed input, reruns the planner, and
compares the entire canonical plan. `durable_commit=true` is valid only after
atomic execution, readback, rescan, and successful lease end. Ambiguous media
results poison the service. Production integration must scrub retained inputs,
plans, and key copies after every result.

Production integration is deliberately disabled. It still needs a trusted
support producer and linear SET/executor composition that commits internal Lock
mutations, including mismatch-with-`ACCESS_DENIED`, without a caller bypass.
