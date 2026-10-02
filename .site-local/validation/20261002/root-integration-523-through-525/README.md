# Protected setup and ABI/style integration receipts

These receipts supersede matching outstanding checks in the earlier packet;
they do not establish complete project or hardware validation.

- Root `debb6d6ad4` passes the combined seven-boot cold MAIN authorization
  suite. The managed unit takes 1m52.343s, not a hardware boot-time measurement.
- Root `165269f51b` builds the native protected setup image, packages it using
  actual coreboot tools and passes the complete three-boot GUI suite. Hotkey
  entry, navigation, OS-requested setup, logo/BGRT restoration and Linux
  completion are checked. The final full 64 KiB protected-store oracle and
  its 86 mutation checks pass. Unit total is 2m18.639s.
- The later test-only protected-store cleanup passes the captured-media HOST
  oracle and whole source lint. This is not another native boot receipt.
- Root `ecce0258a8` passes its focused selected protected configuration gates
  with actual producer import and unchanged caller config/header hashes.
  Earlier failed invocations are retained: one requested an unavailable outer
  forwarding target; another exposed inherited fixture config-header state.
  No failed invocation is counted as passing or as a whole regression.
- The qualified quiet baseline archive binds author and independent native
  three-boot receipts at `cb59ad50fa08`. Firmware UART remains disabled and
  the observer reads actual published coreboot tables and CBMEM. The initial
  early-publication failure is retained. This baseline still hides successful
  LVGL phase text behind DEBUG; its follow-up is not complete in this packet.

`SHA256SUMS` binds raw logs and the qualified quiet packet. These are bounded
source and QEMU/HOST checks. The whole combined regression, typed authenticated
firmware install/restore, DMA, remaining style and hardware are still open.
No private signing keys or mutable guest flash images are included.
