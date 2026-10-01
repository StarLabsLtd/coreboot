# Selected StarBook MTL authenticated-variable SMM stack audit

This records a source and linked-artifact audit, not hardware admission. It
covers the selected BOOT bootstrap and `0xfc` service paths which return from
the pre-lock dispatcher before the ordinary SMI handler. It does not certify
the separate legacy presence/lifecycle-close/S3 routes, the whole ROM, FSP
execution, or another compiler/configuration. No payload variable slots or
hardware activation are implied by this report.

## Reproduce the artifact

The production source revision is
`7113e9f08e7fc08a682bcd28570711285a7aff99`. Its subsequent test-only wire
revision `ab10062ba55422d0c31e216ff4a22dd9bffb15c1` does not change the selected
SMM sources. Run:

```sh
KEEP_MTL_AUTHVAR_SELECTED_TMP=1 \
    sh tests/mainboard/starbook_mtl_authvar_selected_service_test.sh
```

The script prints the retained artifact directory. Both OFF and ON link the
actual native 32-bit SMM ELF; ON compiles with `-fstack-usage` and
`-fcallgraph-info=su`. There is no injected reference or host admission stub.
The recorded compiler is Ubuntu GCC 15.2.0 (`ANY_TOOLCHAIN=y`, LTO disabled),
not a claim about the release crossgcc compiler. The generated ON profile has
16 KiB per-CPU stacks, 16 MiB TSEG, the sole protected SMMSTORE backend, and no
raw `SMMSTORE` dispatcher or runtime write-protection override.

Recorded SHA-256 values:

| Input/artifact | SHA-256 |
| --- | --- |
| ON `full.config` | `94e021c7b7f3cf591594121c32f70f3bd64f862cd84711b8b2ec2ba02af175f0` |
| ON `smm/smm.elf` | `a4ff69f7391f8c749432b4b573681c1ed5905a991d8c12c9a0625cbc4b4e6f9f` |
| Sorted 394 `.ci`/`.su` paths and contents | `12daff6718ab9c8acb24ff822beb438998158311558b5d35fa8df08e9cd26642` |

Inspect all 197 compiler `.ci` files and their `.su` partners, the retained
symbols, and the actual ELF direct calls and tail jumps:

```sh
nm --defined-only ON/smm/smm.elf
objdump -d ON/smm/smm.elf
addr2line -f -e ON/smm/smm.elf ADDRESS...
```

Replace `ON` with the printed ON directory. Resolve static functions by
address and source file, not just their frequently duplicated names. The
compiler's unlabeled split-function edges are real edges. Normalize only
optimizer suffix numbering (`.constprop.N`, `.isra.N`, `.part.N`). Compiler
ICF aliases `bytes_all_zero`/`zero_bytes` and `key_equal`/`key_is` share a body;
they do not introduce another frame. Take the larger frame where a global
weak/strong compiler description overlaps, and include actual retained ELF
edges missing from an earlier compiler graph. Do not sum all compiled frames
or silently drop an unresolved callback.

An optional diagnostic preserves the recorded traversal and callback
manifest without adding Node to the firmware build or test dependencies:

```sh
node tests/mainboard/starbook_mtl_authvar_selected_stack_audit.js ON/smm
```

It deliberately accepts only the recorded ELF/configuration and compiler
annotation hashes. Annotation digest input is the sorted path relative to
`smm`, NUL, decimal byte length, NUL, then exact file bytes for each `.ci` and
`.su` file. The tool parses the same captured buffers it hashes, not a second
read of potentially changed files. Its
fixed assembly/callback manifest is not an automatic inference engine for
arbitrary builds. Review and update that manifest before admitting another
artifact. Unknown roots, unresolved reachable callbacks/native targets,
unbounded stack nodes, and unbounded recursion cannot produce a successful
diagnostic exit. An exit of zero reproduces the recorded calculation; it is
not release-toolchain or hardware sign-off.

## Recorded nested paths

Each following value is the maximum nested C path found within the named
selected root, including conservative four-byte return-slot additions per
call. GCC's static or `dynamic,bounded` frame values already include its
local/outgoing-call allocation. Tail jumps are conservatively treated as
calls rather than used to reduce the bound. No reachable unbounded dynamic
frame is admitted. The unused generic SPI command writer has a dynamic
frame but is outside the fixed hardware-sequencer path described below.

