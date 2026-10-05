Bounded Linux requester refresh HOST evidence, 2026-10-05

Fourteen exact originals plus this note. Top-level originals map to
/home/sean/current693-fwui-qemu.PMINOC with the same basename. Six flattened
host-config originals map to its requester-host directory: .config,
include/cdk2/config.h, kconfig/coreboot-source.tmp,
kconfig/coreboot-input.identity, native/command-inputs,
native/efivar-unlock-test.d. No UKI, kernel, ELF or private fixture is uploaded.

Signed c1ea34fa972759dc1dbd4684cfc439cd3e1462b8, parent009071b67f87378197a2729897d97e86e47ecaa3.
Full reviewed two-path delta SHA256
a0bae588a29a37009daa7adcfec099f3ef4833c39f003dc42905457d1cf56d8c.
Three finite requester pins and descriptions are refreshed; the existing
source-body, section, CPIO, exact-command-line and input-preservation guards
remain. New old-artifact refusal tests use the genuine unchanged binder.
This modifies HOST fixture admission/tests, not production firmware code.

The existing build-local-mini-uki.sh ran on signed009 with
CDK2_MINI_FW_UI_REQUEST=1 CDK2_MINI_BGRT_CAPTURE=0 and retained
/home/sean/normal-linux-fwui-uki-after2f83.ha8wDM/pinned/{kernel,busybox,cbmem,stub}.
Root e2ddbe reaped6bb4a7:0. Current full UKI SHA256
f8ee2b154e03fd36cbdec7c85731a7816cd08e68de19b42e3433e5bc5ca2a88f;
manifest e7c935e9a4b6ce3e986db17fae75898a8ade6960066516a6100c871961cc10d8.
Original kernel/BusyBox/CBMEM/stub remain unchanged. This is a rebuilt requester,
not a new kernel build. The retained original UKI stays at
/home/sean/linux-fwui-requester-recovery-after959f.u4JpHs/linux-fwui.efi.

Observed Root results are tool results, not invented persisted child-status files:
Actual requester/real objcopy preservation-opposition c55042:0, 31 unit tests OK.
Exact pre-PR672 requester refusal d0f6f2:0, 31 unit tests OK; old artifact refused
before external tools and input bytes unchanged. Expected rejection is not a
passing old-artifact admission or a causal production-firmware result.
Full reproducibility/hostile packaging734a73 reaped e99a3a:0; relocated inputs and
times produce identical UKI/manifest; mutations, aliases and bad sections refuse.
Standalone genuine config9b2b1d:0; public strict O2 ASan/UBSan helper5cdb6f:0.
Retained helper flags select no recovery/no PIE; actual helper ELF linked both
sanitizers during review. The dependency record is this helper target only.
Full stable lint ea4b19 reaped4fc543:0, all sixteen stable checks succeed.
No complete compiler/system/environment or whole-profile closure is claimed.

Independent source and actual HOST reviewers accepted these bounded results.
The separately running PR693 firmware UI guest is not this requester's reset
acceptance. No firmware guest, hardware flash or project completion is implied.
