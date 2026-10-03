# Cold local-floor CapsuleReport persistence after 608

Ready source: https://github.com/StarLabsLtd/cdk2/pull/608, clean signed
`fe860fae35156a8bda41faba0c99669a26c4cb49` after ready607
`2e494c7c518bece433fcf174483f06f27b3c57ad`. The six fixture bodies first
ran at signed `1b56a168855a85df762b4269a76a22e2e641ab5c` after ready606.
Their exact restack is `28a504b2`; signed `b4420149f4c534c884a79e383a25ed04108cc44e`
adds only the terminal-read correction and its test. The last commit changes
ROADMAP only. Three signed source archives preserve the original failed
source, actual successful source and ready source separately; original input
maps and command recipes are not rewritten to later metadata.

| Actual invocation | QEMU seconds | GNU elapsed/user/system | Outcome |
| --- | ---: | --- | --- |
| First capture, 66549 / 2qHfAW | 4.388199244000134 | 8.99/7.82/1.23 | outer1, guest3, terminal QMP read failure |
| Fresh retry, 75887 / Aowjcu | 7.340451230997132 | 11.75/10.31/1.41 | outer0, guest3, failure null |

The first failure remains a failed HOST capture. Its accepted table/console
and serial show the ordinary cold MAIN pass, but its all-checks completion
did not run and no after94 record exists. The second invocation recompiles
the genuine small EFI MAIN application, copies the same successful warm donor
into new owned media, stages that application on its own disk and records the
staged baseline before QEMU. It uses the unchanged 180-second deadline and no
guest request or admission mutation. The correction tolerates only the actual
typed PhysicalReadError with OSError cause after accepted metadata and guest
exit3. Semantic/truncated/drift/preacceptance/live/non3 errors remain fatal,
and every strict saved oracle still executes. Eleven modeled HOST tests
exercise those conditions; the preceding ten-test source peer and final
eleven-test terminal peer are separately copied, not called fresh native runs.

The warm donor is the successful local-policy refusal at
`/home/sean/normal-local-floor-producer.uE8CdA/below-floor/run-1`, actual73096,
signed observer `35562cd0`. Its genuine production Core is immutable
`2612d65980d60f2a5020760ce5ea0d3edf6eb70904bb943192b7b4cf12e6c8f1`,
built at signed `b4b5dcf0` before PR602. Genuine producer387 source is
`d960d256e2c3c14ff4aa46d6443ba20a2fab77f7`; both firmware images contain that
Core before capsule signing. The cold runner checks actual loaded Core tuples,
resolved protected SystemFmp configuration, TestFmp/acceptance disabled, real
module inventory, matching PE audit/compiler/cbfstool and prior retained request.
It neither rebuilds whole PR608 Core nor changes producer trust.

Local mode requires exact EFI_UNSUPPORTED; the signature default retains exact
EFI_DEVICE_ERROR. Real installed services check report attributes7 and its fixed
fields, Last attributes7/exact22-byte Capsule0000, Max attributes6/exact22-byte
Capsuleffff and actual WRITE_PROTECTED results for both normal locks. Expected
capsule GUID comes from the prior request, and expected image GUID/index comes
from the real FMP descriptor, not the report itself. Canonical FmpState remains
zero with attributes3 and length20. The saved checked CBMEM chain proves one
firmware9/floor9 boot, no capsule handoff or RESET and RAM success before PCI.
Ordinary MAIN observes request absence and exits through real guest status3.

The genuine producer decoder uses the actual FV/FTW/store/record/UUID sources,
strict ASAN (including leaks) and UBSAN, before and after cold boot. It checks
canonical state/attributes/namespace and clean FTW, then emits only the public
72-byte report plus 22-byte Last. All94 bytes compare exactly, including all16
EFI_TIME bytes. The exact selected status is checked in both application and
decoder. Separate local and default signature decoder preflights each return0;
the opposite actual store is rejected with exact status1 and no sanitizer
diagnostic in each mode. Those are external decoder/PE preflights, not additional
guest cold boots. `check-opposite-modes.sh` records that actual invocation.

Firmware outside actual SMMSTORE and the staged disk remain unchanged. Normal
monotonic-counter persistence may update SMMSTORE; whole-pflash equality is
neither required nor claimed. Final installed hash is
`0e3faf89255b01ebde5ab2d3c8638e2dae72e62fdedaa8a79303f990d153d886`.
The successful staged/final disk hashes are both
`02a5b6faf368bb264b558b57e00b9f0934e795dfc9d03c9a8e97e84c6e76fb1e`.
The actual runner's before/after input maps compare equal; app and decoder
input fingerprints, source/config/tool identities, original GNU timings,
QMP events, console/table and serial records are copied. The packet checker
replays only copied saved semantics and report equality; it does not launch a
VM, recheck omitted media contents or manufacture historical outer statuses.
An independent saved-only peer at `local-floor-cold-saved-peer.qBq6Jo` returns0
in0.88 seconds and also rechecks the original external media and current input
hashes. Its actual script/log/time are copied; it is not an independent VM.

No ROM, disk, full capsule, executable/object, private key, socket, TPM state,
whole variable-store dump or build tree is included. The94 public standard
record bytes and public checked CBMEM/table captures are intentional. The
result proves bounded local version-policy report persistence, not CMS/MM
authentication, a SET/update writer, all profiles, hardware, Linux, arbitrary
power cuts or whole-project acceptance.
