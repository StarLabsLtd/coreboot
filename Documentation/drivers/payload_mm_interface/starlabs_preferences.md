# Star Labs ACPI preferences

With `PAYLOAD_MM_INTERFACE` and `STARLABS_ACPI_EFI_OPTION_SMI`, the Star Labs
ACPI option interface uses the resident payload variable service. coreboot
does not maintain a second writer for the same EFI store. Builds without
payload MM retain the existing option SMI implementation.

Enable `STARLABS_ACPI_EFI_OPTION_SMI` explicitly for a payload MM build
only when its payload implements this preference service. Generic MM support
does not imply support for this protocol. With the option disabled, coreboot
does not publish the mailbox, preference methods or EC runtime apply records.
The mailbox and ACPI consumers below describe the enabled configuration.

This is a restricted preference interface, not arbitrary EFI variable access.
The payload owns variable policy, allocation, reclamation and fault-tolerant
writes. ACPI owns EC transactions and, where supported, touchpad transactions
through the OS GenericSerialBus handler. The payload handler performs no EC or
I2C transactions.

## Mailbox

coreboot allocates a 24-byte ACPI NVS mailbox and describes its address, size
and supported-option mask in the payload MM interface table. The payload
captures these parameters during initialization. Runtime requests cannot
replace the mailbox address or supply pointers, variable names, GUIDs,
attributes or lengths.

The mailbox consists of six little-endian 32-bit fields, in this order:

| Field | Meaning |
| --- | --- |
| Command | GET=1, SET=2, supported-option mask=3 |
| Id | Fixed preference identifier |
| Value | Encoded preference or returned supported-option mask |
| Status | Pending=0xffffffff, success=0, error=1, invalid=2, absent=3, unsupported=4, denied=5 |
| Version | 1 |
| Reserved | Must be zero |

ACPI serializes access with `EOMX`, initializes every request and status, then
writes command `0xe2` to APM port `0xb2`. The MM handler copies the request
before validating it, uses its fixed option table and captured platform mask,
and publishes the value before the completion status. It does not follow
caller-owned pointers. An unconsumed pending request is not success.

Supported mailbox IDs are 1-13 and 18: function lock, trackpad enable,
keyboard brightness, backlight enable, backlight timeout, Fn/Ctrl swap,
maximum charge, fan mode, charging speed, lid switch, power LED, charge LED,
AC-connect power-on, and automatic start. Each platform advertises only its
applicable subset. Values retain existing Merlin/CFR encodings. The payload
checks their allowed values and requires a four-byte EFI value with exactly
NV, boot-service and runtime attributes. Invalid existing values are not
returned as valid preferences. Trackpad state `0x11` is normalized to zero
when saved, matching the previous persistence behavior.

## ACPI consumers

`EORQ(command, id, value)` returns a materialized `{status, value}` package.
`EOGT`, `EOSV` and `EOMS` retain their scalar compatibility interfaces.
The mailbox mutex is released before EC access; no mutex is held for the
lifetime of an OS-visible result.

The firmware sleep path reads live EC preferences through `ECGT`, which
returns transport status separately from the value. Failed reads are not
saved as zero. S3/S5 persistence does not depend on an OS CFR driver.
Resume applies only preferences that the variable service returned
successfully. `ECWR` returns the outcome of its bounded write/readback loop.

Runtime CFR metadata selects the ACPI apply method for MM builds.
`\CTBL.CFRA(id, value)` applies the already-saved preference and rejects an
EC request if the stored preference no longer matches the expected value.
`\CTBL.CFRG(id)` reports live state with an explicit status. Saving a
preference and applying it are separate operations: apply failure is not
evidence that the variable write was rolled back. An OS consumer must check
both results and handle rollback explicitly.

Touchpad IDs 14-17 use the board's ACPI GenericSerialBus backend, not this
mailbox. They are advertised at runtime only when that backend exists.
The PixArt backend needs hardware validation including concurrent input and
suspend/resume. CST controls remain boot-time-only under MM until their ACPI
transaction contract is established.

## Security boundary

These are ordinary mutable preferences. Privileged OS code can issue the
same bounded requests as ACPI; the interface does not authenticate an AML
caller or prove physical presence. AML mutexes coordinate cooperative
callers, not malicious ring-0 code. No arbitrary flash, Secure Boot key,
authenticated-variable or security-policy access is exposed by the mailbox.

coreboot and payload MM share the trusted SMM domain; this is not isolation
between them. Secure Boot policy and the generic MM communication interface
must be reviewed independently. Removing the board's competing SMM writer
does not by itself establish the security of the complete MM implementation.
