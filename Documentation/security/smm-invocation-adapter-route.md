# Intel SMM invocation adapter route provisioning

`SMM_INVOCATION_INTEL_ADAPTER_ROUTE` is a hidden, default-off, SMM-only
composition boundary. It obtains the save-state operations descriptor directly
from the canonical Intel adapter provider and passes that exact pointer to the
dormant authenticated-variable presence route provisioner. A caller cannot
inject a different save-state adapter through this API.

Provisioning is linear and must have one quiescent owner. Provider contention
returns `SMM_INVOCATION_TRY_RETRY` before the route is called. Every other
provider error is returned unchanged. A route provisioning error becomes
`SMM_INVOCATION_TRY_ERROR`; the route retains its existing pre-ownership retry
and post-ownership poison or fail-stop semantics.

The route may retain protected copies of the descriptor for the provider
lifetime. This does not authorize a callback invocation. Only the future sole
statically selected protected handler may invoke a retained copy, and only
between one successful provider arm and its matching retire. Calls outside
that interval are forbidden even though the adapter also rejects them without
success. Route provisioning itself neither arms nor retires the provider and
does not classify a cause, dispatch a command, enter an invocation, release a
lock, consume EOS or install a handler.

The production i386 wrapper has an exact 96-byte `dynamic,bounded` stack frame
because it forwards the existing route provisioner's cdecl argument list. Its
linked graph has exactly two direct children and no indirect edge. The provider
and route-session child graphs remain independently resolved, and their actual
maximum plus the wrapper frame must fit the selected 16 KiB SMM stack.
