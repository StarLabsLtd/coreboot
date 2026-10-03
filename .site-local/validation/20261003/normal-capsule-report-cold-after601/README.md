# Normal cold CapsuleReport persistence after 601

Ready source: https://github.com/StarLabsLtd/cdk2/pull/601.
Two clean signed commits after ready600 `775298748124abe4db71d2a2a9ccf26efc9b6b94`:
source `499d6d4df1e8f5cf0377a959f1a5d8a338b3fc37`, then documentation
`f0e6f14cf6bb9335143f77ce7c134c29f6555912`.
The seven test bodies compare exactly to the actual tested signed WIP
`e13f187f87905f77736e7aa7f3efbc511b7ebccd`, based on ready599
`8276b68eea49ee0620e4fdcdd49bb2386e0bc7cf`. The two source archives preserve
that relationship; no original native input map is rewritten to a later head.

Both fresh native runs used actual corrected PR599 normal Core
`ed0e1cb0b413f2eb32ff1f6665cde9588ef3442d9ec7318da44be6557ed056a7`
and the successful corrected warm wrong-signer donor at
`/home/sean/normal-capsule-report-corrected-producer.xexfMA/wrong-signer/run-1`.
That warm source is `aa7801b3`, not the later ready600 metadata. Genuine producer
387 source is `d960d256e2c3c14ff4aa46d6443ba20a2fab77f7`; its real store decoder,
public config and matching cbfstool are used, not an inferred byte oracle.
SystemFmp is enabled, QEMU_TEST_FMP and the acceptance driver are disabled,
and the real protected variable route remains enabled.

| Actual run | QEMU seconds | GNU elapsed / user / system | Outcome |
| --- | ---: | --- | --- |
| Author session 79313, E64Ju6 | 7.076000814002327 | 11.48 / 10.24 / 1.34 | guest 3, failure null |
| Independent root session 49252, tXXQHL | 8.180112 | 13.31 / 11.81 / 1.61 | guest 3, failure null |

Both outer invocations actually returned 0. The independent recipe differs
only in its owned output root. Each recompiles the same small normal EFI MAIN
app using the actual native PE linker/relocation audit, copies the successful
warm pflash and disk, stages the app on its own disk, then records that staged
disk baseline. There is no pflash rewrite, payload substitution, capsule
resubmission, update operation, seeded state or post-signing image change.

The real ordinary MAIN validates canonical zero FmpState, report attributes 7
and fixed fields, Last attributes 7/exact 22-byte Capsule0000, Max attributes 6/
exact 22-byte Capsuleffff, and actual WRITE_PROTECTED results for both Max and
Last. Its expected capsule GUID is copied from the prior request before app
linking, not learned from the report; image GUID/index come from the real FMP
descriptor. Actual saved CBMEM shows one firmware-9/floor-9 normal boot,
successful RAM completion before PCI, no capsule handoff and no guest RESET.
MAIN observes absent CapsuleUpdateData and exits through real guest status 3,
within the unchanged 180-second budget.

The genuine producer decoder runs before and after cold boot, checks clean
FTW and canonical zero FmpState, then emits only the public 72-byte report and
22-byte Last. All 94 bytes compare exactly, including all 16 timestamp bytes.
These public standard report records are included; no whole variable-store
contents are copied. Firmware outside SMMSTORE and the staged disk remain
byte-identical. Normal monotonic-counter reservation legitimately updates
SMMSTORE, so unchanged whole-pflash is neither required nor claimed. Actual
final media hashes are in result.json and are not equality-to-input claims.

Original author root: `/home/sean/capsule-report-cold-final.E64Ju6`.
Original independent root: `/home/sean/capsule-report-cold-independent.tXXQHL`.
Raw command/result/QMP events, public saved tables/console, immutable maps,
actual app/codec input hashes, compiler logs and producer/config identities
are copied. The author saved-only checker separately verified all 17,024 live
immutable input hashes after the native reap; it does not launch a guest.
The packet checker validates copied saved receipts only. Seven modeled HOST
checks on the unchanged restack and project patch style also pass; those are
not fresh native runs. Earlier real PE/codec preflight logs under `preflight`
used prior FCq warm media, and are not relabeled as final corrected proof.

No ROM, disk, full capsule, executable, private key, TPM state, socket, whole
build tree or whole variable-store dump is included. No new whole PR600/601
Core build, hardware, Linux, SET failure history, below-floor recovery,
power-loss or atomic multi-variable transaction guarantee is claimed.
