# CDK2 NVMe bus-master evidence

This is external evidence, not part of the coreboot ownership gate. It must
remain unresolved until coreboot CI checks out the pinned CDK2 revision and
runs the named tests. In particular, this document is not sufficient evidence
to disable `PCI_ALLOW_BUS_MASTER_ANY_DEVICE`.

Reviewed CDK2 commit:

`b4492d5cb26847b628cdf8c4252a4331359ea385`

Pinned Git object IDs:

- `src/modules/nvme/pci_adapter.c`: `5196febae81adeda7a8a98739b5ca643caa87511`
- `tests/nvme_entry_test.c`: `0a1fe3a47955ef6989efd25bf0c34cefd5a6ce5a`
- `tests/linear_boot_test.c`: `c2b3e8ff2cc4489b4903e21bd6ded852d6d7b0aa`

At that revision, `cdk2_nvme_pci_adapter_init()` reads the original PCI
attributes before enabling the selected NVMe controller. Its release path
disables the attributes it acquired and restores the saved attribute value.
That is payload endpoint ownership; it does not establish ownership of the PCIe
bridge forwarding path.

Locally observed on 2026-08-25:

```text
$ make native-nvme-test
nvme model tests: PASS
nvme controller tests: PASS
nvme binding tests: PASS
nvme pass thru tests: PASS
nvme block tests: PASS
nvme entry tests: PASS
nvme PCI transaction tests: PASS
nvme diagnostic tests: PASS

$ make native-linear-boot-test
cdk2 linear boot test: PASS
```

These local results are recorded for review only. A future policy-changing
patch must add paired CI that verifies the exact commit, object IDs and test
results. No comparison with Depthcharge is asserted here.
