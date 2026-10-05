PR704 CFR-root and TPM-PPI authority — bounded HOST validation

Signed source 649e12efcc641f84d544d8453f8db1e4d11069ad, parent
a1cc4f88e3dfe8902a28042bbaa4af68f11c5266. Exact two-file binary delta
6ae67f67eaffd9c508efcb45362f7753aff8ba18439f7815ff12ec82fdf1bc35.
Original records remain at:
/home/sean/current693-fwui-qemu.PMINOC/cfr-ppi-host.K3XaGlLC

The aggregate importer uses existing singleton authority checks for CFR
root and TPM-PPI. Repeated valid CFR child forms remain permitted. Optional
malformed or unsupported UI data retains the existing boot-continuation
policy; this patch does not make such optional UI errors fatal. PPI error
propagation and optional-absence/value policies remain unchanged.

The existing aggregate model covers 42 cases, including 24 added CFR/PPI
cases, exact HOB bytes, ordered repeated children and zero padding. PPI
producer records are 16 bytes, including two padding bytes; the legacy
minimum local record is 14 bytes. Selected producer 7ee34bed emits one CFR
root and one optional PPI record. No claim about other producers or legacy
SMRAM/SMM/S3 singleton cardinality is made.

O0 and O2 final public-target records are raw zero, using C11, strict
warnings, short wchar and ASan/UBSan with no recovery and non-PIE linking.
Native-stage public target and final owned style/stable lint are raw zero.
Initial owned-style raw one is retained: a ternary spacing failure was
fixed by using a named case count, with no assertion/checker relaxation.
Initial positive runs precede that style-only change and remain separate.

The signed-parent handoff translation unit is retained literally. Actual
emitted public compile commands were mechanically replayed with only that
source/output substitution. Both initial and final old-source compiles are
zero; old-source model runs are one with 12 expected status/HOB assertion
failures and no sanitizer diagnostic. The final replay uses the same final
fixture as the positive runs. These failures are causal test evidence, not
candidate firmware failures. Retained .d files cover the last compilation
unit only, not a universal 18-translation-unit dependency closure.

Flat original records and three resolved config/prepared Kconfig/header/
command records are literal copies. Generated executable bodies remain in
the original directory, not this packet. This is bounded HOST and native
stage validation, not a fresh complete firmware build, QEMU run, hardware
result, universal source/tool/header closure or complete project sign-off.
