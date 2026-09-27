<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Dormant SMM invocation entry and Intel adapter

`SMM_INVOCATION_ENTRY` is a hidden, default-off prerequisite for connecting
the protected invocation-evidence state machine to an SMM entry path.  This
slice deliberately installs no command, handler route or board selection.

The generic coordinator accepts a platform-produced immutable cause fact,
then records the full initial APIC ID and waits for every installed SMM
participant before any caller may contend for the global handler lock.  A
participant prepares its immutable ticket and completes all source checks,
then publishes a per-generation barrier acknowledgement as its final
fallible/source-dependent action. It subsequently performs only a bounded
wait for the complete acknowledgement mask before returning. Claim is gated
until that mask is complete, so no caller can contend for the handler lock
while another participant still has validation or ticket work. A failure
after admission atomically latches the shared failure request without waiting
for a stalled admission owner; that owner suppresses progress if it resumes.
Admission primitives return an explicit success, retry or terminal-error
result; callers never infer retryability from a later phase load. A retry
result carries a protected, one-use failure token bound to the evidence
object, admission kind, loader-instance nonce, loader lifecycle and invocation
generation. Wrong-kind, stale, replayed or cross-instance tokens cannot
change the state. A contender that observes a transient owner resnapshots an
exact stable state word; a token captured before a new reservation is never
returned as retry authority. One canonical 32-bit atomic state word binds the
phase and admission reservation in the same CAS: bits 0--4 hold the phase,
bits 5--6 identify the operation, bit 7 marks its owner busy, bit 8 records
one-use consumption, bit 9 records a shutdown request, bit 10 records callback
reentry, bit 11 records lifecycle close ownership, and bits 12--31 hold a
20-bit monotonically increasing
attempt nonce. Nonce
exhaustion poisons the instance; it never wraps or reuses an attempt. Failure
owners first claim `POISON_ADMITTING`, publish failure markers, and only then
release-publish `POISONING`, so quiescent cleanup cannot overtake those writes.
No `cb_err` wrapper guesses how to handle retry: every caller must either retry
within its frozen bound or submit the exact one-use failure token before its
non-returning reset.
The requester invokes the strongly linked platform non-returning fail-stop
action after exhausting the numeric poll budget frozen into the policy and
ticket. The policy and ticket contain no callback, context, or pointer:
returning a CPU through RSM would let an
incomplete rendezvous escape. The action must attempt a platform-wide reset,
then a platform-wide fallback reset or watchdog, and may terminally halt only
as its final fallback; merely stopping the requesting CPU is insufficient.
Protected cleanup may be deferred to hardware reset and is not claimed to
complete first.
A future callsite must retain the shared cause
until all arrivals are recorded, let the registry-selected owner claim and
complete or abort the evidence, release the global lock, record every
departure, and set EOS only after `smm_invocation_entry_eos_ready()`.
EOS readiness consumes a one-use close receipt bound to the exact invocation,
loader-instance nonce, loader lifecycle and BSP ticket; a bare or stale `READY`
phase is insufficient. Departure uses the same explicit success/retry/error
contract; the coordinator bounds retries by the poll budget frozen into the
participant ticket and resets rather than spinning behind a stalled departure
owner. Departure validation remains in `DEPARTURE_ADMITTING`; the owner must
win the final CAS to `DEPARTURE_COMMITTING` before it can publish the departed
bit. An exact-generation timeout that wins first owns
`DEPARTURE_FAIL_ADMITTING`, publishes every failure marker, and only then
hands off to cleanup. A timeout that loses to the final commit is a byte-level
no-op and cannot overwrite the committed owner or a terminal object.

The entry ABI change is conditional.  With the option disabled, the existing
16-bit APIC map and stack argument layout are unchanged.  With it enabled,
the stub compares the complete CPUID initial APIC ID against a 32-bit map and
passes that value beside the logical CPU number.  The installed participant
topology, loader-instance nonce and BSP identity must come from the loader;
the logical lock winner and `CONFIG_MAX_CPUS` are not evidence.

The Intel adapter seals exact save-state addresses for the actual participant
count.  It supports only revisions `0x30100` and `0x30101`.  A match requires
the complete I/O-misc value `0x00b20003`: synchronous valid, byte-width OUT DX,
reserved bits zero, and the full port `0x00b2`.  OUT-immediate encodings,
truncated port aliases and unsupported revisions fail closed.  Revision,
I/O-misc and full 64-bit RAX are copied and rechecked around access to the same
sealed node; the existing lazy `apmc_node()` and generic register helpers are
not used. A successful exact match also seals node identity, revision,
I/O-misc, full RAX and an invocation nonce. Reads and writes require that seal,
recheck the node descriptor and tuple around access, and update the sealed RAX
only after a verified full-width write.

## Remaining integration gate

No existing generic entry can safely select this code yet.  Safe integration
requires one atomic composition containing all of:

1. a shared, recognized private APMC cause predicate visible to every active
   participant and retained through rendezvous;
2. a new loader instance with explicit active topology, BSP and nonzero
   128-bit correlation nonce, with the previous instance closed and drained;
3. registry dispatch owning the exact command and the evidence claim;
4. one completion/abort owner which moves evidence to `CLOSING`;
5. a selected platform capability with exactly one strongly linked SMM-safe
   non-returning reset or terminal halt for timeout or ambiguity.

Until that composition exists, the hidden platform capability has no selector
and tests require the coordinator to have no production callsite.  Q35 may
exercise only the generic synthetic rendezvous because its save state lacks
the exact Intel I/O-misc evidence.  Intel common 30100/30101 builds are compile
targets; Meteor Lake hardware remains the production adapter validation gate.
The focused target also compiles 32-bit freestanding objects without
`libatomic`, checks 32-bit and 64-bit stub disassembly, and compares the
option-disabled stub objects byte-for-byte with the base revision. Platform
profile validation uses `configs/config.starlabs_lite_glk` for 30100 and
`configs/config.starlabs_lite_adl` plus
`configs/config.starlabs_starbook_mtl` for 30101. These are compile checks, not
full-image or hardware validation; a full Meteor Lake image additionally
requires the external FSP tooling.
`make -f util/testing/Makefile.mk test-smm-invocation-entry-profiles`
reproduces those exact forced-capability object compiles in isolated build and
configuration directories. It does not imply that any board selects the
hidden capabilities.
