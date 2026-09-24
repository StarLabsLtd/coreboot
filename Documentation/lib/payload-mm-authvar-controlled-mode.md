# Payload-MM authenticated-variable controlled modes

`CONFIG_PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE` builds an EDK2-derived fail-closed policy
for `SecureBootEnable` and `CustomMode`. Exact GUID/name classification feeds
two hooks because EDK2 does not evaluate these decisions together: VarCheck
validates the fixed `NV|BS`, one-byte property before store lookup, while
AuthVariable requires trusted physical presence after existing-variable
runtime, attribute and authentication-protection checks.

Deletion is exempt from the property shape check but still requires physical
presence. The byte is deliberately not limited to zero or one: EDK2 accepts
every byte and interprets a mode as enabled only when its value is exactly one.
The native mode readers preserve that distinction; `VendorKeysNv` retains its
separate strict zero-or-one invariant.

Unlike EDK2 VarCheckLib, controlled-mode writes reject `APPEND_WRITE`. Masking
that bit would allow repeated one-byte requests to create a malformed
multi-byte control record. This is a deliberate fail-closed hardening; writes
must carry exactly `NV|BS`.

Unlike EDK2's physical-presence cleanup shortcut, noncanonical authenticated
controlled records are not deleted without authentication. Stored deprecated
counter-authentication records fail scanning, counter-auth SET remains
unsupported, and time authentication remains on the authenticated path;
canonical controlled records are `NV|BS`.

The SET preflight composes both hooks at their matching positions and bypasses
the blanket derived-key rejection only for these two controlled keys. The
stage remains dormant with the coordinator and publishes no endpoint.
Secure MOR, the remaining fixed VarCheck entries, and language-variable
mirroring are subsequent ordered gates.

Production selection is also blocked on boot initialization atomically
rewriting persistent `CustomMode` to zero, as EDK2 does on every boot. No
persisted value of one may be trusted across that boundary. Even within one
boot, CustomMode-based Secure Boot bypass remains conjunctive with trusted
physical presence sealed for the same coordinator invocation.

That same atomic initialization must create an absent `SecureBootEnable=1`
when PK places the machine in UserMode, matching EDK2, before deriving the
current boot's volatile `SecureBoot` value. Until then, the coordinator
deliberately fails closed when PK is present but `SecureBootEnable` is absent.

A successful future `SecureBootEnable` commit must also retain the current
boot's volatile `SecureBoot` value and mark persisted state for reconciliation
at the next boot initialization. Updating the one-byte record without this
lifecycle step would make the executor's sealed volatile projection disagree
with the store and poison the next transaction. This slice therefore proves
admission only: the production ordinary path has no physical-presence source
and remains fail closed, while the coordinator test path still rejects
ordinary mutation.
