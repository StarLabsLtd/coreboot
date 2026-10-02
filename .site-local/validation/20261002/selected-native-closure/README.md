# Selected native service fixture closure

Signed, independently reviewed CDK2 `4ba3da5839202746c9137cde1e875064753ebc87`
(ready PR480) adds the real selected lifecycle source closure and calls its
normal initialization before variable entry. It does not disable configuration
symbols, substitute authority stubs or lift the production boot guard.

The donor is the independently fresh signed producer ROM recorded in
`../bounded-native-service`, SHA256
`605b3c0751b2ab8833a7ef3613e209d93335f272cccda2ca7a3d329b6a08139b`.

Actual managed executions:

| Configuration | Session | Exit | Wall / CPU |
| --- | --- | --- | --- |
| Selected before correction | 21144 | 2 | 8.696s / 8.872s |
| Selected corrected | 36957 | 0 | 18.077s / 18.504s |
| Default corrected | 36170 | 0 | 17.026s / 17.366s |

The earlier selected failure is a real unresolved lifecycle-symbol compile
failure, not a native execution pass. Corrected suites each execute four actual
VMs: positive authenticated-policy behavior, enrolled cold restart, genuine
unenrolled initialization, and the compiled CoreLoadImage CLI-discard negative.
The independent reviewer inspected the corrected selected serial receipts as
well as the exact source and both raw suite logs.

`receipts.tar.zst` SHA256:
`50a0e186bc56e8ecd4c7444f1c6b97fd8ff9224000857814c9d48b9f2b9a1ea3`.
It retains the three artifact directories and raw logs, including generated
source, binaries, disposable media and native execution output. This is a
controlled immutable-flash-trigger component result, not production RAM-trigger
activation, arbitrary OS runtime or physical hardware validation.
