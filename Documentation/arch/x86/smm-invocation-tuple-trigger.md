# Dormant SMM invocation tuple trigger

The private invocation transport uses one i386 ramstage leaf to issue its
software SMI. The leaf initializes both halves of the transaction sentinel,
places its low 32 bits in EAX and its high 32 bits in ECX, and places the fixed
APM control port in DX. It executes exactly one byte `OUT DX` and returns the
raw post-SMI ECX:EAX tuple as one logical 64-bit value.

The APMC command registry is the single authority for both the private command
and its logical sentinel. The transaction layer aliases that sentinel instead
of defining a second value. A compile-time assertion binds the current i386
all-ones encoding to the canonical protocol value.

EAX and ECX are read-write operands because their post-SMI values are the
transport response. DX is input-only: it carries the port to `OUT`, is not part
of that response, and neither the instruction nor the private handler contract
modifies RDX.

The trigger does not use the generic `call_smm()` interface because that
interface exposes only EAX as a returned value and assigns EBX to an argument
pointer. It does not use `outb()` because the generic I/O constraint permits an
immediate-port encoding, which would not match the protected Intel save-state
cause contract.

This primitive does not decide whether the returned value represents success,
retry, absence of an SMI handler, or a transport failure. In particular, an
unchanged sentinel is only raw transport output. A later sender must validate
the transaction response and own all abort or fail-stop policy. The trigger has
no timeout, retry, state, callback, callsite or public endpoint.