| Selected root | Nested C bytes |
| --- | ---: |
| `service_bootstrap_dispatch` | 9,908 |
| `starbook_mtl_authvar_service_runtime_dispatch` | 12,004 |

The BOOT maximum is:

```text
service_bootstrap_dispatch                         272
  starbook_mtl_presence_bootstrap_route_install      96
  starbook_mtl_authvar_presence_route_composition_provision 96
  intel_smm_invocation_adapter_route_provision       96
  payload_mm_authvar_presence_route_session_provision 128
  route_session_validate_and_bind                 5968
  protected_exact                                2896
  receiver protected_storage                       48
  smm_invocation_runtime_range_is_protected         48
  runtime_geometry_unchanged                       32
  runtime_geometry_snapshot                        64
  runtime_geometry_valid                           80
  runtime_range_contains                           20
  runtime_range_valid                              12
```

The runtime maximum is:

```text
starbook_mtl_authvar_service_runtime_dispatch       528
  payload_mm_authvar_service_execute                32
  payload_mm_authvar_service_transaction           448
  payload_mm_authvar_set_transaction               288
  coordinate_transaction                         1408
  payload_mm_authvar_coordinator_prepare           704
  payload_mm_authvar_authority_decide             1216
  payload_mm_authvar_authority_provider_verify     432
  payload_mm_authvar_trust_store_verify            416
  payload_mm_authvar_trust_anchor_verify           176
  payload_mm_authvar_x509_chain_verify_detailed   4656
  mbedtls_x509_crt_verify                            8
  x509_crt_verify_restartable_ca_cb                 176
  x509_crt_verify_chain                            160
  mbedtls_md                                        8
  mbedtls_sha512                                  320
  mbedtls_sha512_finish                            80
  mbedtls_internal_sha512_process                  864
  memcpy                                           12
```

Add `smm_pre_lock_dispatch` (544), `smm_handler_start` (32), and their two
four-byte call slots. The 32-bit entry stub pushes four words and a call
return address (20 bytes), may lose up to 15 bytes aligning ESP, and reserves
the four-byte bottom canary. This adds a conservative 39 bytes outside C.
The resulting recorded totals are 10,531 bytes for BOOT and 12,627 for the
runtime root, leaving 3,757 bytes below the 16,384-byte allocation in the
larger case. Do not add AP wait frames to the BSP crypto path: each CPU owns
its separate stack. These values require independent audit acceptance and
must be recalculated after compiler, configuration, source, or callback
binding changes; they are not a generic 1/2 KiB SMM stack guarantee.

## Callback and recursion manifest

The following bindings are specific to the selected source/configuration.
Function names scoped to a file mean its static function, not another
same-named ELF symbol.

