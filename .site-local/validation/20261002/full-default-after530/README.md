# Full default regression and protected failure

The primary agent reaped the complete default `review-profile-check check
native-stage` process with exit 0 at immutable clean CDK2
`f0445d0e830c23049fcc35d990d1b1de0c62d5ff`. Unit
`cdk2-root-full-default-after530`, invocation
`7419954a2cad4e2c9269876fcd76c7c7`, completed in 23m15.409s with
46m56.380s CPU and 957M unit peak memory. The raw GNU time peak is the
individual command accounting, not the unit aggregate.

This reused the actual build directory from the earlier failed whole run;
changed-source dependencies rebuilt. It is not a fresh-directory result.
The configuration SHA256 is
`8a70dc70dc9cb75849aa1bd9cc15fa472a22a1a9d131a011b38a290130cdba46`;
the generated configuration header SHA256 is
`48556c4c243ffc3e67c3f91293ed3c220e105edee946f739ede45ec40094167d`;
the resulting `native/cdk2-stage.elf` SHA256 is
`5f152ef18202b254594524f41b217c91d8d0e29b71a59fc43577d19d3867e713`.
The source remained clean at the same HEAD after the reader exited.

This result does not cover subsequent fixture/documentation commits and
does not establish protected-variable, hardware, capsule installation or
project sign-off.

The separate actual protected suite at CDK2 `8eadad29cc` was reaped with
exit 2: unit `cdk2-root-full-protected-after531`, invocation
`e9e3860f82204c6eba8639eae2f5413e`, 25m18.007s runtime,
25m48.361s CPU, 1.2G unit peak memory. Its retained raw log identifies
missing protected implementation dependencies in the generic legacy
`cdk2-variable-runtime-entry-test` recipe. A scoped test-family correction
is pending; this failure is not a protected-suite pass.

Only raw text receipts and this qualification are included; no firmware
image, variable media or private signing material is archived here.
