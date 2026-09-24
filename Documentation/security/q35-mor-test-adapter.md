<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Q35 MOR test-adapter boundary

Q35 is the hardware-independent validation target for the generic Payload-MM
memory-overwrite-request path.  It must not grow a second MOR implementation.
The eventual adapter may supply only platform facts to the shared path:

1. a protected cold-boot classification and nonzero boot generation;
2. an exact, revalidated VT-d policy identity and PCI requester inventory;
3. final bootmem DRAM provenance plus aligned page-table, aperture, plan and
   binding reservations;
4. the FMAP `SMMSTORE` geometry and a QEMU-pflash media implementation; and
5. protected SMM storage and a fixed private ramstage-to-SMM transport.

The PR210 base does not yet provide the interfaces needed to compose that
adapter.  In dependency order, the missing pieces are:

* a trusted SMM bootstrap which installs the authenticated-variable contract,
  executor arena and media port before accepting a private command;
* an owner-bound QEMU-pflash media lease.  The existing concrete backend is
  deliberately SPI-only and depends on `BOOT_DEVICE_SPI_FLASH`,
  `SPI_FLASH_VOLATILE_LEASE` and `boot_device_spi_flash()`.  Q35 exposes a
  writable region device instead, so selecting or imitating that backend would
  be a false validation;
* a platform-owned cold/S3 generation source;
* a fixed private completion-seal channel and authenticated SMM trigger; and
* the linear orchestrator which performs cold classification, DMA guard,
  read-only MOR probe, live inventory, memory clear, completion-seal install
  and terminal Control-bit clear in that order.

Until all five exist, Q35 must make no end-to-end MOR support claim.  A future
adapter must be default-off, select `SMMSTORE_READ_REGION`, and conflict with
legacy `SMMSTORE`: the read-only FMAP lookup remains available while the raw
`APM_CNT_SMMSTORE` mutation endpoint is absent.

`tests/lib/q35_mor_fixture.c` deterministically generates complete EDK2 26.09
variable-store images instead of checking in opaque blobs.  It pins the
on-media constants and layout directly; it does not call the production
formatter or record encoder.  A separately implemented test oracle constructs
all 16 KiB again and compares every byte.  Both constructors pin the variable
name as explicit UTF-16LE bytes, independently of host endianness.  The scenarios
cover an asserted request with upper bits (`0x11`), no request (`0x10`), an
absent variable, malformed attributes, FTW recovery, a fixed media fault,
idempotent before/after images and an asserted request rejected on S3.  The
test round-trips every applicable image through the production FV/FTW decoder,
store scanner and MOR probe and verifies byte-for-byte reproducibility.  It
also applies the production authenticated-variable writer plan to the before
image, compares the complete result with the independently constructed after
image, replays the writer and requires a no-op with no changed byte.

That replay validates the accepted pure store transformation only.  It is not
an end-to-end MOR transaction or SMM idempotency claim: the protected
bootstrap, media authority, private channel and orchestrator listed above do
not exist yet.  Mutation checks prove that the independent oracle, exact-byte
comparison, negative-enum rejection and second replay are each effective.