| Callback owner | Selected target/source binding |
| --- | --- |
| Evidence claim/restore/publish, runtime claim, BOOT receiver/response | `invocation_adapter.c` `match_apmc_write`, `read_value`, `write_value`; both adapter initialization and provider binding install exactly this tuple |
| Adapter span initialization | `invocation_adapter_provider.c` `runtime_span`, installed by its provider provision path |
| Authority decision | `payload_mm_authvar_authority_provider_verify`, installed by the actual executor factory |
| PK setup/free/can-do/bit-length/verification | `mbedtls_verify_wrap.c` fixed verification-only RSA table: `rsa_alloc`, `rsa_free`, `rsa_can_do`, `rsa_get_bitlen`, `rsa_verify` |
| RSA public-key parsing | Actual link `--wrap=mbedtls_rsa_parse_pubkey` selects `__wrap_mbedtls_rsa_parse_pubkey`, not the unused upstream private-key parser |
| X509 extension and verification callbacks | NULL: selected crypto uses `mbedtls_x509_crt_parse_der_nocopy` and passes NULL verification callbacks |
| ASN.1 sequence traversal | `asn1parse.c` `asn1_get_sequence_of_cb`, installed by `mbedtls_asn1_get_sequence_of` |
| Media begin/read/program/erase/sync/end | `payload_mm_authvar_smmstore.c` fixed `begin`, `read_media`, `program`, `erase`, `sync_media`, `end` table |
| Bootstrap media facts/install | `payload_mm_authvar_smm_media_spi.c` `media_facts`/`media_install`; QEMU backend is excluded |
| SPI setup/probe and volatile lease I/O | MTL SMM bus map has only bus 0 FastSPI: `fast_spi_flash_ctrlr_setup`, `fast_spi_flash_probe`, and fixed `fast_spi_flash_read/write/erase/status` |
| Generic SPI fallback | A failed hardware probe DOES enter `spi_flash_generic_probe`. Include that frame and its READ-ID command/transfer failure path. Fixed FastSPI has NULL generic xfer/vector hooks, so READ-ID fails before vendor matching or `after_probe` callbacks; do not omit the whole fallback merely because `flash_probe` is non-NULL |
| SPI write-restriction proof | MTL `authvar_platform_smm.c` `writes_are_private`, passed through the sealed bootstrap policy |
| Contract platform callbacks | `payload_mm_authvar_smm_bootstrap.c` `smm_entry_owned`, `spi_writes_restricted`, `raw_flash_transport_absent`, `communication_reserved`, `store_owned` |
| Runtime authority storage / MOR seal | Bootstrap `authority_storage`; MOR callbacks are bootstrap `protected_storage` and `fixed_transport` |
| Original BOOT storage proof | `authvar_presence_bootstrap_receiver.c` `protected_storage` obtains the real runtime view and uses its range validator |
| Delegated transaction storage proof | Arm `wrapped_protected_storage` calls route-session `delegated_protected_storage`, then the sealed ORIGINAL receiver proof. `protected_exact` also retains the original proof; no delegation self-recursion |
| Route/arm terminal failure | Fixed board authority policy `fail_stop`; transaction provisioning uses arm `wrapped_fail_stop`, which reaches that original sealed failure callback |
| Presence page guard | Board authority policy `dma_protected` and `cpu_rendezvous_active`. This is the legacy `fe` presence path, not authority for an `fc` claim |
| DMA receipt/observer | Fixed receiver `ordinary_dram_range`, `observer_read32`, `observer_sha256`, `observer_verify_translation` |
| DMA requester and translation reader | `dma_smm_authority.c` `binding_read`, `ecam_read32`, `table_read64` |
| Mapped store access | Actual ELF `boot_device_ro` is FastSPI `mmap_boot.c`, not the weak x86 implementation. Its xlate windows point only to memory-region devices: outer `rdev_mmap` → `xlate_mmap` → one nested `rdev_mmap` → `mdev_mmap`; unmap uses `xlate_munmap` |

BOOT's large route binding calls the original proof while validating and
binding delegation. It returns before the later wrapped/delegated transaction
proof runs; their large snapshots must not be falsely nested together.
Conversely, split `.part` bodies and their failure/proof callbacks must not be
omitted merely because the wrapper body is small.

Two real recursions have source bounds:

- CMS `der_validate`: all entry points start at depth zero; constructed
  objects reject at `PAYLOAD_MM_MAX_DER_DEPTH == 12` before descending.
  Therefore at most 13 simultaneous 96-byte frames, including a primitive
  leaf at depth 12. Object/sibling counts are loop work, not extra nesting.
- VT-d `verify_level`: entry level is 3, recursion decreases it, and level
  zero is a leaf. At most four simultaneous 160-byte frames. Table entries
  and requesters are loops, not recursive stack multipliers.

The selected X509 chain walk and RSA/MPI routines are iterative. Crypto arena
allocation does not allocate certificate or MPI arrays on this C stack.
The linked i386 libgcc division helpers are leaf routines with four saved
registers and 28/44 bytes of reservation: 44 bytes for `__udivdi3`, 60 for
`__umoddi3`/`__udivmoddi4`, before the conservative call-slot addition. The
signed division reference in the early compiler graph is absent from the
retained ELF and is not treated as an unresolved runtime callback.

## Remaining gates

Independent source/manifest review is required before final stack sign-off.
Re-run on the release toolchain and final selected ROM, including the real
entry stub and any changed callback implementation. This audit does not
prove live SMRR/TSEG locks, FSP's rendezvous behavior, hardware flash timing,
volatile-variable ownership, or successful OS runtime cutover. Compatibility
QEMU runs and host hardware models cannot replace those hardware gates.
