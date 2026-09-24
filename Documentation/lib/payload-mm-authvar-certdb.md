# Payload-MM private authenticated-variable certdb codec

`PAYLOAD_MM_AUTHVAR_CERTDB` is a dormant, allocation-free SMM library. It
validates and composes only the packed EDK2 26.09 persistent `certdb` value. It
performs no certificate parsing, hashing, signature verification, authority
decision, store mutation, media operation or endpoint publication.

The little-endian serialization starts with a 32-bit total size, including the
size field itself. Each following node contains a 16-byte vendor GUID, 32-bit
node size, UTF-16 code-unit count, binding size, a non-empty UTF-16LE variable
name without its trailing NUL, and opaque non-empty binding bytes. Nodes have
no padding or alignment. The exact empty database is `04 00 00 00`.

Every operation validates the complete stream before publishing a borrowed
binding or writing an output byte. This deliberately tightens EDK2's walker,
which can trust malformed sizes in a nonmatching node. Truncation, overflow,
zero-sized fields, noncanonical names, duplicate matches and trailing bytes
are malformed. Inputs and outputs are bounded, nonwrapping and disjoint. The
output and its size remain untouched on every failure.

ADD appends a new node and rejects an existing key. REMOVE compacts exactly one
existing node and leaves the four-byte empty database after the last removal.
There is no replace-binding operation: EDK2 preserves the original signer
binding while updating or appending an existing private variable. Binding data
remains opaque so stores containing the legacy full signer-certificate stack
remain readable; later cryptographic policy alone decides whether a binding is
a native SHA-256/384/512 digest or that legacy serialization.

The later fixed-role bundle must compose the target variable and its certdb
replacement into one whole-store candidate and commit both through the same
FTW transaction. This codec never reproduces EDK2's separate writes or its
boot-time orphan cleanup.
