# Payload-MM authenticated-variable signature databases

`PAYLOAD_MM_AUTHVAR_SIGNATURE_DB` is a dormant SMM library. It consumes only
immutable protected snapshots and exposes no provider, command, media access or
mutation authority.

Validation accepts exactly the twelve signature types in EDK2 26.09
`AuthService.c:mSupportSigItem`, requires an empty signature header, a non-empty
list and the exact fixed stride. Each X.509 entry is independently decoded as
one exact RSA DER certificate in the wiped bounded crypto arena. Native `PK` is
exactly one X.509 list containing exactly one entry.

Append filtering matches EDK2 `FilterSignatureList`: an append entry is removed
only when the current database contains the complete same `EFI_SIGNATURE_DATA`
(owner and payload) in a list with the same type and stride. It preserves list
and entry order, including duplicates within the append input. A retained list
copies its original header and has only `SignatureListSize` rewritten. Both
inputs are fully validated and the required size is computed before output is
written; output bytes and `output_size` remain untouched on every failure.
Filtering admits at most 64 X.509 entries per input, rejecting the limit before
certificate parsing, and at most 65,536 current-to-append candidate pairs per
filter pass. The sizing and emission passes therefore perform at most 131,072
comparisons in total, so hostile SMM work is bounded. Empty append/delete
payloads are handled by the later variable authority rather than this non-empty
database filter.
