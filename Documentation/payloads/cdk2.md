# CDK2 payload

CDK2 is built from `payloads/external/cdk2/cdk2` as a coreboot-owned external
payload. Coreboot passes its resolved configuration, toolchain and output
directory to CDK2. CDK2 remains responsible for selecting, compiling, linking
and testing its own sources.

Coreboot's lint targets intentionally do not inspect files behind a submodule
gitlink. `test-cdk2` therefore invokes CDK2's `what-jenkins-does` target
explicitly. `test-cdk2-qemu` additionally builds a Q35 ROM containing that
exact payload with both the normal 32-bit ramstage and the optional 64-bit
ramstage, then requires the shell startup and shutdown markers from each.

CDK2 has a 32-bit protected-mode entry stub because that is the standard x86
coreboot payload ABI. A 32-bit ramstage calls it directly using cdecl. A 64-bit
ramstage uses coreboot's protected-mode compatibility call, which presents the
same stack layout. CDK2 performs its own transition to long mode in both cases;
`USE_X86_64_SUPPORT` is therefore not a payload requirement.

The QEMU test leaves the linked ROM unchanged, initializes the SMM variable
store in a test copy with coreboot's `smmstoretool`, verifies that the store is
empty, and exposes that copy as writable pflash. This models the runtime flash
contract and avoids depending on a preformatted repository binary.

## Transitional retained firmware volume

Until CDK2's retained-module inventory reaches zero, an integration build must
set both `CDK2_RETAINED_FV` and `CDK2_RETAINED_FV_SHA256`. The build validates
the file, its digest and CDK2's inventory before using it. The firmware volume
is a reviewed build input; it is not stored or silently downloaded by coreboot.

The retained-FV Kconfig options and validation path must be removed when the
inventory reaches zero. At that point CDK2's coreboot build contract supplies
the complete payload using only its source tree and coreboot configuration.
