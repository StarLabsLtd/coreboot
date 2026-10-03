# Corrected standard CapsuleReport warm native receipts

Two actual root-owned normal Core RAM runs use ready 599's corrected report
source `8276b68eea49ee0620e4fdcdd49bb2386e0bc7cf`, actual Core ELF SHA256
`ed0e1cb0b413f2eb32ff1f6665cde9588ef3442d9ec7318da44be6557ed056a7`,
and genuine producer 387 `d960d256e2c3c14ff4aa46d6443ba20a2fab77f7`.
The actual observer source is signed
`aa7801b3d881dee88d0d4737571b8d2cec356a71`; the four observer bodies are
archived at that exact revision. Ready 600 `775298748124abe4db71d2a2a9ccf26efc9b6b94`
has the same whole tree as the later documented observer tip b19fa7, but
the actual runs are not relabeled as having run at that later Git metadata.

Both initial9 and targetA were produced with the genuine normal Core payload
before capsule signing. Both retain SystemFmp enabled and QEMU_TEST_FMP
disabled. The ordinary MAIN app uses the real Runtime staging route, then
the real warm Core graph performs CHECK, CLOSE, report publication, exact
request retirement and normal MAIN continuation. There is no post-signing
or post-write payload substitution. The fixed native budget is 180 seconds.

| Case | Actual QEMU seconds | GNU elapsed / user / system | Result |
| --- | ---: | --- | --- |
| Wrong signer | 74.5415926650021 | 76.62 / 76.68 / 3.86 | guest 3, failure null |
| Signed-byte corruption | 73.7037858750009 | 75.59 / 75.86 / 3.48 | guest 3, failure null |

The precise signed-byte QEMU value is bound by result.json; the numeric table
must agree with that raw file. Both outer invocations returned 0. These are
guest continuation results, not historical HOST-terminated refusal tests.
The captured real CBMEM epochs include the signature-refused diagnostic,
successful RAM end before PCI and ordinary MAIN's report validation. The
genuine Runtime reads retrieve all 72 Capsule#### bytes, validate attributes 7
and the fixed fields (the 16-byte informational timestamp is not compared to
a known time by this warm observer),
CapsuleLast attributes 7 and exact 22-byte UTF-16 name, and CapsuleMax
attributes 6/default FFFF plus its installed write lock. The report's
failure remains the original EFI_DEVICE_ERROR, not authenticated success.
FmpState remains canonical zero. Actual CLOSE-before-report ordering is
source-backed and checked by the lifecycle; there is no dedicated producer
CLOSE UART marker being claimed.

Raw logs, QMP result/events, public CBMEM text/metadata, initial/final input
hash maps, packaging scripts/manifests and actual app compile receipts are
copied. No ROM, disk, capsule, executable, physical-memory dump, private key
or variable-store contents are copied. The input hash maps and producer
codec oracles bind their original external files. Source/tools and disk
are unchanged; full COREBOOT/probe/gap and every byte outside SMMSTORE are
unchanged. SMMSTORE may legitimately change for reports and normal runtime
bookkeeping. The final whole-media hashes (not an unchanged-media claim) are:

- Wrong signer: `7132e49339eebd0bf517b4426a5d9f39844c22a349e8b52cea7963889c87c652`.
- Signed byte: `e348a0fa5770a3abc63069746ffc8ccb86e2b3850ce97b5111512ea33c7a9e9d`.

Original external root:
`/home/sean/normal-capsule-report-corrected-producer.xexfMA`.
The earlier 477c/Core22f/FCq native pass remains separate prior-source evidence.
An initial archive command guessed a nonexistent runner filename and failed
128 before producing a usable archive; the corrected archive uses the actual
tracked system_fmp_core_ram_runner_test.py, without source changes.

Normal cold persistence and Last locking are separate receipts, not part of
these two warm runs. No below-floor recovery, SET failure history, Linux boot,
hardware or power-loss/atomic report transaction guarantee is claimed.
