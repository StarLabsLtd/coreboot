# Bounded cleanup, refusal-client and UART receipts

Signed, independently reviewed CDK2 source slices:

* PR587: `81478b02d4dd02ff6a98d749c1784842109aefbe`, remove uncalled executable-FV discovery. Variable FV/FVB codecs remain.
* PR588: `b6f37a6f095c5e541644cda3412b590d1cce04ce`, accept precisely classified private CHECK refusal replies; public FMP still returns EFI_DEVICE_ERROR. No boot continuation or retirement was implemented by that slice.
* PR591: `bad527ca4db51ca1750389c15832f687851b69d6`, delete unreferenced standalone compact emitter. Actual compact emission remains in diagnostic.c.
* PR592: `f9201738089d13a4a1ba19998ce6e99938ad2bc3`, validate complete logger UART spans, retain valid context after primary rejection, honor MMIO width, remove implicit COM1 fallback and obsolete build defines.

The finite snapshots contain only the changed files still present at each signed tip, except the explicitly named PR591 parent snapshot of its deleted file; they are not transitive source/dependency closures. Source histories remain in the published CDK2 branches. Logs and scripts retain their original absolute worktree paths; these receipts are not a self-contained rebuild distribution. No executable, object, disk, ROM or private key is included.

FV named retry and final-head gates returned 0; the first named invocation returned 2 because it requested a target not exposed by the top Makefile. Its separate genuine-config FV-protocol fixture and four malformed-header fixture checks passed. The header fixture resets restored the original bodies. The compact deletion's normal stage/CBMEM/ATA gates returned 0.

The refusal-client final focused gate returned 0, including actual session/coordinator and strict O0/O2 sanitizer transport cases. Separate unchanged-parent consumer testing confirms that code 2 was previously BAD_RESPONSE. These are HOST protocol/transport tests, not a native MM refusal-recovery boot.

UART quiet named gate returned 0 in 21.63 seconds; separately, normal Kconfig resolution with CDK2_DEBUG selected enabled DEBUG, BUILD_DEBUG and SERIAL and its named gate returned 0 in 25.36 seconds. Both gates cover stage/coreboot/CBMEM/entry. Native wide UART callbacks are compiled, not claimed hardware-executed. HOB callbacks are actually exercised on aligned 16/32-bit buffers, byte access with stride four, full-width writes, and preservation of the previous sink after malformed replacement. PIO/address-span boundary assertions do not issue privileged HOST port accesses.

Diagnostic tests passed strict O0/O2 ASan/UBSan under genuine previously resolved default/protected configurations (3.70 seconds). The script identifies those existing generated headers; their copies are included for identification, not falsely described as new configurations. Independent runtime callback tests passed O0/O2 and sanitizer checks, including a second run against the fresh UART quiet header. The retired CBMEM page is protected against access. No new QEMU boot or hardware result is claimed by this packet.

UART initial failure logs are retained: one run reached the fixture while its obsolete counter calls had not yet been replaced; another fixture retained previous CBMEM and used unlabeled DEBUG messages, so the quiet profile suppressed expected UART writes. Tests were corrected to explicitly retire previous sinks and emit INFO records; production policy and output assertions were not weakened. Subsequent gates are listed separately.

This packet does not close SerialIO's separate register-6 consumer audit, capsule OS continuation, durable CapsuleReport history, whole-project lint or release/hardware validation.
