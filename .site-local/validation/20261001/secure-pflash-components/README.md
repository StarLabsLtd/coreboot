# Actual QEMU secure-pflash write boundary

Reviewed, signed source: CDK2 `9f7fb4bc96`, unchanged linear cherry
`97835a23de` (PR475). This fixture runs only on disposable QEMU copies.
It uses the real published EFI FVB protocol for store geometry and the genuine
legacy FVB/SMM callback for its allowed write, not a mocked media provider.

Baseline ROM SHA256:
`f3118dda1eb1560ca009b0a9b1536b293e992c9ffe087a95ebdee0c407990206`.
Baseline firmware is the PR465 compatibility composition, not the newly added
protected native service or the current reader's rebuilt ELF.
Input NVMe SHA256:
`054a451e67b291adc7390301ef21c9df2de3582e768612cc66459e58d1305e36`.

## Actual results

Root retry session `14400` actually reaped exit 0 in 19.794s (20.768s CPU).
Independent reviewer replay session `49410` actually reaped exit 0.
Each invocation runs both controls:

- Secure off: actual non-SMM byte programming changes blank `0xff` to `0x7f`.
- Secure on: the same non-SMM program sequence leaves the byte at `0xff`.
- Both: the real FVB/SMM write changes it to `0x3f`; the fixture reads it back
  and the host independently observes exactly decimal 63 in the pflash file.

The real protocol reports physical byte address `0xff87ffff`; for this 8 MiB
ROM the persisted file offset is 524287. Both guest exits are exactly debug-exit
status 1. The wrapper requires the matching markers and exact byte checks;
guest exit alone is not accepted. Original ROM and boot image hashes remain
unchanged.

QEMU is the installed 10.1.0 build. The secure property uses the actual long
form `-global driver=cfi.pflash01,property=secure,value=on`; the driver contains
a dot and cannot use the abbreviated driver.property syntax here. There is no
monitor or QMP channel for externally injecting a competing SMI.

## Failure preservation and proof limits

The initial wrapper actually returned 1 (session 2450, 10.304s): abbreviated
global syntax named the wrong class, and the debugcon-only markers were absent
after the firmware transition. Its raw guest outputs are preserved, never
counted as a pass. The accepted replay uses the long property syntax and COM1
markers; exact whitespace-normalized numeric host comparisons replace the
reviewed too-broad suffix match.

This proves a QEMU media restriction against non-SMM writers. It also explicitly
demonstrates that SMM can still modify the medium. The donor's legacy full-flash
SMM writer is enabled, so this is not immutable ROM, authenticated-variable
authorization, arbitrary-OS entry admission or protected-service activation.
No hardware was accessed.

`guest-receipts.tar.zst` retains root failed/successful and independent successful
raw logs, statuses, EFI programs, objects and resulting flash copies. Input hash
lists identify the immutable external boot image without duplicating its 128 MiB
contents. Archive SHA256:
`49f7b361c964402c6227f48ddfcd77c370f82f65294cc0d5f752c34a3527a66b`.
