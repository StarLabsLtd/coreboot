# Scratch configuration isolation: preserved failures and focused repair

The protected whole regression at CDK2 `74e4fa5122` failed with exit 2.
Unit `cdk2-root-full-protected-after535`, invocation
`2467060386a044a99a10e03dc3ba8ba3`, was actually reaped after 22m2.318s.
Its bridge scratch test inherited the explicit caller configuration-header
path: the outer resolved config still selected protected runtime, but the
generated header was overwritten with the default profile. No later leaf
from that contaminated run is evidence for a protected-profile pass.

CDK2 PR538 isolates the bridge's nested Make environment and registers the
real Core RAM-window gate in native checks. Focused unit
`cdk2-root-bridge-environment-isolation`, invocation
`50f78e6681da48e7ba635b673c595978`, was actually reaped 0 after 26.946s.
Before/after caller config and header hash checks passed. This proves the
focused bridge repair, not the whole regression.

The first wider seven-caller run failed with exit 2. Unit
`cdk2-root-scratch-callers-isolation`, invocation
`70bc2fbd7a984f4c9393185f57f7cb41`, ran for 1m6.460s. The linear fixture
expected an explicit unset assignment to disable a now-hidden default-on
composition. Independent inherited-producer and standalone traces both
reproduce that obsolete fixture expectation; it is not a header-leak fix
being accepted as a pass. Subsequent isolated replay also found the shared
scratch helper needed to clear inherited `CDK2_BUILD_DIR`: a nested
coreboot-stage test otherwise reused the caller's artifact directory.
Those wider repairs and resolved-cache negative fixtures are under review.

A root quiet-native validation run started while a peer scratch-caller run
used the same artifact directory. Root stopped its unit when the overlap was
identified. Unit `cdk2-root-quiet-consumer-after539`, invocation
`b7d3683332394a5da6278b1643ac268c`, was reaped with killed status 15/TERM,
after 27.547s (CPU 27.711s, peak 63.6M). The unit's `Result=success` label
does not override the killed process status: this run is NOT a pass. The
tracked quiet defconfig and actual immutable coreboot producer were then
used to regenerate the native caller config/header. Subsequent peer replay
uses a separate artifact directory. Whole and focused runs must bind both
caller hashes before and after testing; a log's PASS lines alone are not
completion evidence.

The six accompanying raw logs preserve the failed and focused results.
No hardware flash, current SMM writer admission, new endpoint publication,
authenticated firmware installation, or whole-project sign-off occurred in
these runs. Historical pass receipts remain separate and are not rewritten.
