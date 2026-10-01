# Protected policy consumer: PR472 source and checks

Publication branch: `agent/runtime-consumer-publication-after-tpm-wire`.
The signed, independently reviewed sequence is:

1. `664c2f5d4e00ce4be8250a269ba5e2844594078f`: complete binding,
   retirement and native crypto/source closure mechanism.
2. `60f6bd5d97fd726203fb5e625a669ec11de19741`: real initialized-entry
   policy-router joins.
3. `16750e753472e2bf4e9de54a7705cc1dff8bffd8`: retirement guard causal tests.
4. `923c157409d294d396896a64c85981e19d9e0ab9`: actual DxeCore LoadImage
   join with entry-installed policy.

The complete publication tree exactly matches independently tested integration
head `f8e678645f`. This is tree parity, not a new test run at a renamed head.
The combined mechanism also passed its own standalone selected native closure
without needing any subsequent test commit.

## Actual reaped results

| Raw log | Actual completion | Coverage |
| --- | --- | --- |
| `cdk2-policy-entry-retire-recovery.log` | session 79418, exit 0, about 3m44s | Author entry/lifecycle O0/O2 and compiled causal negatives |
| `cdk2-entry-537-peer-recovery.log` | session 26260, exit 0, 5m26.483s including lock wait | Independent initialized-entry replay; 184 PASS rows, eight causal rows |
| `cdk2-policy-entry-core-join-recovery2.log` | session 9173, exit 0, 4m17.715s | Author actual core-loader join |
| `cdk2-entry-core-peer-isolated.log` | session 51170, exit 0, 4m28.647s | Independent isolated replay; 186 entry, four core and ten causal PASS rows |
| `cdk2-runtime-integrated-f8e-focused.log` | session 99896, exit 0, 17m55.571s | Integrated selected PE/config isolation, fresh joins, canonical/core/DXE/security/variable, FAT and SMM checks |
| `cdk2-runtime-mechanism-664c-standalone-closure-initialized.log` | session 57839, exit 0, 1m47.942s | Complete mechanism alone: selected VariableRuntimeDxe PE/native ELF and source/cache/config isolation |

Author and peer initialized-entry raw logs are byte-identical; they are retained
as separately reported invocations rather than counted as different coverage.
Raw PASS rows are not a project-completion percentage.

The initial core join failed to compile because its test-only BUILD_DEBUG
selection was missing. `cdk2-policy-entry-core-join-recovery.log` preserves
that failure; it is not a pass. The reviewed correction adds the actual test
selection, not a production stub or warning suppression.

The first standalone mechanism checkout had not initialized its pinned BearSSL
submodule. `cdk2-runtime-mechanism-664c-standalone-closure.log` records the
actual exit 2 (session 23165, 3.136s) for missing `bearssl.h`. Initializing the
exact pinned donor, without a source patch, preceded the completed replay.

## Scope

The genuine policy/Auth2 fixtures, actual DxeCore TU and compiled causal
negatives cover mechanism and integration semantics. Entry/core/Auth2 joins
execute on the host with modeled IRQ state, HOB input and trigger delivery;
selected PE/native ELF closure checks are builds, not native execution. These
are not a native SMI or arbitrary-OS platform admission test. The protected native boot guard remains
held. Actual Q35 missing-endpoint refusal and media/SMM persistence are separate
component checks with separately identified firmware images. Hardware, full
native service activation and project completion remain unproven here.
