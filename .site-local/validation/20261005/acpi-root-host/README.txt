Bounded ACPI root HOST evidence, 2026-10-05

Nine exact original logs from /home/sean/acpi-authority-host.bYyxbf plus eight
configuration inputs. For each build/build-o0 context, flattened .config,
config.h, coreboot-source.tmp and coreboot-input.identity map respectively to
original .config, include/cdk2/config.h, kconfig/coreboot-source.tmp and
kconfig/coreboot-input.identity. No source duplicate or executable uploaded.

Signed source009071b67f87378197a2729897d97e86e47ecaa3, parentROADMAP-only
e5ba728c22d260f1b43e93b6f0bdee99491a8bc8. Complete two-path production/fixture
delta SHA25632e6f865f0c4a03470d393bb0e78066e88ec58b8a155a5ef32d81808a7886f5c
was frozen and tested on signedf6 before unchanged transplant onto e5.
Independent source reviewers accepted the bounded change. Distinct valid RSDP
addresses share decoded contents; this is not a differing RTC/PCI-value test.
Absence-only legacy CBMEM fallback and explicit-zero NOT_FOUND are preserved.

Observed Root tool results (not fabricated persisted child-status files):
configure95ae74 and O0 configureb109d0:0.
Public native-coreboot-test O2ac3d2a reaped75c52a:0; O02dc81f reaped902001:0.
Both use -std=c11, -Wall -Wextra -Werror -fshort-wchar and ASan/UBSan,
-fno-sanitize-recover=all -no-pie; config headers compare byte equal.
Old compile9af703 reaped6e765d:0; old runa1698d:raw1 with sixteen assertions,
four accepted bad cases times four reasons, no sanitizer diagnostic. Short-first
and single-short already refused historically, not new causal failures.
Owned full-file C styleab363e reaped5a6681:0; allowlistd95835 reapedafb84e:0.
Empty logs are preserved, not treated by themselves as proof of success.
native-coreboot-stage d45c44 reaped1f0a91:0, real model PASS and stage ELF present.

Old source is exactly signedf6 src/boot/coreboot_handoff.c (git-show byte cmp0).
The actual typed compiler command preserves the emitted 17-source Make recipe,
swapping only that handoff source, with final current fixture and sixteen other
sources; relative paths resolve against rtc-board-facts-after677. Outputs are
old-coreboot-test and old-coreboot-test.d in the original receipt directory.
Shared -MF describes only the last translation unit, NOT full input/header
closure; no complete compiler/system/environment attestation is claimed.
The stage command supplied COREBOOT_TREE at retained signed3e2609c8938a and an
unused nonexistent MBEDTLS_SOURCE path; no mbedTLS validation is inferred from
that argument. Actual source dependencies are the existing native Make rules.

No QEMU run, ROM flash, hardware result or whole-project completion is implied.
