# Private NVMe native types: bounded source and HOST results

Ready CDK2 PR642 is signed commit `8137157963fe88cb3ca9b3f9a1662e3c5bbaf41c`,
based on ready PR641 `99605176e9955f1bc451ca90634cf7fdea10415f`. Root and an
independent worker reviewed the final fourteen-file slice before author gates.

The default eight-test NVMe suite and O0/O2 runtime-sanitizer ASAN/UBSAN suites
return zero (`default-native-nvme`, `san-runtime-o0-native-nvme` and
`san-runtime-o2-native-nvme` in `status`). The separate warning-promoted
`san-o0-native-nvme` and `san-o2-native-nvme` suites return 2.
Root independently replayed those sixteen existing sanitizer executables; that
is not an independent compilation. Both generated comparison headers match,
and root separately verifies nine whole native object pairs match. The PCI
adapter object differs at reviewed, correctly typed EFI output boundaries.

Strict pedantic/conversion warning-promoted suites fail on both baseline and
candidate with inherited diagnostics. Initial object commands also fail before
the corrected default object commands succeed. All failures remain in `status`
and original logs; none is presented as a successful stricter lint/build gate.
Changed-file checkpatch returns zero, not whole-project lint completion.

The author receipts originated at `/tmp/cdk2-nvme-gate.xFK4Nu`; that temporary
directory is no longer present after the host reboot. This durable finite copy
preserves text logs, outcomes, section/symbol inventories, source hashes and
resolved default configuration. Source before/after records have different
formatting; their eighteen source/config hashes match individually. Recorded
compiler/version identity is not a complete before/after tool-binary manifest.
Absolute object paths in `object-hashes` identify omitted original objects.

No objects, executables, firmware images, disks, secrets or imported source
trees are included. No PE/Core build, QEMU boot, hardware result, 32-bit
portability or full-release completion follows from this packet.
