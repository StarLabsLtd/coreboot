# Native Q35 missing-endpoint component

Reviewed test source: CDK2 `50898acd5b8b500d0e028fe04dd4451c656801ed`.
This executes actual entry32, the production ACPI/MCFG importer, real HOB
construction, DxeCore/SecurityStub initialization and protected variable entry.
It proves refusal of a missing GENERAL protected-service endpoint with unchanged
runtime hooks, live-pool count and installed-protocol count.

The linked native OUTB trigger is **not executed** in this missing-endpoint
test. The production protected-boot admission guard remains held.

## Actual reaped invocations

- First requester-layout attempt: session 21497, exit 2, 37.221s; firmware did
  not reach the payload. The initial failure log is retained, not a pass.
- Corrected requester topology: session 35393, exit 2, 7.766s; reached payload
  entry but refused absent ACPI/MCFG enrichment. Failure log retained.
- Real production ACPI importer: session 81300, exit 0, 8.390s; absence proof.
- Final test plus compiled final-status-discard negative: session 65978,
  exit 0, 10.173s; `run.vCeq4I` in the archive.
- Independent replay on the real new Q35 loader ROM: session 72832,
  exit 0, 9.833s; `run.hfPh73` in the archive.

Normal guest exits with the expected debug-exit status 3. The compiled mutant
changes only the actual entry's final returned status to EFI_SUCCESS after its
unchanged cleanup. It exits with status 5 and the explicit
`REFUSAL_STATUS_BAD` assertion; it is not a timeout or a fault counted as a
causal rejection.

## Firmware and archive identity

The independent replay's donor loader ROM is
`9b9848d421e9bd6363e3e3c00b4e266e0b1c156f905dec6445cd4fe6635485cd`.
The genuine loader prerequisite source is coreboot `4b830ad9463`, following
GENERAL capability source `138957872743bdf285f7bf39e964386b388f1745`.
This prerequisite enables topology/composition with actual single-CPU and
RDRAND evidence, not ENTRY or service-route authority.

The component ROM replaces that donor's payload with the real test component:

- `run.hfPh73/probe.rom`:
  `565067c737b8aeda2c128c2bd12e8fcd8b8194802abea2ff08b4df1cb5a8055f`.
- `run.hfPh73/probe.elf`:
  `8ab902bc373c05ecc4bb3d9b44a4ddd92f2a761efbd0fb6acb99de43cb3fb633`.
- `run.hfPh73/status-discard.rom`:
  `bc1a00a6926c647959081e790fdc21730d480dd571e35886a000095e67183153`.
- `run.hfPh73/status-discard.elf`:
  `29a720c530cc9e8ff1cc7c1a1f289e74c464e5b7a00e3e3fb79d2870556e2377`.

`guest-receipts.tar.zst` retains both final raw guest artifact directories,
including real ROMs, ELF files, source mutation, maps and serial/QEMU logs.
Its SHA256 is `66187c0f9a9ba8f6660ffbfc4bfe00750989d2585bbe37fd9793aa6d3dd737c4`.

This is native loader/refusal evidence, not a full normal-CDK2 guest boot,
authenticated native SMM-service positive, arbitrary-OS admission, hardware
validation or project completion.
