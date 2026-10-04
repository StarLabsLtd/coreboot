# Unexecuted TPM acceptance recipes

Source-only draft for root and an independent peer. No build, compiler, guest,
syntax test, or recipe execution has been performed. These recipes are not
released merely by existing on disk.

Root creates a fresh clean coreboot worktree at
`7ee34bed989c46913c3ee6672fb25e83227c3b6c`. Its `.config` and `build` must not
exist. Do not use the dirty `/home/sean/Documents/coreboot` tree. Root supplies
the final reviewed signed clean CDK2 HEAD after the pending xcompile correction,
and a clean checkout with populated exact pinned BearSSL/LVGL gitlinks. Vboot
must be the clean actual selected source at
`5c360ef458b0a013d8a6d47724bb0fffb5accbcf`. The existing clean vboot checkout
under `/home/sean/Documents/coreboot/3rdparty/vboot` can be passed separately.
Unselected producer gitlinks are recorded, not claimed as compiled inputs.
Both entrypoints require the supplied CDK2 HEAD to be full lowercase 40-hex,
clean selected producer/CDK2/vboot roots, and successful actual `git verify-commit`
for the owned producer/CDK2 HEADs, preserving individual signature logs/statuses.
The exact upstream vboot object has no signature header: its provenance is
hash-pinned to the producer's matching gitlink, not signed vendor verification.
Its actual commit object and complete clean tracked regular/symlink sources are
recorded without replacing the pinned vendor object. The entrypoints refuse
any PYTHONOPTIMIZE environment variable; receipt.py also explicitly refuses
optimized execution so its assert-based checks cannot silently disappear.

After review and explicit release, root substitutes literal absolute paths and
the actual 40-character signed HEAD for these placeholders:

```sh
env -i PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin TMPDIR=/home/sean LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1 bash /home/sean/tpm-acceptance-recipes.5YqxUa/build.sh NEW_CLEAN_PRODUCER CLEAN_CDK2 NEXT_SIGNED_READY_HEAD /home/sean/Documents/coreboot/3rdparty/vboot NEW_BUILD_RECEIPTS
env -i PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin TMPDIR=/home/sean LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1 bash /home/sean/tpm-acceptance-recipes.5YqxUa/run-two.sh NEW_CLEAN_PRODUCER CLEAN_CDK2 NEXT_SIGNED_READY_HEAD /home/sean/Documents/coreboot/3rdparty/vboot PASSED_BUILD_RECEIPTS NEW_RUN_RECEIPTS
```

The bootstrap uses source-owned q35 DMA requester PAYLOAD_NONE profile and the
existing `util/scripts/config` late-FIFO symbol, then real olddefconfig/all.
Between olddefconfig and compiler-input capture, the producer explicitly builds
its existing public `build/xcompile` output target; olddefconfig does not create
that output on a fresh tree. This stage has actual argv/time/log/status receipts.
It does not require an existing payload. CDK2 uses the ordinary Q35 acceptance
legacy FVB/FTW variable owner, explicitly excluding the protected owner. Both
lanes require resolved strict-direct/native TCG2/replacement predicates. The
same freshly built/pair-verified ROM is appropriate for these two predicates;
no normal protected ROM, FV seed, disk mutation, host reset, or old TPM0 lane
substitution is used. Source-owned direct composition replay and extracted
post-insertion payload verification are supplied by build-cdk2-pair.sh.

Finite receipts bind actual invoked argv/environment, tracked regular/symlink
sources, selected vendor roots, source heads, host/compiler support tools and
32-bit libgcc, resolved profiles, xcompile, actual payload and producer ROM,
cbfstool, pair manifest and strict-direct inventory. Config before/after and
source/tool/recipe before/after comparisons refuse mutation. This is an honest
local development validation lane, not complete system-header attestation or
an adversarial resealing defense. Producer xcompile's actual selected command
family must match the supported recorded compiler inputs; otherwise stop.

Each independent run uses real canonical NVMe/USB fixtures, saves a checked
pre-launch original NVMe copy, records actual bash-expanded source-owned QEMU
launch trace on a separate fd, and derives QEMU status only from the run-one
manifest's actual wait result. The unmodified existing lane oracles are used.
The present lane additionally calls the existing freezer. Both lanes check
unchanged disk and no pflash changes outside existing mutable FMAP regions.
They also save the pinned original USB before launch and compare the actual
guest usb.raw byte-for-byte against both that original and the canonical fixture.
Failures/statuses and empty actual logs are preserved; no diagnostics are
fabricated. Outputs must be new directories and remain outside source trees.

Known remaining source-fix candidate: freeze-tpm-evidence.py currently requires
nonempty qemu.log. Real successful QEMU diagnostics can be present but empty.
This does not prove TPM failure; the recipe records the freezer failure and
does not exempt it. A bounded separately reviewed presence-versus-nonempty
diagnostic predicate fix is needed if the actual successful run is empty.
No such source edit is included here.
