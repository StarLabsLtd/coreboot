Reviewed SystemFmp private-type receipts after CDK2 PR567/568
==========================================================

Public receipt archive SHA256:
7a3971e38b1d178159123037f774385eccb05b38a46c4301b6ecc7aa203b30ed

The archive contains two independently reviewed README files, 77 hashed
public files, signed source snapshots, original HOST test receipts and
explicitly labelled derived disassemblies. Root verified all 77 hashes,
archive integrity and every regular archived source file against its
specified signed CDK2 revision. The independent reviewer also checked the
complete packet. No private keys, guest media or firmware ROMs are included.

Transport candidate b8baa4a3cb24d535b407781a349e49fddd12d99a changes
private scalar bookkeeping only. Actual encoded structures and callback
ABI stay unchanged. Parser/client/future-floor/production-closure gates
passed. Only the P0/P1 O2 whole objects match; O0 and Os differ with bool
normalization. Os text grows seven bytes. Ten actual objects are retained,
not twelve. Original failed comparisons and export failure qualifications
are preserved; no universal byte-parity or speed improvement is claimed.

Entry candidate 5d27c185e869f24097ee5d23b520ccc0b56e9832 changes private
types and inlines a single-use GUID comparison; its existing HOST fixture
changes coherently. Joined-parent gates passed. Six integer/address-only
generated comparison pairs match; full bool/final-body parity is not
claimed. Both exact GUID-comparison mutations hit their intended assertions
at O0/O2 and independent repetition passed. The RAM generator's separate
fixture entry remains unchanged; no manifest update was needed.

These receipts establish bounded supported-x64 HOST proofs, not normal
production native capsule installation, hardware or current whole-tree
regression completion. See both inner README files for original failed
attempts, configuration bindings and replay-path qualifications.
