# Full integration checks at PR475

Frozen CDK2 source: `97835a23debbc39d720a3c13aa5f70f2850d3b95`.
Worktree: `/home/sean/Documents/.cdk2-worktrees/protected-variable-runtime-reviewed451`.
This incorporates the reviewed consumer, native private types, per-run test
object reuse, native refusal component and secure-pflash test source.

Both managed readers invoked `make -k -j2 check native-stage` with
`CDK2_CONFIG_READY=1`, an explicit build directory, a 64 MiB stack limit and
`TMPDIR=/home/sean`. Source stayed frozen until both processes were reaped.

| Profile | Actual tool reap | Elapsed | CPU | Peak memory |
| --- | --- | --- | --- | --- |
| Default | session 82795, exit 0 | 20m21.362s | 40m58.253s | 743.7 MiB |
| Selected | session 34571, exit 0 | 23m58.036s | 46m38.300s | 1 GiB |

## Configuration identity

- Default directory: `build/protected-variable-root-whole-e468`.
  Config SHA256: `8a70dc70dc9cb75849aa1bd9cc15fa472a22a1a9d131a011b38a290130cdba46`.
  Generated header SHA256: `6eb3a4fcb4f44f68ccc1ccaf9ec607c054ff16c43e689b88d0f8dd70e88d19ff`.
- Selected directory: `build/qemu-owned-style-root-694/cdk2`.
  Config SHA256: `b01cc23d5abc7a07b1daae14cb758a381b4c6a4ec39d9551619eb3cb5d14eaeb`.
  Generated header SHA256: `4236bb2180e7fb61dee32ba9dc2e84458929d407b42f83a4e6d25c5c5486b292`.

Build directory labels do not identify source revisions. Raw make output and
independent journal records are retained here. The actual tool-reap receipts,
not positive log tails or inactive service state alone, establish exit 0.

## Limits

These checks cover the selected `check` dependency graph and native-stage
builds, not every standalone target or configuration. Intentional compiled
negative tests can emit `Aborted` or rejection diagnostics in successful raw
logs; they do not authorize reclassifying unrelated failures as passed.

Host entry/core/Auth2 joins use modeled IRQ/HOB/trigger inputs. Native image
closure checks are builds, not native SMI execution. Actual Q35 missing-endpoint
refusal and secure-pflash controls have separate exact firmware receipts in the
20261001 evidence. No new normal guest boot, positive protected native service,
hardware validation, full-tree style sign-off or project completion follows
from these two successful readers. Later NVMe cleanup is not in this source SHA.
