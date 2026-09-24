# Payload-MM authenticated-variable default store

The dormant default-store composer creates the exact persistent SetupMode
defaults produced by EDK2 26.09 `AuthVariableLibInitialize()`. It is a pure
byte transformation: it allocates nothing, invokes no callback, reads no
media, and publishes no service.

The composer first uses the shared authenticated-variable FV formatter. It
then appends these canonical `ADDED` records in EDK2 initialization order:

| Variable | Attributes | Timestamp | Data |
| --- | --- | --- | --- |
| `CustomMode` | NV + BS | zero, validated | standard mode (`0`) |
| `certdb` | NV + BS + RT + time-authenticated | trusted zero | list size (`4`, little-endian) |
| `VendorKeysNv` | NV + BS + time-authenticated | trusted zero | vendor keys valid (`1`) |

`SecureBootEnable` is absent because an empty store has no platform key and is
therefore in SetupMode. `SetupMode`, `SignatureSupport`, `SecureBoot`,
`certdbv`, and `VendorKeys` are volatile projections and are not stored in the
persistent image.

## Source classification

Classification covers the complete SMMSTORE region, including the active FV,
working block, spare area, and every erased tail byte:

- `ERASED` means every source byte is `0xff`.
- `COMPLETE` means the source is byte-identical to the full three-default
  target. It never means merely formatted.
- `NOR_SUBSET` means the source is neither erased nor complete and every byte
  can reach the target using only NOR `1` to `0` transitions:
  `(source_byte & target_byte) == target_byte`.
- `FOREIGN` means at least one source zero bit would have to become one.

Consequently, a canonical formatted empty FV is a `NOR_SUBSET`, while any
non-erased working, spare, or target-tail byte is `FOREIGN`. The classifier
does not scan or interpret an incomplete store.

For every structurally valid call, including `FOREIGN`, the output buffer is
the same deterministic candidate. A caller must consume it only after an
accepted classification. Invalid arguments leave the output untouched.

This component is not a bootstrap executor. A later same-lease FTW integration
must recover interrupted NOR programming, validate readback, and decide when a
candidate may be written. Until then the composer remains unreachable from any
service, provider, SMI route, or coreboot-table record.
