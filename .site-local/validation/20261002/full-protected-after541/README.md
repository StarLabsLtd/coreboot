# Protected whole regression and scratch-caller closure after PR541

CDK2 source was the clean signed revision
`522c0a2a7cba1067db2e98eb977117531f44b900`. The protected whole regression
ran `review-profile-check check native-stage` against the genuine protected
quiet setup acceptance defconfig and explicit caller config/header paths.
Root actually reaped process session 32715 with exit 0. User unit
`cdk2-root-full-protected-after541`, invocation
`96705a30424b47829b3f11aa27ca3357`, took 21m26.188s; GNU time reported
21m25.97s and exit 0. Both caller hashes passed after the run and again
during evidence collection. This was changed-source recompilation in a
reused artifact directory, not a fresh build.

The seven scratch-caller final integration run was actually reaped as
session 91153 with exit 0. Its config/header hashes remained unchanged;
the ten-file source manifest matched the committed PR541 source. The
earlier integration attempt, session 25083, exited 2 because its disk-ON
fixture had not selected the real capsule composition. Its raw failed log
is included separately. No elapsed time is claimed for the seven-caller run.
The root strict lint replay at this revision, session 6250, exited 0.

These receipts close the protected whole regression and focused scratch
configuration repairs at this revision. They do not prove future source
changes, the default-profile run, new native capsule provider admission,
authenticated firmware installation, hardware validation, or project sign-off.
Earlier contaminated and interrupted runs remain failures in the separate
scratch-profile-isolation packet; none is relabelled here.

The manifest binds the accompanying raw logs and source/caller manifests.
Caller manifests contain original absolute paths for replay comparison.
