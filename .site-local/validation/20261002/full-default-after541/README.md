# Default whole regression after PR541

The clean signed CDK2 source revision was
`522c0a2a7cba1067db2e98eb977117531f44b900`. Root actually reaped session
39495 with exit 0 for user unit `cdk2-root-full-default-after541`, invocation
`369ffe2978b54facbab13761d71be8f0`. The unit ran for 25m28.655s;
GNU time reported 25m28.21s and exit 0. CPU time was 48m6.805s and peak
memory 958M. Both caller config/header hashes passed after the run and
again at evidence collection.

The actual command ran `review-profile-check check native-stage` with the
genuine default defconfig, explicit independently owned caller config/header
paths and artifact directory. It used the same pinned coreboot codec inputs
as the protected run, not an invented protected-policy bypass. This was
changed-source recompilation in a reused build directory, not a fresh build.
The accompanying raw log retains expected causal assertion failures; the
complete outer command and config fingerprint checks exited 0.

This closes the default whole regression at this revision. The separate
full-protected-after541 packet closes the protected run at the same revision.
Neither result proves future capsule-provider changes, authenticated firmware
installation, hardware validation, or final project sign-off.
