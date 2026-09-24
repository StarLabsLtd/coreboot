# Aligned bootmem reservations

`BOOTMEM_ALIGNED_RESERVATIONS` provides a dormant, bounded reservation service
for clients that need exact physical backing before the OS memory map is
published. It installs no request, platform hook, or feature-support claim.

A client registers one fixed request or an atomic batch before bootmem
initialization. Each request
contains a nonzero page-multiple byte count, a power-of-two alignment of at
least one page, an explicit exclusive address limit no greater than 4 GiB, and
either the `BM_MEM_RESERVED` or `BM_MEM_TABLE` destination tag. Registration
copies every request and returns opaque handles. A batch is validated completely
before any registry slot is consumed. Duplicate, malformed, aliased, late, or
excess requests fail without publishing a handle or retaining a partial batch.

After all existing CBMEM, capsule, ramstage, architecture, and platform ranges
have been applied, bootmem clones both its internal and OS-visible maps. It
services requests in registration order, selecting each physical base exactly
once from the cloned final `BM_MEM_RAM` view. That same base is verified as RAM
and retagged in the cloned OS view. Only the requested bytes are retagged;
alignment gaps remain RAM. If any request cannot be satisfied, both clones are
discarded and boot terminates before memory-table serialization. On success the
two maps are adopted together and handles may be queried for their exact base,
size, and tag.

The registry is rebuilt in each ramstage, so it retains no stale S3 pointer or
allocation. A future consumer must explicitly suppress destructive work on S3,
register the same reservations before bootmem initialization, and query only
after successful initialization.
