# Star Labs CFR v1 preferences

With `PAYLOAD_MM_INTERFACE` and `STARLABS_ACPI_EFI_OPTION_SMI`, the matched
payload owns EFI variable writes and coreboot publishes a fixed ACPI NVS
mailbox for Star Labs preferences. Builds without payload MM retain the
existing coreboot option SMI. The bridge preserves the CFR v1 interface; it
does not add another OS-facing ACPI protocol.

The payload owns variable policy, allocation, reclamation and fault-tolerant
writes. coreboot owns EC and chipset transactions. The payload handler does
not access the EC. Before OS handoff, the matched payload closes the MM loader
and checks that the mailbox handler reports the mask published by coreboot.

## Mailbox

The payload MM interface table supplies the mailbox address, its 24-byte size
and the supported-option mask. The payload captures these values during MM
initialization. Runtime requests cannot replace the address or provide variable
names, GUIDs, attributes, lengths or pointers.

The mailbox contains six little-endian 32-bit fields:

| Field | Meaning |
| --- | --- |
| Command | GET=1, SET=2, supported-option mask=3 |
| Id | Fixed preference identifier |
| Value | Encoded preference or returned mask |
| Status | Pending=0xffffffff, success=0, error=1, invalid=2, absent=3, unsupported=4, denied=5 |
| Version | 1 |
| Reserved | Must be zero |

AML serializes requests with `EOMX`, initializes all fields, then writes command
`0xe2` to APM port `0xb2`. The MM handler copies the request, validates it
against its fixed option table and the published mask, and writes the value
before the completion status. A request left pending is not successful.

The current coreboot publisher advertises the applicable subset of identifiers
1-13 and 18. These cover the existing keyboard, trackpad-enable, charging, fan,
lid, LED, power-on and automatic-start preferences selected by the board. The
payload validates each value and requires an existing value to be four bytes
with non-volatile, boot-service and runtime attributes. Saving trackpad value
`0x11` normalizes it to zero, preserving the previous behavior.

## ACPI behavior

Linux CFR v1 writes the preference through EFI runtime services, then issues
APMC command `0xe3` for coreboot to read and apply it. It does not use the ACPI
mailbox methods or require a new driver interface.

For ACPI callers, `EORQ(command, id, value)` is the internal request helper
and returns a materialized `{status, value}` package. The existing scalar
compatibility methods remain: `EOGT` returns the stored value or
`0xffffffff`, `EOSV` returns status after saving and applying a preference, and
`EOMS` returns the supported mask. EC access occurs after releasing `EOMX`.

The sleep path reads live EC state with an explicit transport status before
saving trackpad, Fn-lock, keyboard-state and keyboard-brightness preferences.
It does not turn a failed read into a saved zero. Resume applies only values
that the variable service returned successfully and leaves the EC unchanged
for a missing or failed value. This persistence does not depend on an OS CFR
driver.

On Intel boards with `STARLABS_AUTOMATIC_START`, applying automatic start also
updates the chipset power-failure policy. The saved EFI value remains owned by
the payload service.

## Security boundary

The mailbox exposes ordinary mutable preferences from a fixed allowlist. It
does not provide arbitrary flash access, variable names, Secure Boot keys or
authenticated-variable policy. Privileged software can issue the same bounded
requests as AML; `EOMX` coordinates callers but is not an authentication
boundary. coreboot and payload MM remain in the same trusted SMM domain.
