Offline SMMSTORE variable modification tool `C`

`smmstoretool backup.smmstore migrate-cdk2-padded canonical.smmstore` converts
a raw SMMSTORE-region backup known to have been written by CDK2's legacy
four-byte-aligned variable-name layout. The output must not exist. The input
is opened read-only; no store is initialized or repaired.

Migration requires clean FTW state and complete records. Canonical or ambiguous
inputs, malformed records and dirty tails are rejected. Record order, states,
names, payloads and authentication headers are preserved; only name padding and
record offsets change. FV/store headers and FTW working/spare regions remain
unchanged. This command does not use the ordinary variable editing serializer,
which does not retain authentication timestamps.

Keep the original backup and record `sha256sum` for both files before programming
the converted region. Full ROM images must have their SMMSTORE region extracted
first. Conversion does not program hardware or establish power-loss safety for
the later programmer operation.
