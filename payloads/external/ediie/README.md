# edIIe payload

Before configuring edIIe, prepare its source and dependencies from the
coreboot checkout:

```sh
sh payloads/external/ediie/prepare-source.sh
```

This selects `refs/heads/main`. For a release, pass its full tag ref
(`refs/tags/<tag>`) instead. Source preparation records and prints the
resolved commit. Run it explicitly to change or refresh the selection;
configuration and builds do not advance the checkout.

Select `PAYLOAD_EDIIE` on a supported x86 board and enable ASS. For firmware
updates, enable the EFI capsule driver and configure the trusted public
certificate and update regions. edIIe accepts capsules on disk only. Normal
builds verify the payload revision without updating its checkout.

To build a local edIIe commit, first initialize its submodules and commit
your changes. From the coreboot checkout, run:

```sh
sh payloads/external/ediie/prepare-source.sh --local /path/to/ediie
make EDIIE_SOURCE=/path/to/ediie
```

The local checkout must be clean and its submodules must match the recorded
revisions. Repeat preparation after changing the selected local commit.
