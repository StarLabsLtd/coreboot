# Fresh consumer root native build

Root actually reaped the selected build command with exit0 at clean signed
CDK2 `301023dfacc71d562f4f70c60f943a15fa925204`, directly following PR457.
Runtime was 1m9.824s. The command was:

```sh
PATH=/home/sean/.local/bin:/home/sean/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
TMPDIR=/home/sean make -j4 COREBOOT_CONFIG= \
 CDK2_DEFCONFIG=/home/sean/Documents/.cdk2-worktrees/protected-variable-runtime-reviewed451/util/qemu/config/cdk2-q35-setup-acceptance.defconfig \
 CDK2_BUILD_DIR=build/qemu-fresh-consumer-root-301/cdk2 \
 native-coreboot-image native-lvgl-renderer-test native-protected-image-policy-consumer-test
```

The working directory was the clean root validation worktree above. The
retained resolved configuration and header have SHA256 respectively
`b01cc23d5abc7a07b1daae14cb758a381b4c6a4ec39d9551619eb3cb5d14eaeb` and
`4236bb2180e7fb61dee32ba9dc2e84458929d407b42f83a4e6d25c5c5486b292`.
The retained native ELF has SHA256
`6902b0904fd3a061ab921f6a097bd5509bc9802fca2c83a772f8c71818c0fdac`.

The actual checks pass the 36-record/eight-mutation direct composition
contract, ELF layout, coreboot host import, MTRR ownership mutations, pinned
LVGL source/configuration and form rendering, framebuffer restoration mutant,
splash-status ownership, and focused O0/O2 image-policy consumer sanitizers.
The selected profile retains the compatibility variable provider. The new
protected consumer is tested separately as a component, not installed through
entry by this native build. No ROM pairing, QEMU boot, protected-policy
activation, whole-suite/style completion or hardware result is claimed here.

The separate router-namespace directory also retains the actual root join at
PR457, which returned 0 after 53.908s with genuine Auth2/FTW producer artifacts,
coherent snapshot, crypto and generic Security2 denial mutation. That earlier
join does not exercise this newly added consumer. Model delivery is explicitly
qualified in its log.
