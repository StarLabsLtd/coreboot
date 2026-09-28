# Meteor Lake authenticated-variable presence boot classifier

`SOC_INTEL_METEORLAKE_AUTHVAR_PRESENCE_BOOT_CLASSIFIER` builds a dormant,
default-off classifier over the power state retained by
`pmc_fill_power_state()`. That function samples PM1 status and control,
`GEN_PMCON_A`, and both global-reset and host-reset cause registers before it
clears the hardware status. It retains those raw values in
`chipset_power_state` and adds the normalized previous sleep state.

A future caller must exclusively own the immutable romstage structure while
classification runs and must pass it without rereading the cleared hardware.
Two complete relevant-field snapshots reject synchronous mutation at the API
boundary; this is not a claim of C data-race tolerance. Either `GBL_RST_STS` or
`HOST_RST_STS` classifies the boot as reset,
before considering a possibly stale PM1 sleep type. Both flags together still
mean reset. The cause registers are retained for diagnostics but are not proof
of a current reset by themselves.

Without reset evidence, raw `WAK_STS`, raw `SLP_TYP`, and the normalized sleep
state must agree exactly for S3 or S4. A wake that normalized to S5, including
a failed S3 caused by power-well loss, is unknown. Genuine cold requires no
`WAK_STS`, positive `PWR_FLR` or `SUS_PWR_FLR`, and normalized S5. This matches
the SoC's existing true-G3 normalization; the no-wake requirement prevents a
failed S3 with power-well loss from being admitted as cold. Soft S5, S0,
reserved sleep types, and every contradiction remain unknown.

The versioned result is diagnostic classification evidence only. It does not
mint authority, change the existing loader-instance lifecycle ABI, install a
boot or lifecycle hook, select a board, or expose an endpoint, route or
transport. A later composition must require the existing loader-instance
contract separately and may admit presence authority only for exact cold-boot
evidence.
