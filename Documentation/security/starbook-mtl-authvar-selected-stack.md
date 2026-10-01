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

## Separate pinned crossgcc artifact

The release-compiler SMM-only fixture was also executed from revision
`979a6a33500d895ff9189f4e9a5ecf9ada68af75` with the optional fixture change
introducing `MTL_AUTHVAR_SELECTED_XGCCPATH`. No selected production source
changed. This does not substitute for a final released ROM or hardware test.

```sh
MTL_AUTHVAR_SELECTED_XGCCPATH=/path/to/coreboot/util/crossgcc/xgcc/bin \
    KEEP_MTL_AUTHVAR_SELECTED_TMP=1 \
    sh tests/mainboard/starbook_mtl_authvar_selected_service_test.sh
node tests/mainboard/starbook_mtl_authvar_selected_stack_audit.js \
    --release-recorded ON/smm
```

The fixture explicitly supplies `XGCCPATH` and `CROSS_COMPILE_x86_32`, disables
`ANY_TOOLCHAIN`, checks the repository's pinned toolchain version, and checks
both generated OFF/ON compiler selections after the actual links. The recorded
compiler is `i386-elf-gcc (coreboot toolchain v2026-07-28_508d4deeddc) 15.2.0`;
its executable SHA-256 is
`2bb7e369e87dcb22736938d7e9cae0b7d408199114eb0a31c7ceb5f873cd3c6d`.
It was used read-only from the `q35-mor-linear-composition` crossgcc checkout.
LTO remains disabled and the selected stack/backend prerequisites are unchanged.

| Release input/artifact | SHA-256 |
| --- | --- |
| ON `full.config` | `6016a34ab0c6957a05a3a5935876b8c17556e291e33ec67f450d1530ca385aa2` |
| ON `smm/smm.elf` | `5cfcf28ca58500c2e637fff7d3f2870ec443f70f622d19c97f81de48b3242723` |
| Sorted 394 `.ci`/`.su` paths and contents | `fd0dac259e6b45701ee183eba642190bc2293f53e168f1e031359b65fb3bd50e` |

The separate record does not replace or relax the native hashes above. Actual
release ELF direct/tail traversal with the same source-qualified fixed callback
and recursion bindings reaches no unresolved edge. The two maxima remain
9,908 and 12,004 bytes; outer compiler frames remain 544 and 32 bytes. The same
stub/alignment/canary allowance gives 10,531 and 12,627 bytes respectively,
leaving 3,757 bytes under the selected 16 KiB per-CPU stack for the larger path.
There is no additional ESP realignment instruction in this ELF. Release
libgcc leaf prologues were checked separately: `__udivdi3` and `__umoddi3`
each save four registers and reserve 28 bytes (44-byte frames), whereas
`__udivmoddi4` reserves 44 bytes (60-byte frame). They make no nested calls.
The native `__umoddi3` frame remains 60 bytes in its unchanged record.

These are artifact-qualified selected BOOT/runtime calculations, not a
whole-ROM, FSP, legacy route, hardware admission, or OS variable cutover claim.
Changed compiler, source, callback bindings, or configuration requires a fresh
audit; changing a hash alone is not acceptance of another artifact.

## Recorded nested paths

### Post-classifier release record

A fresh OFF/ON fixture run at source commit
`cdc87a8610cf6fffa0547fcc7188c9961c458ab9` links the reviewed key-classification
producer and successor invocation readiness owner with the same pinned release
compiler above. Its explicit diagnostic mode is:

```sh
node tests/mainboard/starbook_mtl_authvar_selected_stack_audit.js \
    --release-post-classifier-recorded ON/smm
```

| Post-classifier release input/artifact | SHA-256 |
| --- | --- |
| ON `full.config` | `6016a34ab0c6957a05a3a5935876b8c17556e291e33ec67f450d1530ca385aa2` |
| ON `smm/smm.elf` | `5f839e8b6bfdf477651c4e91c04e314979ed57bc58eddb35a9339eb371e6d64a` |
| Sorted 394 `.ci`/`.su` paths and contents | `f430443957b5852d4a32abcc42682bb9a1e59a54b368d3bcba061b40239a8d8c` |

The selected BOOT/runtime maxima remain 9,908 and 12,004 nested C bytes. Actual
linked direct/tail traversal reaches no unresolved edge or unbounded recursion.
The new readonly classification path uses direct validated-view/index lookups;
it adds no callback binding or recursive call. The existing outer frames,
assembly allowance and fixed callback/source qualifications above remain
necessary. The resulting artifact-qualified totals remain 10,531 and 12,627
bytes, not a general guarantee for another build.

`PAYLOAD_BOOT_PRIVATE_BUFFER` is disabled in this profile: the unselected
held-lease prerequisite has no retained symbols in this ELF, so these totals
do not cover its call chain, first-entry delivery or resumed-CPU lifetime.
Historical native and release records are preserved independently; neither
accepts this new artifact. This is still an SMM-only artifact calculation,
not a whole-ROM, hardware, loader-lifetime or public-variable cutover signoff.

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
