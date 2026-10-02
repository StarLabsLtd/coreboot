# Full default and protected regressions after CDK2 PR549

Both independent invocations completed at signed CDK2
`8b5025c7334d8099d1aa51ed93c5e149f232d3d2`. Their actual commands and GNU
resource measurements are retained verbatim in `default.log` and
`protected.log`. The outer executions were also reaped with status 0:
default execution 56782 and protected execution 15609.

Both commands run `review-profile-check check native-stage`. The default
run took 49m18.70s and the protected quiet run took 50m39.72s. These are
concurrent complete test/build durations, not firmware boot timings or
controlled performance comparisons. Each reused its independently owned
artifact directory and rebuilt changed source; neither is a fresh build.

The `*.before.sha256` files were captured before their respective commands.
Their actual configuration and generated-header hashes matched after the
commands and again when this packet was assembled. The checked source
worktrees remained at the same clean signed revision. The protected run
used the genuine `q35-protected-ui-producer.sv8jP6/full.config` producer input
and protected quiet defconfig recorded in its raw command. Neither run
reused the other's build directory or prepared configuration.

The root commands did not export TMPDIR=/home/sean, so scratch tests used
their normal /tmp locations. No out-of-space failure occurred in these
runs. Expected negative-test diagnostics, including literal FAIL messages
from rejected mutants, are preserved in the raw logs; the aggregate and
outer statuses are both 0. Earlier failed or contaminated runs remain
separate evidence and are not overwritten by this packet.

These receipts establish the listed HOST/build/regression gates at PR549,
not native authenticated firmware installation, hardware validation,
aggregate lint completion, or a whole-regression pass for newer revisions.
The newer PR553--557 focused receipts and remaining exact-head release
gates must be considered separately.
