# Independent future-floor HOST replay

The tested production source and fixture bodies match signed CDK2
`a9f95cd47999642fc6b256a3b41936d8d65a5716` (PR542). Production changes
remove only two runtime metadata upper caps; compiled endpoint baselines,
running-image identity and minimum-floor validation remain unchanged.

Root actually reaped session 41705 with exit 0 for user unit
`cdk2-root-future-floor-after541-retry`, invocation
`ebfba6a3cfd84c38bba6d8d80aaced21`. Its fresh artifact directory was
`/home/sean/cdk2-root-future-floor.jNhjXx`. Runtime was 13.464s (GNU 13.43s),
CPU 13.475s and peak memory 63.4M. Caller config/header hashes passed before
and after. The archive preserves the actual generated signed images, public
test certificates, binaries, byte-exact mutants, source manifest and logs.
Transient test signing keys were removed by the harness before archiving.

O0/O2 real session/CMS/payload/board parsers refused trusted below-floor and
rogue-signed images, and allowed at-floor SET delegation. Transport execution
and record discovery in that session fixture are explicitly HOST models.
Each of the two removed caps was restored separately at O0/O2 and rejected
by its exact expected assertion (exit 134), not a sanitizer fault. The four
expected abort messages in the outer log are those causal tests.

Root's separate direct transport-client/importer replay, session 1466, also
exited 0, runtime 3.673s, CPU 3.809s and peak memory 43.4M. Raw logs are included.
The initial combined named-target invocation exited 2 because the existing
transport targets were not registered as outer Make targets. That failed
harness invocation remains separately included; the correction used the
registered new target and directly invoked the existing test scripts.

This is bounded HOST consumer validation, not a native SMM capsule installation,
hardware result, whole regression at PR542, or final project sign-off.
