<!-- SPDX-License-Identifier: GPL-2.0-only -->

# SMM invocation fail-stop providers

`SMM_INVOCATION_FAIL_STOP_PLATFORM` requires exactly one strong, no-argument,
non-returning `smm_invocation_platform_fail_stop()` implementation in the SMM
link. The capability remains hidden and default-off. Supplying a provider does
not select the invocation entry, adapter, command registry, route or public
endpoint.

Two mutually exclusive mainboard selections supply the initial providers:

* `Q35_SMM_INVOCATION_FAIL_STOP_TEST` is a QEMU q35 test composition. It asks
  QEMU for a full CF9 reset, tries the distinct system-reset encoding if that
  returns, and terminally halts only if neither request resets the machine.
* `STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP` issues the platform-wide CF9
  system reset that remains available after the global-reset policy is locked.
  If that returns, it programs and starts the chipset TCO watchdog at its
  minimum supported countdown, then terminally halts while the independent
  watchdog remains armed.

The mainboard choice makes these providers mutually exclusive in a firmware
configuration. Link tests additionally reject both zero-provider and
multiple-provider compositions. There is no weak or generic fallback: a
selected consumer without exactly one platform provider must fail its link.

These providers deliberately use only state-free platform primitives. They
carry no callback or context pointer, expose no public selector, and make no
claim that protected state is scrubbed before reset. Their terminal halt is a
last resort after every configured platform-wide reset mechanism has been
attempted; it is not itself treated as a platform-wide reset.
