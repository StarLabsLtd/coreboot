# Fourteen obsolete review-state recovery snapshots

This is recovery evidence, not build, boot, hardware or code-quality validation.
The original worktrees were still present and unchanged when the archive was
created. Retirement requires a separate independent replay and approval.

Archive: `coreboot-retired-review-fourteen.tar.gz` (27 MiB), SHA-256:
`8ada804c8f9f8d7bda453ea57fe0178eec3fdeaad172e5c0d5746229e08c71c4`.
Its `manifest.json` names all fourteen original absolute paths and exact source
HEADs, index trees, full binary staged/unstaged patch hashes, retained refs and
public remotes. It records 41 repository states: fourteen main repositories
and 27 dirty or uniquely committed submodule/nested repositories. Eighteen
incremental Git bundles preserve commits not covered by the source's remote
refs. Thirty-five untracked and 57 relevant ignored files are retained, including
configuration, ROM/log receipts and source/helper inputs. Compiler objects and
other reproducible build products are not a full filesystem backup.

Identical generated recovery files were content-hardlinked before packing;
extraction restores every original artifact path and byte hash. No original
worktree was deduplicated or otherwise changed. The main repository's existing
branches/refs and active toolchain/MbedTLS donors remain retained separately.
Incremental bundles require the recorded public source prerequisites; they are
not standalone copies of complete repositories. Replaying a public source after
retirement depends on that remote or a retained object cache being available.

## Exact recovery exceptions without key content

Two cbootimage states have a staged deletion of their complete public source
tree at `80c499ebbe8a64aa2ee4222e6a8f342f465f6680`. Its upstream sample tree
includes a private-key fixture. No key bytes or deletion patch containing those
bytes are archived. Instead, `index-deletions.json` records the 53 public names,
the exact public HEAD/remotes, empty index tree
`4b825dc642cb6eb9a060e54bf8d69288fbee4904` and canonical deletion-diff hash.
Replay validates names against that exact HEAD, recreates the empty index and
checks the original full diff hash. Both states have no worktree/untracked or
uniquely committed changes.

Two clean nested `contrib/mbed-tls` checkouts are reference-only at public pin
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. Their stale development-only
remote refs misleadingly classified 32,727 public historical commits as unique.
The retained canonical donor's public master/3.6 refs contain this exact pin.
No history bundle containing public test-key fixtures is copied. Each capture
and replay instead fetched the exact public pin into a fresh scratch repository
from `https://github.com/Mbed-TLS/mbedtls.git`, then verified the original clean
index tree, empty staged/unstaged diffs and empty untracked/ignored lists.

All other patch, untracked/ignored and unique-commit blob bytes were checked for
private-key PEM and known GitHub/OpenAI token markers before artifact creation.
This marker preflight is not a claim of an exhaustive secret-classification tool.

## Replay

Optional Node diagnostic utilities are not firmware build dependencies.
From this evidence directory, use a new scratch directory:

```sh
recovery_scratch=$(mktemp -d /tmp/coreboot-review-replay.XXXXXX)
tar -xzf coreboot-retired-review-fourteen.tar.gz -C "$recovery_scratch"
node review_recovery_replay.js "$recovery_scratch"
```

Replay first verifies all archived artifact hashes, then restores each repository
in fresh scratch clones and verifies HEAD, exact index tree, canonical binary
staged/unstaged diff hashes, exact untracked names, content, executable modes and
symlink values, plus relevant ignored content. Originals are only read as optional
object caches; if absent, recorded public remotes plus incremental bundles are
used. The two clean MbedTLS exceptions always fetch their exact public pin.
Only internally created scratch clones are removed. No original source is reset,
cleaned or deleted by these utilities.

Author capture/restoration and separate full fourteen-state replay returned
success. The compressed archive was additionally extracted into
`/tmp/coreboot-fourteen-archive-replay.6g2VFj` for the same replay checks.
An independent reviewer's receipt and any later retirement are separate events.
