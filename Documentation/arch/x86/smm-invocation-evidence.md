# Dormant SMM invocation evidence

The `SMM_INVOCATION_EVIDENCE` option compiles a protected, dormant state
machine for a future private SMM dispatcher. It does not install an entry
hook, claim an APMC command, call the command registry, expose a table, or
activate a presence route. No platform selects it.

The loader contract supplies the installed SMM participant count, the exact
logical-node to initial-APIC-ID topology, BSP node, a nonzero boot/resume
generation. These are runtime facts; neither
`CONFIG_MAX_CPUS` nor `boot_cpu()` is evidence. Reuse of an existing instance
across reset or S3 is rejected. A fresh cold or resume loader instance is
accepted only with a nonzero, fresh boot/resume generation after the trusted
lifecycle has closed the old instance.

Every installed participant must publish one arrival before the existing SMM
handler lock. Per-CPU slots move `EMPTY -> WRITING -> READY` with release/acquire
ordering. The claim owner can seal only after every slot contains the expected
APIC ID and one common invocation generation. Departure is also counted, so an
epoch cannot be reused while a participant still executes the old SMI.

The save-state adapter must inspect every active node and return `MATCHED` only
after a revision-specific immutable snapshot proves a valid synchronous I/O
write of exact `outb` width to the full APM control port, the requested command,
and permitted reserved bits. It must recheck that complete tuple around every
save-state access. Its match and full-RAX callbacks form one cohesive,
platform-trusted adapter and must keep an immutable handle to that exact node;
performing a fresh lookup between callbacks is invalid. The collector requires
exactly one match and requires that node to be the loader-recorded BSP. The
current `apmc_node()` first-match helper does not meet this contract. Q35
AMD64/legacy save state lacks the I/O metadata and its AL/EAX heuristic is
explicitly unsupported.

Claim writes and reads back a full-width sentinel whose low byte remains the
command. Publication first proves that sentinel still exists on the same node,
then writes and reads back the full-width result. The token records the boot
generation, invocation generation, topology and initiator plus a deterministic
digest. It is immutable one-invocation correlation data, not a secret, an
authenticator, or a capability. Callers must not use it as authority.

Shutdown requests are observed around every fallible adapter boundary. Before
the sentinel is written, callback reentry poisons the invocation. After the
write, abort restores and verifies the original full RAX; any ambiguous restore
or result publication invokes the provisioned nonreturning fail-stop policy.
The generic shutdown operation waits for participants already inside the SMI.
Production composition therefore also requires a nonreturning platform
timeout/reset path for a participant that never arrives or departs.

Production integration remains blocked on platform entry instrumentation that
can call arrival before locking and departure afterward on every installed SMM
participant, a revision-specific Intel save-state adapter, a trusted loader BSP
and boot/resume-generation source, and a nonreturning timeout/reset policy for
a missing participant. Adding those callsites changes production SMM behavior
and is deliberately outside this prerequisite.
