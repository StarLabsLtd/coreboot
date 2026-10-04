UNEXECUTED Root-only finite loose-file preservation proposal.

After Root and opposing full source review, Root may run:
env -i PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin HOME=/home/sean \
 LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1 \
 python3 /home/sean/loose-cdk2-files-retirement-retry-collector.Kst9uj/collect.py

The fixed absent destination is:
 /home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/loose-cdk2-files-retirement
No execution, file move, deletion or Git write has occurred from this proposal.
All shared versions must stay immutable; changes require a new recipe directory.
Original zIKWjY e425a884/7b158271/4c77143f remains unchanged and unexecuted.
This new version only changes its own recipe path and guards finalization:
any final ledger write/hash-check/success-print exception restores the failure
marker and propagates failure. No previous collector execution failure is invented.

MANIFEST.tsv is the exact frozen147-path census, excluding the two active
cdk2-retired-worktree-cleanup-20261004.sh and
cdk2-retired-receipt-cleanup-20261004.sh scripts. Each literal row has class,
byte size, full SHA256 and absolute original path. No glob selects new files.
Author read-only census:142 historical text files total1,543,906 bytes;
2 generated HOST ELF files total84,240 bytes;3 existing local tar.zst files
total238,340,418 bytes. Original originals total239,968,564 bytes.

All142 text original byte streams are preserved with content-addressed
deduplication. Files over1MiB use deterministic gzip -n and full decompressed
byte comparison; others use plain cmp for every original row, including
duplicates and empty files. MIME must be text/plain, text/x-diff or empty;
private-key headers refuse. Original bytes and recipes are checked before
and after. GNU ORIGINAL_SHA256SUMS binds all147 unchanged originals.
FILES_MAP.tsv maps text rows to full blobs; its five nontext rows explicitly
mean metadata-only or future-plan-only, NOT payload preservation.
ARCHIVE_SHA256SUMS binds every stored file other than itself.
Three own recipe originals are separately copied and hashed. Utility evidence
is finite enumeration, not complete interpreter/stdlib/runtime attestation.

The two old flash-parser HOST ELFs are NOT executed or copied into evidence.
Their actual static file/ELF headers, byte sizes and original hashes are
recorded. They are generated historical test binaries, not source authority;
no byte-identical rebuild or historical test-pass claim is invented.
Root's later retirement plan may discard these rebuildable generated outputs.

The three existing tar.zst archives are NOT duplicated, extracted or moved.
They may contain historical media, frame captures or other private test
artifacts; no absence-of-keys/content-cleanliness claim is made. Their fixed
size/hash and proposed local target under
 /home/sean/Documents/cdk2-validation/retained-inputs/loose-cdk2-archives/
are recorded in ARCHIVE_MOVE_PLAN.tsv. The collector checks only their zstd
magic and complete original hashes. Root alone may later review an exact
no-clobber MOVE, after packet durability and independent verification, then
verify full retained bytes/hash. These large existing archives must never be
copied into evidence Git or duplicated just for cleanup.

Author's bounded read-only reference census found no absolute-path OR basename
reference to any of the147 originals in current canonical tracked files and:
 whole-regression-after689-recipe.i9xraY
 full-lint-after688-recipe.FIdWyr
 tpm-current-os-guest-recipes.jNWZGi
 tpm-fifo-functional-recipes.4GUdjk
 legacy-generated-retirement-retry-collector.NpbXOk
 coreboot-config-retirement-collector-final.AKAY8f
nor a direct selected-input reference in actual normal689 inputs-before.json.
This is a finite known-consumer audit, NOT a whole-home/future-use guarantee.
Regular loose files are not worktree/object-store owners. Active/default
source/vendor/golden config/guest fixture paths are outside this exact scope.
Root must recheck relevant dependencies when approving eventual retirement.

Collector success means byte preservation of142 text originals and honest
metadata/plans for the remaining5 only. It does not imply historical gate,
firmware, QEMU, hardware or complete cleanup success. Preflight failures create
no packet; post-creation failures retain partial packet/collection.status1.
No implicit overwrite/retry cleanup occurs. Require independent stored/original
hash and every mapped plain/decompressed counterpart audit, signed durable
evidence push, then a separately reviewed exact Root retirement script. Root
alone writes evidence Git, moves archives and sends retired originals to trash.
