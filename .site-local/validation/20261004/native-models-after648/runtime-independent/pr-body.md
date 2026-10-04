Use native standard C types for private runtime entry relocation/range/list bookkeeping and matching test arithmetic. Preserve installed EFI/MS ABI callbacks, table/wire layouts, unsigned adjustment and existing relocation/error behavior.

Root and two non-author workers reviewed the source. Eight existing model/entry ASAN/UBSAN executions pass at O0/O2 in genuinely resolved default and strict profiles. Both complete production entry objects match their exact baseline byte-for-byte. Compiler include closures, configuration, source/tool/input and executable hashes remain unchanged. Root independently replays the eight existing executables and two object comparisons; no independent compilation is claimed.

Existing entry coverage includes misaligned DIR64/ABSOLUTE, changed-fixup preservation, map snapshot and transition/cleanup. This patch does not claim separate HIGHLOW, malformed relocation or list-bound runtime cases. Two precompile harness failures remain preserved separately. No fresh whole Core, guest, hardware or full release signoff follows.

Author receipts: `/home/sean/runtime2-author-gates-configfix.LZnLzl`; independent replay: `/home/sean/runtime-root-independent.6uKGVO`. Linear successor to PR646.
