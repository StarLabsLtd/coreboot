SystemFmp provider private bookkeeping: final-parent focused proof

Ready CDK2 PR563, signed commit 838733db0b54dd58a448b2038b91a0c75245d9e9,
parent bc315c03f650858eccc3d5e3d44d9d942ca5b039 (shared ABI after MM route561).
Exactly one provider.c changes. Its complete body equals the independently
reviewed/tested WIP751006259260c9b4541d9b4d89e93e267c0a1414. Final-parent
source equality and actual shared-ABI/public-header parity were independently
reviewed and accepted before publication.

Root actual session88488 returned 0 for fresh genuine-default targets
native-system-fmp-provider-test and native-system-fmp-session-test, including
O0/O2/sanitizer variants. GNU elapsed 7.84 seconds. Before/after three-input
SHA checks returned 0; copied config/header are the actual generated caller
inputs. File checkpatch returned 0 with 0 errors, 0 warnings, 320 lines.

This proves bounded HOST provider/session closure on the actual linear parent,
not native SystemFmp mutation, a complete latest tree pass or hardware sign-off.
signed-source.tar retains exact signed public includes, provider and its test;
use the full published Git checkout and normal Make targets to reconstruct.
No firmware, guest media, generated signer key or secret is included.
