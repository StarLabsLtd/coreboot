# Authenticated-variable recovery planner

`PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER` is a dormant SMM-executor prerequisite.
It adds no command, dispatcher, boot hook, or support claim.

After the executor acquires its exclusive media lease and reads the complete
SMMSTORE into protected memory, the planner copies that immutable snapshot into
one fixed full-store candidate arena. It decodes and byte-simulates at most the
executor's existing 16 recovery iterations. The resulting plan retains the
exact FTW geometry and queue decision for every step, the lease generation and
token, and the predicted complete post-recovery image. A second full-store
arena retains a byte-exact seal of that image. Planning performs no media
operation and allocates no memory.

The executor seals a second byte-exact plan copy before authorization can be
interposed. Execution first compares live media with the retained source
snapshot, then re-decodes and compares every planned step before applying the
existing verified recovery primitive. While plan execution is active, the
executor's media-read boundary compares the predicted image with its seal after
every callback-bearing read, before the recovery primitive can issue a later
program or erase. It also compares the seal immediately before every recovery
step.
Each step is durably synchronized and read back by the media port. Completion
requires a clean FTW decode and full byte equality with the predicted image
before the variable store is scanned.
Snapshot, geometry, step, lease-token, plan-copy, callback, or final-image
mutation fails closed.

When selected, existing executor transactions use plan followed immediately by
execute, preserving their external recovery behavior. The private test seam is
compiled only for tests; production exposes no recovery-plan entry point.
