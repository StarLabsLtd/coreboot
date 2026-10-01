# Exact PR451 follow-up logs

These are separately completed root runs at CDK2
`16520afbee6ee5d4b9ff0e23f777e9c641c893b8`, not renamed historical e468 logs.
Native build, pairing, three setup QEMU boots, BGRT assertion and Linux runtime
assertion commands all returned zero. The manifest records coreboot
`4dd7974f5a5ec4a6e849cb76f0f81d1212645148`. This is the debug-enabled legacy
SMMSTORE compatibility profile: protected-variable runtime is disabled.

ROM SHA256 is
`7f7b605b3e2c3f40b44cd1db5392becb5ca6d319715f0d341c65ba61e2990b46`;
native ELF SHA256 is
`6023226467abfef746173e59f4fec5259ebc7e86d2f5bbc081e4180ebccdabaa`.
Both equal the previously archived historical compatibility artifacts; the new
manifest and direct provenance retain this run's distinct source identity.
This directory preserves logs/provenance only, not another complete raw QEMU
archive. The earlier historical archive remains separately qualified.

The two `native-first-entry-root-f764` logs are independent root component runs
at signed WIP `f76455c68e70ba36b3ab3d2fc3c59315b79aaf36`, directly after PR451.
Both commands returned zero. Initialized-entry tests use the genuine producer
with modeled MODULE/prefix/trigger/MMIO and cover twelve cases, two protected
entry refusals and causal removed-refusal mutants. Their base component
header is the original selected header, SHA256
`78b97c2da99c1bb7c24d7e55674390110c044d216d07158146cd1df98c012edc`.
The fixture explicitly selects COPY=1 and separately overrides protected mode
for the outer refusal cases; those overrides are not Kconfig admission.
The selected/default link matrix derives genuine configurations from the
recorded coreboot source config, SHA256
`0b5be5aa9b706d8f29e73b3815522022bf3378100d347578ebc5f51497ef8a33`.
Its COPY-on profile deliberately selects legacy storage so copy code is
reachable; protected mode is separately proven to refuse before copying.

These component and compatibility results do not activate protected image
policy, establish hardware validation, prove splash-text persistence, or close
project-wide sign-off. The first-entry experimental primitive may remain
unmerged if fresh GENERAL policy delivery makes it unnecessary.
