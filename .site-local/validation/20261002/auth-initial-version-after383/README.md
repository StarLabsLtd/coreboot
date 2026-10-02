# Initial-version authentication after provider 382: bounded receipts

Signed authentication checkpoint
`2514b790675965b88c502d387b44464007f554ad` has seven paths and parent provider
`6b62fd5d7195e7a2313d286b4ac2d08298286cca`. Its separate test-only include-line
wrap is `c7e8aaf6c917c55467e4f4f272aea048c23d518f`. The six non-receiver bodies
match original reviewed `47cef7e7debfcd9c2974428b37d7a9fa4935f10e`; the receiver
change relative to provider 382 is solely the reviewed compiled firmware
current-version initializer. Two source archives reproduce the exact seven
checkpoint bodies and one final wrapped test script, with per-file hashes.
They are after-testing snapshots, not invented pre-build source manifests.

## Actual results

| Receipt | Reaped result | Scope |
| --- | --- | --- |
| Joined pre-wrap HOST aggregate 41408 | 1 | CMS/dependency, old policy and publication tests passed; patch style alone failed with a 353-column shell line |
| Final wrapped author aggregate 57504 | 0 | New strict CMS/dependency tests, old policy/CMS/composition, publication and patch style |
| Independent final wrapped script 88375 | 0 | O0/O2 actual CMS/dependencies and six exact targeted causal refusals |
| Original independent auth script 63687 | 0 | Original seven-body checkpoint, before provider restack |
| Original independent old-policy regression 64310 | 0 | Real CMS/composed tests and an actual older Q35 ROM board-input case, not the new public-trust ROMs |
| Joined baseline-version new-public-trust producer build 48535 | 0 | Genuine fresh enabled full graph |
| Joined newer-version new-public-trust producer build 11066 | 0 | Genuine fresh enabled full graph; not an OFF build |

Author final aggregate managed duration was 22.567 s. Its retained raw GNU
timings are 11.99 s for the new script and 9.42 s for the old policy script;
publication/style are part of that aggregate. The two build managed durations
were reported as 60.807 s and 60.407 s respectively. They are recorded author
tool-receipt facts, not derived from post-completion systemd properties. No
independent elapsed times were measured or invented. Reaped statuses come from
actual tool receipts; PASS lines alone are not substitutes.

The pre-wrap aggregate remains a failure, even though its functional parts
passed. `failures/` retains those individual functional logs and the actual
one-warning patch-style refusal. Wrapping the same ordered compiler arguments
does not change production, policy, ABI or calling convention. It does not
turn the original aggregate into a pass or establish a whole-project pass.

## Authentication change and HOST limits

Policy revision 2 copies the trusted initial current version from the genuine
compiled `efi_fw_info_get` result into protected installed policy. Actual owner
read, mandatory present-record geometry and full expected-record comparison
remain required. Only the refusal for an unset durable version-present flag
is removed: dependency comparison uses durable version when flagged, otherwise
the sealed initial current version. Existing durable-present and floor behavior
remain unchanged. No state write or durable-version-present claim is added.
No new generic nonzero-version or compiled-floor cap is imposed.

The HOST fixtures use actual CMS/MbedTLS, dependency, board and payload parsing
with explicitly modeled owner records and protection callbacks. They test cold
flag-zero, durable-present, dependencies, rogue signatures, floors, missing
record/read errors, drift/ABA and source-copy behavior. The modeled record is
not actual protected storage or native SMM authorization. Three exact source
mutation classes at O0 and O2 restore cold refusal, ignore durable version or
discard sealed baseline. All six require targeted assertions and status 134,
reject sanitizer-only failure, and reverse-reconstruct the full production TU.
The copied positive/mutant binaries, six raw assertion logs, modeled public
capsules/images/signatures, mutant sources and strict runner preserve those
bounded observations. The author and independent ten-input source-before
manifests still verify against the final frozen signed source.

The actual MbedTLS source gitlink is
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. HOST test certificates are independent
generated public fixtures, not asserted to be the producer's configured trust.
Private fixture keys are not copied; signer/key creation logs are excluded.

## Genuine joined producer and public trust facts

Both fresh producer builds select the real provider SERVICE, owner/auth policy
and buffers with 8 MiB TSEG. Neither is a SERVICE-disabled build. Their genuine
FW_INFO identity is GUID `00112233-4455-6677-8899-aabbccddeeff` and floor
`0x001a0009`; baseline current version is `0x001a0009`, newer version is
`0x001a000a`. Both full configs and normal configure/build logs are preserved.
The builds still contain the earlier fallback component, not a final capsule
update target or a native authenticated writer test.

| Public input/artifact | SHA256 |
| --- | --- |
| Configured public PEM | `676fa1fdbde4e77f55818ed4f28394a5fbfbeeac1fad9a803bc05b24b9633c7a` |
| Public DER, both built and extracted from each actual ROM | `89f101575013365d7e553cc3ba123ba2ff8a785c25a945fddc53052c06a73bec` |
| Baseline producer config | `affdbf37d1a94634ede4d162d325a30fe010036a3a8c6d566987b36b34cfde35` |
| Newer producer config | `1e0dc4f0fe7a9a5da96d26d9f090d111b3856fc5a1d0c88dde6e46eede8890c9` |
| Baseline ROM | `48114dbda96a7165339d94fccc00f3c1cdbe630e8bebeb5e7117853badc9cbc1` |
| Newer ROM | `4b3202302538de0930ba5980280763bc572706122f8251cc9d3ed1e914398d9b` |
| Matching cbfstool, both outputs | `2329a80e0691e751de79886482fc18cfd49866e79ab671ce73f24ec01ea6d0f9` |

Independent packaging readback used each matching cbfstool to extract the raw
`capsule/trust.der` from that actual ROM; it equals the configured public DER
and generated build DER byte-for-byte. This proves public trust packaging, not
that a capsule was admitted or installed. The configured PEM path is a public
input; its sibling signing private key is deliberately never archived.

## Contents and open scope

`public-producer-inputs.tar.zst` contains the two real ROM/tool/config sets,
public PEM/DER and their built/extracted public DER, with twelve inner hashes.
The recovery copies remain under
`/home/sean/auth383-packet-input-recovery.6OEnhm/inputs`. Public HOST outputs and
raw logs are finite selected files, not full scratch trees or duplicate Git
history. Existing evidence directories and historical results are unchanged.
There are no private keys, combined signer PEMs, key-generation logs, native
guest disks or live user/hardware data in this packet.

Actual provider endpoint/READ_INFO/CLOSE is separately qualified in
`provider-after382`; it is not new auth CHECK/SET evidence. This packet does not
prove native first-update admission, capsule apply, flash writer/readback/
restore, disk lifecycle, hardware authority or project completion. The actual
CHECK/SET fixture and authenticated update/restore remain open work.
