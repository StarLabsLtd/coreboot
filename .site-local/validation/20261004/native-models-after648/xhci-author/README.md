# xHCI private-model recovery gates

Reviewed candidate base: 8137157963fe88cb3ca9b3f9a1662e3c5bbaf41c.
Exact ten-file reviewed diff SHA256:
e0c92a5156b30e616a7f18252f157bb51807a1d054cb4ff3f931c8eaa0be7833.
Prior temporary receipts were lost on host reboot; these are fresh results.

Default and O0/O2 existing four-test suites return zero. Each existing recipe
uses ASAN/UBSAN; O0/O2 runs retain Wall/Wextra/Werror. Default diagnostic parity
returns zero, including its required xHCI PE link dependency. Final aggregate
default suite + parity replay returns zero. Genuine defconfig headers match
baseline/candidate; final default config/header before/after hashes match.
Before/after tracked source/config-input/tool-binary manifest matches exactly.

Strict Wpedantic/Wconversion/Wsign-conversion suites return 2 on candidate and
baseline at inherited model narrowing diagnostics. These are not passing gates.
Changed-line patch checkpatch returns zero; nine whole changed files return zero.
Candidate whole-header checkpatch returns 1 at six unchanged EFI UINT8-pointer
lines; baseline whole-header returns zero. This scanner-context regression is
not hidden or described as a whole-header lint pass. No flags were weakened.

Initial baseline object commands return 2 due to incorrect top-level dispatch;
their comparisons return 2. Corrected CDK2_CONFIG_READY=1 commands return zero.
Whole model/controller/diagnostic objects match exactly. PCI adapter, usb2_abi,
and entry objects differ. Typed EFI output temporaries explain PCI adapter
changes. Private usb2 state changes from UINTN to uint32_t, shrinking its private
structure by eight bytes (539528 to 539520), shifting async offsets by eight.
Entry disassembly reflects allocation size and async offsets only. All other
listed structures have identical size/alignment. Installed USB2 protocol and
callback header block matches byte-for-byte; its listed offsets and 112/8
size/alignment match. Private-layout comparison consequently returns 1.

The broad port-status-to-protocol text comparison returns 1 because it includes
private model field substitutions; the precise installed USB2 block returns 0.
All actual statuses and original logs remain in status and adjacent files.
No hardware/VM/Core firmware validation, 32-bit portability, whole-project lint,
or full-release completion is claimed. Source has not been changed during gates.
