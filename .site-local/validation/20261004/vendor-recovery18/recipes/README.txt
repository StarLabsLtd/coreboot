UNEXECUTED Root-only vendor recovery preservation proposal.

After full Root and independent source review, run with a new absent receipt:
env -i PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin HOME=/home/sean \
 LANG=C LC_ALL=C GIT_CONFIG_NOSYSTEM=1 \
 bash /home/sean/coreboot-vendor-recovery-allrefs-proposal.FJaETV/preserve.sh \
 /home/sean/coreboot-vendor-promotion.20261004-r1

This explicitly authorizes only the reviewed local recovery writes, not source,
HEAD, checkout, canonical branch overwrite, firmware build, network fetch or
deletion. Root must hold other writes to these stores during the receipt.
All earlier collector recipes/failure remain immutable. Failed partial stores
and receipts must remain and be qualified; no implicit cleanup/retry overwrite.

PROMOTIONS.tsv enumerates 17 nonshallow canonical-module/local-source/OID
pairs. Each fetched recovery ref is refs/archive/retired-vendor-<exact-oid>.
An existing exact ref is accepted; an existing different ref refuses. Fetch is
local-only, no tags, no forced refspec and no FETCH_HEAD write. Before/after
source and canonical HEAD/status/shallow metadata must remain byte-identical.

The eighteenth pair is shallow upstream vboot dd38e912b39166975ed13e46602d8a5b2a6e85f4.
Its parent341fc8dad682d828e63671b76ee4e4d71bc39a07 is absent, not reconstructed.
The canonical vboot repository MUST stay nonshallow. Instead a new absent
/home/sean/Documents/cdk2-validation/retained-inputs/vboot-dd38-recovery.git is cloned as a separate bare
single-main-branch repository via local file transport, --no-local and
--no-hardlinks, with an empty template. This preserves the declared shallow
tip boundary without borrowing alternates from a retiring store. The exact
main/HEAD/archive ref, bare/shallow states, absent alternate/promisor settings,
connectivity, shallow metadata and all retained files are explicitly checked.
No complete upstream history or signed vendor provenance is claimed.

Finite recipe/tool/Git-subtool and source/canonical state closures plus raw
stage statuses are preserved. A later collector successor must accept this
isolated retained store ONLY for the exact dd38 branch tip; all other required
tip/HEAD and all raw ref-object checks still use canonical module repositories. Recovery store and
all archive refs must remain after retirement; no Git GC/pruning authority.
No parent may be removed until successful complete preservation, successor
packet qualification, durable signed push and a separate exact retirement
plan are independently reviewed. This proposal has not been executed.
