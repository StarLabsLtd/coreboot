# Native CDK2 source selection

Initialize the recorded payload and its dependencies before configuring:

```
git submodule update --init --recursive payloads/external/cdk2/cdk2
```

Ordinary configuration, builds and dry runs require the clean CDK2 checkout
recorded by coreboot's staged gitlink. They do not fetch or switch CDK2 sources.
Once the pinned objects and dependencies are present, source selection needs
no network access. Stage a changed gitlink together with its integration change.

For local payload development, pass `CDK2_DEVELOPMENT=1` to both configuration
and build commands. This explicitly permits a different revision or modified
payload checkout; it does not change source files or disable artifact guards.
Such builds are development artifacts, not a clean pinned source pair.

Advancing to remote `main` is a separate, explicit operation:

```
sh payloads/external/cdk2/update-source.sh
git diff --submodule=log -- payloads/external/cdk2/cdk2
git add payloads/external/cdk2/cdk2
```

The update script refuses local changes. Review and commit the new gitlink with
the producer changes it requires. Normal make targets never invoke this script.

Run `python3 payloads/external/cdk2/test-profiles.py --out /absolute/output/path`
to resolve the 19 native Star Labs profiles and check their selected board,
architecture, payload, update GUID and variable owner. This checks configuration,
not compilation or platform qualification. These profiles select ordinary
SMMSTORE variables; they do not qualify protected variables or early DMA.

The former `config.starlabs_starfighter_phx` EDK2 profile is not supported in
this tree: neither its board selector nor its Phoenix firmware description
exists here. It has been removed rather than letting Kconfig silently select a
different motherboard while retaining a Phoenix update GUID.
