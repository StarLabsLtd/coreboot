Decoded native transport model receipts after CDK2 PR570
======================================================

Public archive SHA256:
c45271d73a078e4f32f4b8c89b33b43d645ec0be6c35bc451df6ebc446b80416

Root reviewed the complete inner README and proof sources, checked all 118
inner hashes and archive integrity, compared every regular signed baseline
and candidate source snapshot, and matched the archived independent logs
to its actual separate replay. The source change and original proof also
have author/independent acceptance.

One header replaces 26 native integer/address/size fields and eight byte
storage flags. Wire structures, EFI_GUID and public callbacks stay intact.
Byte flags remain uint8_t rather than _Bool; every 0..255 representation is
preserved. Supported x64 proof is not generic language-alias or 32-bit ABI
equivalence. All 30 complete consuming-object pairs match under real P0/P1
headers at O0/O2/Os, including ordinary Core entry.

Six produced endpoint/INFO/layout/digest comparisons match, including
padding. Endpoint import and full endpoint SHA are actual code; READ_INFO
transport in the HOST fixture is explicitly modeled. Root independently
repeated these comparisons and every byte-flag value. Focused named and
parser/client gates pass, not native MM installation or hardware acceptance.

The source-frozen manifest was recorded after the first named gate, not
before it. Original replay scripts need private path adaptation; pinned
BearSSL remains an independently verified input. See the complete inner
README for actual configuration identities and copy-attempt qualifications.
No private keys, ROMs or guest media are in this packet.
