UNEXECUTED Root-only config and reconstruction collector successor.

Root alone may run after full Root/peer source review and successful closure
of frozen vendor preservation FJaETV76171797/202a19aa/f68a4316. Run collect.py
with one new absent absolute packet directory under the existing evidence
validation date directory. Keep all coreboot/evidence/module/recovery refs,
source and configs frozen during both admission and recheck phases.

Same exact66 parents and original finite configuration/recipe copying policy
as immutable executed AKAY8f42b7598b/502. Its first failure before packet
creation is preserved; it exposed an unavailable private vboot branch tip,
not lost source or a successful archive. All .config/.config.old originals,
parent signature/HEAD/ref/status/full diff and recursive nested source refs
and actual stores remain explicitly recorded. No source/vendor/object/build,
media/ROM/ELF/private-state recursion or copying occurs. Only the two own
collector recipe files are additionally copied and byte-rechecked.

Admission now requires actual HEAD plus ALL original raw ref object IDs,
including remote refs and annotated tag objects, not just local branches or
silently peeled commit IDs. Original objects must be both present and in
retained Git ref reachability; object types are recorded without dereferencing
tags away. Actual canonical refs/HEAD/common-store/shallow metadata and a
digest of reachable object names are recorded once per retained repository.
These finite snapshots are freshly re-read and compared after collection.
Phase-local snapshots avoid repeating full Git ancestry walks per parent/ref;
there is no persistent cache or weakened post-collection check.

Only exact vboot dd38e912b39166975ed13e46602d8a5b2a6e85f4 uses the retained
/home/sean/Documents/cdk2-validation/retained-inputs/vboot-dd38-recovery.git.
That store must be proper bare/shallow, expose the exact archive recovery ref,
declare dd38 as a shallow boundary and have no borrowing alternates. Canonical
vboot MUST remain nonshallow. All other objects still require their proper
canonical module repositories. The absent ancestor341fc8 is not reconstructed;
this preserves exact shallow-tip recovery, NOT complete upstream history.
The separate successful preservation receipt binds all isolated-store files
and actual connectivity. No signed upstream vendor or complete-object-pack
attestation is inferred from these config/reconstruction receipts.

All prior source-clean/ignored-input refusal, proper resolved paths, foreign
worktree/incoming alternate refusal and original+stored SHA/plain-map checks
remain. RECONSTRUCTION.json and RETAINED_REPOSITORIES.json are generated
metadata; ORIGINAL_SHA256SUMS/FILES_MAP bind copied original configs/recipes,
while ARCHIVE_SHA256SUMS covers copied and generated packet files.

No deletion until complete packet success, independent original/stored/map/
reconstruction audit, durable signed push, then separate exact retirement
script with fresh dependency/status/ref checks. Keep canonical/recovery stores
and all retained refs; no Git GC/pruning authority. Failed partial packets
are not successful archives. This successor has not been executed or tested.
