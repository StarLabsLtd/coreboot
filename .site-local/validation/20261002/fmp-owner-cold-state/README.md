# Protected FMP cold-owner prerequisite

This packet covers a default-off controlled Q35 composition that derives the
FMP state namespace and trusted lowest-version floor from the compiled initial
coreboot firmware during the actual private cold-bootstrap claim. The existing
AUTHVAR_BOOT owner reconciles its combined state through the real protected
variable executor. No capsule broker endpoint or firmware writer is installed.
The initial firmware is trusted; there is no live-image cryptographic
attestation or hardware rollback-resistant version claim.

Signed source identities:

- Coreboot generic store-policy correction: `bb2e7dbf584c767b3c035cdcaab9dde6542dcbea`.
- Coreboot Q35 owner composition: `104ff6d6fa8863fdc5c069835260e925befb40f6`.
- CDK2 native two-cold-boot observer: `b09f1f02b4120cda7a1b34dfff19553cabd13fbc`.

Actual completed receipts:

- Author declared incremental corrected ROM build, session 49108: exit 0.
- Independent fresh declared full ROM build, session 29992: exit 0, no forced
  object prerequisite workaround. Its ROM matches the author's
  `fe0517e9c05741b5459e7ea66db8ec2929ca205b49c1c51a5e905a8c8352450b`.
- Author normal named native gate, session 29422: exit 0, 11.940 seconds wall.
- Independent normal named native gate, session 68934: exit 0. Both run two
  genuinely fresh QEMU VMs on the same writable secure-pflash media. Each reads
  the actual MM combined FmpState with attributes 3 and exactly 20 zero initial
  state bytes; the second boot leaves the whole first-boot media unchanged.
- Author focused small-store gate, session 17102: exit 0, 30.499 seconds wall.
- Independent focused gate, session 17165: exit 0. O0/O2 execute a valid
  endpoint maximum-data limit larger than the actual recovered store, then
  compile an exact single FMP-clamp-discard mutant requiring the targeted
  assertion exit 134 without any sanitizer fault. Exact inverse reconstruction
  must match the whole original translation unit.

The real native failure revealed an inherited FMP write-policy inconsistency:
the record limit was narrowed to the actual recovered store but the data limit
remained larger. The canonical writer correctly returned INVALID_PARAMETER.
The correction uses the existing ordinary SET header guard and bounded data
clamp; no writer or protected-span validation was weakened.

Preserved failures include missing real UUID/hex conversion link closure,
native cold-bootstrap fail-stop/reset loops before the correction, a focused
missing service TU link failure, and an initially invalid test limit rejected
by the genuine executor-size contract. These are not successful native proof.
The debug serial trace is diagnostic only; deliberate platform reset failure
must not be labelled a proven CPU triple fault. Debugger observations changed
no guest register, memory or guard.

`coreboot-source` and `cdk2-source` contain the exact signed changed source
bodies. `author` contains the resolved configurations, corrected producer ROM,
actual observer ELF, both cold serial logs and aggregate receipts. `peer`
contains independent full-build/native/host raw logs. `failures` retains failed
receipts. No private certificate key material is included.

Still open: authenticated typed FMP provider and bounded media installation,
firmware-version authority after a real install, capsule back to original
content at an admitted newer version, and hardware validation. The opaque UX
RAM reset/population proof from the earlier packet is separate.
