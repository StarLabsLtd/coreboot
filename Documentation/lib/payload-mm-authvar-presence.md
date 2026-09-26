# Payload-MM authenticated-variable presence ABI

This is a dormant revision 1 wire contract for one narrowly scoped physical-
presence action: `ENTER_SETUP_MODE`. It defines shared structures and structural
validators only. It does not produce a coreboot table record, generate a
capability, install an SMI route, add a Kconfig option, or link a firmware
object.

## Threat model and boundary

The payload and operating system are hostile. The mailbox must be fixed and
reserved for the complete boot, protected from DMA before it is exposed, and
handled by coreboot-owned SMM with all CPUs rendezvoused. The endpoint's
generation binds requests to one boot. The request carries an opaque 256-bit,
one-shot capability. Capability creation, protected delivery, consumption,
replay state, and the physical-presence policy are deliberately outside this
ABI slice.

No capability or other secret is present in the coreboot table. The endpoint's
`capability_size` field describes only the capability field in the mailbox
message. The table exposes no variable name, vendor GUID, variable data,
SMRAM address, media geometry, flash operation, or generic variable service.

This ABI alone makes no security claim. Its flags and validators describe the
conditions a future producer and consumer must prove; they do not establish
those conditions merely by existing.

## Endpoint record

`LB_TAG_AUTHVAR_PRESENCE_ENDPOINT` is `0x0056`, separate from the revision 1
authenticated-variable service endpoint. The packed endpoint is exactly 64
bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | `tag` |
| 4 | 4 | `size` |
| 8 | 2 | `revision` |
| 10 | 2 | `header_size` |
| 12 | 4 | `flags` |
| 16 | 8 | `generation` |
| 24 | 8 | `communication_base` |
| 32 | 4 | `communication_size` |
| 36 | 4 | `message_size` |
| 40 | 2 | `transport` |
| 42 | 2 | `trigger_width` |
| 44 | 4 | `trigger_address` |
| 48 | 4 | `trigger_value` |
| 52 | 4 | `action_scope` |
| 56 | 4 | `capability_size` |
| 60 | 4 | `reserved` |

Revision and header size are exact. Required flags assert coreboot SMM
ownership, fixed communication, DMA protection, CPU rendezvous, one-shot
capability semantics, and an implemented mandatory lifecycle seal. The
`LIFECYCLE_SEALED` flag does not mean that a freshly published endpoint is
already closed; it asserts that the installed authority will irreversibly close
the open endpoint before external code can execute.
The generation and aligned communication base are
nonzero. The address range must not overflow either the wire address or native
address space. Communication size and message size are both exactly 80 bytes.
Transport is the eight-bit APM I/O transport, its trigger address and value fit
that transport and are nonzero, action scope is exactly `ENTER_SETUP_MODE`,
capability size is exactly 32 bytes, and the reserved field is zero.

## Message

The fixed, eight-byte-aligned request/completion message is exactly 80 bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | `revision` |
| 4 | 4 | `size` |
| 8 | 4 | `action` |
| 12 | 4 | `flags` |
| 16 | 8 | `generation` |
| 24 | 8 | `request_id` |
| 32 | 32 | `capability` |
| 64 | 8 | `status` |
| 72 | 4 | `reserved` |
| 76 | 4 | `completion` |

A request uses revision 1, exact size, the endpoint's generation and action
scope, a nonzero request identifier, zero flags and reserved field, a
non-all-zero opaque capability, `UINT64_MAX` pending status, and `UINT32_MAX`
pending completion. There are no inline names, GUIDs, data, offsets, or
pointers.

A completion byte-for-byte echoes revision, size, action, flags, generation,
request identifier, and all 32 capability bytes. It keeps the reserved field
zero and changes completion to zero only after status is final. The closed
status domain is success, unsupported, device error, write protected, access
denied, or security violation, using fixed 64-bit UEFI-compatible values.

An `ENTER_SETUP_MODE` success unconditionally requires an immediate cold reset.
Consequently the message has no `reset_required` field and no successful caller
may continue executing the current boot.

Structural validation neither consumes the capability nor authorizes the
action. A future protected executor must atomically check and consume the
one-shot capability, enforce the action scope and physical-presence policy,
perform the state change, publish completion last, and cold-reset immediately
on success.

## Host validation

`tests/lib/payload_mm_authvar_presence_abi_test.sh` runs strict `-O0` and `-O2`
builds, ASan plus UBSan at both optimization levels, deterministic field and
capability-byte rejection cases, and validator mutation tests. It also proves
that the public header contains no generic variable/private-media vocabulary
and that no firmware Makefile links the dormant validator.
