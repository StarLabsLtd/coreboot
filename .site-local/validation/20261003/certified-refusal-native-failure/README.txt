Normal certified-refusal continuation: first native failures, 2026-10-03

These are failures, not guest continuation or completed lifecycle acceptance.
The root-owned actual sessions 87638 (wrong signer) and 11815 (one signed-byte
corruption) both reaped status 1. The unchanged 180-second gate expired after
the genuine warm RAM phase completed, followed by PLATFORM_TABLES failing
with EFI_COMPROMISED_DATA. Whole invocation elapsed times were 182.74 and
182.69 seconds. Each result records exactly one guest warm reset. The result
field refusal_observed is false because full continuation acceptance failed;
the saved CBMEM logs nevertheless contain the exact Core rejection receipt.

Source and inputs
-----------------
Fixture checkout: signed f1368e864ee5433356d8514d25eeca25b10b42d0, containing
signed feature f96fc1f870b13737bdc5884b083034a6267e5eea. The independently
compiled actual normal Core is the PR593 d3863ab002696c2622199262d96ff68cb38b9f01c62fe0c537799775b0e2dd76
ELF, not a newly compiled PR594 whole tree. Coreboot producer d960d256e2c3c14ff4aa46d6443ba20a2fab77f7
genuinely builds both firmware versions 9/A with that Core before signing.
Private QEMU test signing material remains outside this packet. The normal
profile disables QEMU_TEST_FMP and the QEMU acceptance profile.

Both actual input-before/input-after JSON files compare equal. Original app
compile manifests, native commands, saved observer/console snapshots, actual
result.json, original logs/time, producer recipes and resolved configs are
retained. No source or runner changes were made while either VM was running.
Original NVMe and pflash copies remain outside this finite packet.

Source-backed cause and next fix
-------------------------------
initialize_platform_tables rejects retained RAM selection, and the protected
linear-state validator also forbids RAM selection after CAPSULE_RAM. The
next reviewed change must consume that selection only after certified CLOSE,
exact request retirement and all postchecks. NONE disables disk processing;
it does not permit rerunning early capsule selection. No validator relaxation,
generic error recovery, signature success, SET operation or flash update is
inferred from these failed runs. Later runner early-failure detection is not
retroactively applied to these recorded 180-second failures.

Receipts archive excludes executables, objects, capsules, ROMs, disks,
variable stores, private certificates, keys and tokens.
