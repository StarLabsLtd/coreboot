# Frozen PR465 default and selected checks

CDK2 source: `55bd35d644e7b53feedbfa7fdd34e47010dbaf92`.
Worktree: `/home/sean/Documents/.cdk2-worktrees/protected-variable-runtime-reviewed451`.
The source remained frozen throughout both completed readers.

Both invoked `make -k check native-stage` with `CDK2_CONFIG_READY=1`,
an explicit build directory, a 64 MiB stack limit and `TMPDIR=/home/sean`.
The default invocation used `-j4`; the selected invocation used `-j2`.

| Profile | Build directory | Config SHA256 | Generated header SHA256 |
| --- | --- | --- | --- |
| Default | `build/protected-variable-root-whole-e468` | `8a70dc70dc9cb75849aa1bd9cc15fa472a22a1a9d131a011b38a290130cdba46` | `6eb3a4fcb4f44f68ccc1ccaf9ec607c054ff16c43e689b88d0f8dd70e88d19ff` |
| Selected | `build/qemu-owned-style-root-694/cdk2` | `b01cc23d5abc7a07b1daae14cb758a381b4c6a4ec39d9551619eb3cb5d14eaeb` | `4236bb2180e7fb61dee32ba9dc2e84458929d407b42f83a4e6d25c5c5486b292` |

Directory labels identify reused build directories, not tested source revisions.

## Actual completion

- Default restart-proof retry: tool session `99827`, reaped exit 0,
  10m36.069 elapsed, 39m52.426 CPU, 1.1 GiB peak memory.
- Selected check: tool session `69680`, reaped exit 0,
  19m28.156 elapsed, 39m10.832 CPU, 953.9 MiB peak memory.

Raw make output and independent systemd journal records accompany this file.
The completed runs write directly to log files instead of using a live output
pipe, so a tool-server restart does not break the test's stdout consumer.

## Preserved failure and limits

The first default invocation used `tee` through a tool-owned pipe. A server
restart closed that pipe; the service exited 141 after 10m38s. Its raw output
and journal are preserved as interrupted evidence, never counted as a pass.

Exit 0 covers the actual selected `check` dependencies and native-stage target,
not every standalone target or configuration. In particular, the separately
invoked selected SMMSTORE driver target exposed a missing diagnostic-source
test closure; that later fix is `b7ea46f1d9449192a9d5fb89dc3045bb1da1de59`,
not part of these frozen PR465 runs.

These are host/build checks, not fresh guest boots, native protected-service
activation, hardware validation or project completion. The separate PR465
firmware parity and PR459/coreboot349 guest receipts retain their own scope.
