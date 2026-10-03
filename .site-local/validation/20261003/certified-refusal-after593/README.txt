Certified pre-mutation signature refusal source checkpoint (CDK2 PR 593)
======================================================================

Scope
-----
Signed head 3b022120d08dca81b8dfec0e3acf344679fe602b, parent checkpoint
f9201738089d13a4a1ba19998ce6e99938ad2bc3 (PR 592). The functional commit is
1deff29c7921166090b517d002d97fc29b0bfd4b; the additive final commit fixes
three HOST registration/recipe/ABI issues. No amended published history.

The private same-handle FMP CHECK outcome and exact Runtime aggregate allow
Core to certify only a clean, pre-mutation signature refusal. Actual loaded
owner, pinned span, publication/source/transaction/generation and input hash
are validated; CLOSE, exact request retirement and postchecks precede success.
Mixed or unproved discovery/cleanup, any SET, owner/digest drift and CLOSE or
retirement failure remain fatal. Local below-floor recovery and durable
CapsuleReport remain open. A console rejection is not update success.

This packet establishes source/HOST/normal compilation only. It contains no
native continuation result, hardware result or host-power-loss proof. The
new guest continuation fixture is separately owned by the root worker.

Actual runs
-----------
Author original selected invocation: Make 2 before compilation because the
new gate was absent from CDK2_CONFIG_TARGETS (named.log/time).
Author corrected 11847: Make 2, GNU 37.80 s, due a literal sanitizer comma
being split by GNU Make's call helper (named-corrected.log/time).
Author 47803: Make 2, GNU 61.12 s, due SysV va_list in the MS-ABI tuple mock
(named-final.log/time). No Core ELF was produced by that failed invocation.
These are preserved harness failures, not native runs.

Final author seven selected HOST targets 49760: actual 0, GNU 71.19 s,
user 66.52 s, system 6.09 s (named-ms-abi.log/time).
Separate author normal full Core 51398: actual 0, GNU 70.43 s,
user 94.88 s, system 19.84 s (core-only.log/time).
Independent final selected eight targets including full Core 11628:
actual 0, GNU 162.64 s, user 190.12 s, system 32.16 s
(peer/named-final.log/time). Separate independent certified script 2663:
actual 0 (peer/certified.log/time).

Both actual normal Core ELF files have SHA256
d3863ab002696c2622199262d96ff68cb38b9f01c62fe0c537799775b0e2dd76
and were compared byte-for-byte (actual cmp 0). Their actual generated
configuration has SystemFmp enabled, QEMU_TEST_FMP disabled and acceptance
profile disabled. Generated image/composition inventories and config/header
copies are included; ELF files are hash-only external references.

The actual Core fixture includes real allocator/image-transaction ownership
and the five real software-hash backend sources. Publication, transport and
Runtime outcomes in HOST fixtures remain modeled. O0/O2 positive fixtures
use ASan/UBSan; only ASan global registration is disabled for the Core TU to
avoid unrelated global boot roots, retaining heap/stack instrumentation.
Four exact Core guard-discard causes are O2 uninstrumented binaries, require
the intended libc assertion/status 134 without sanitizer output, and compare
the full inverse TU. Real CMS tests remain separate from modeled MM replies.
Earlier Core fixture link failures are retained in the three author logs.

Header rebuild proof
--------------------
After the final independent source reader released, the new outcome header's
mtime alone was touched. Content SHA stayed unchanged. The first direct-file
target Make invocation returned 0 with 'Nothing to be done'; it did not load
the supported configured recipes and is NOT a rebuild proof.
The supported named provider/Runtime/full-Core invocation 17693 then returned
0, GNU 39.17 s, and rebuilt provider O0, Runtime entry fixture and Core entry
object. Before/after recorded mtimes show all three advancing. The rebuilt
Core ELF remained d3863ab0. No source bytes changed for this proof.

Source and provenance
---------------------
source/signed-593-source.tar.gz contains exactly the 24 regular changed files
from the signed Git head. source/current-24.sha256 hashes the same bodies.
Independent final-before source/config manifests match the final signed
bodies. The earlier peer source-before manifest intentionally predates the
allowed MS-ABI test fix and is historical, not asserted to match the final
fixture. No pre-run author manifest is invented. Included current rechecks
are explicitly post-run checks. The raw recorded run statuses were actually
reaped by the workers; time/log text alone is not a process-status oracle.

No binaries, ROMs, pflash, disks, variable stores, keys or private signing
material are included. check-receipts.py is read-only: it checks source archive
against Git, current final manifests, and the two external actual ELF hashes
and byte equality. SHA256SUMS covers every regular packet file except itself.
