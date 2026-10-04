Keep duplicate detection linear in the number of receipt rows using a separate
set of the same Path objects. Preserve the ordered return list, every SHA check,
lexical Path identity, malformed/empty/duplicate refusal and tool-alias rules.
No hash cache, receipt omission or deadline relaxation is introduced.

Root and a nonauthor worker reviewed both files before final gates. Fresh9 and
historical10 tests pass, including explicit structural scaling and real semantic
oppositions. The final author run verified every row of three unchanged real
receipts (1,637 / 25,087 / 35,735), with source/tool/input/config/recipe closures
unchanged. Largest receipt took1.85 seconds; this is a primitive receipt replay,
not full firmware admission. Root reran both suites and the real native receipt
on the exact ready commit before advancing the canonical source.

The exact prior ready653 fresh Core/ROM/seal did pass, but its complete build
took607.90 seconds, with quadratic receipt scans dominating verification. No
guest was launched under those scans. This signed linear successor keeps the
original180 guest budgets; fresh firmware and guest gates still follow. No
hardware or full-project sign-off is claimed.
