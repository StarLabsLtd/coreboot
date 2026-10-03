# Bounded normal-profile private AUTH2: older APPEND and replay

This finite packet records one actual Q35/TCG boot, not all-project completion.
Actual session 73762 succeeded at 7.8244191510020755 seconds within its original
180-second budget; guest exit status was 3. GNU time was 15.07 seconds including
the actual final-media O0/O2 sanitizer checks. No RESET was observed.

The new ordinary EFI MAIN executes real ExitBootServices and a nonidentity
virtual transition. In unenrolled SetupMode it creates the private variable
at timestamp second 7, rejects a wrong signer, accepts a signed timestamp-6
APPEND (call/signed attributes 0x67), then rejects timestamp-7 nonappend replay.
Final actual media has the exact appended value, stored attributes 0x27 and
timestamp 7, the independent ASCII-CN plus complete encoded-TBS signer binding
in certdb, zero certdb timestamp, and a clean FTW working queue. Both virgin
EMPTY and decoder-validated completed-history NONE queues are distinguished.

The original normal PR628 firmware is reused honestly: source d8e49af2ab0c,
Core 6c86f953340891eac7f847508d9a65c6c67f0ac30c56905b5ee824a08282d08d,
producer389 source 7ee34bed989c, ROM
e381f1268fbc56e15a261fc7238206f97229430df4c4752cca192aac9fd697ae.
Its generated configuration is protected-variable 1/SystemFmp 1, acceptance 0,
test-FMP 0, debug 0, timeout 0, architectural Security2 router 1 and
SecureBoot-build 0. Retaining that source-owned router is not a claim of a
no-op private AUTH2 verifier. No firmware/Core rebuild or relabeling occurred.
The actual app-only build has thirteen compiler-derived TUs: ten app/support,
the real relocation-marker C, and two HOST PE helpers; 151 unique input files
were hashed before/after. It built in 1.41 seconds (outer 6.69), independently
equal generated configuration, and supplies a new ordinary MAIN PE.

Tested source 796022ad0979 and ready PR630 6a990c61dd18 have exactly the same
seven path bodies. Ready630 inherits ready629; it does not claim a new630 Core.
Original signed 4df1b4f47b3e and f48e634a34cf sources remain separate archives.
Source/archive identities and preserved originals are listed in TSV maps.
The real-codec archive includes actual compiler-consumed producer headers,
not just five implementation C files. Manifests bind the actual recorded
source/config/recipe/tool inputs, not a complete execution-loader universe.

All 26,567 native input hashes match before/after. Strict saved FW_INFO9,
no capsule HOB, RAM-before-PCI/DISK/MAIN chronology, required variable/ESRT
drivers, no capsule CHECK/SET, thirteen ordered UART lines and no RESET were
checked. Latest validated pre-exit CBMEM snapshots are archived; they are not
described as fresh terminal memory captures. Actual flash bytes outside the
real 64KiB SMMSTORE and the disk baseline after MAIN staging stayed identical.
Those original comparisons require omitted media to replay, unlike the saved
CBMEM/table/UART checks.

Independent session44467 replayed saved bytes and rebuilt the real final-media
codec at O0/O2 with ASAN/leak+UBSAN: saved GNU0.04 and media GNU2.84, both actual0.
This is saved-only HOST replay, not another guest. Model/syntax/genuine-CMS
HOST gates are not private-policy/native/media execution claims. Initial
HOST runs forced the normal header and missed the real app include defect;
corrected runs test the actual include route without that forced include.

Preserved failures remain failures:

- First app precompile94558: actual1/4.43 seconds, missing real config inclusion.
- Second app38007: actual2/6.75 seconds; app compiled, shared PE relocation
  marker prerequisite was missing. The existing output/config registries fix it.
- First VM observer2398: actual1/11.33 seconds, observed guest markers and
  SHUTDOWN but QMP reset caused observer termination of the unreaped wrapper,
  recording status143 at 7.801870720002626 seconds. It is not reclassified.
- Initial author and root terminal-HOST attempts: actual1, AST traversal order
  assertion. Sorting the same handlers by source line fixes only that assertion.
  Corrected seventeen-case author/root runs are actual0.

The retry accepts only transport EOF/OSError or PhysicalReadError caused by
OSError after validated metadata, waits at most min(2 seconds, original budget
remaining) for the natural wrapper exit, and still requires actual exit3 plus
all saved/media/hash checks. Malformed tables, checksum/rewind/motion, missing
metadata, bad exit, live wrapper or expired deadline remain fatal. No firmware
delay, variable seed, authority grant, audit waiver or private-policy change.

## Portable rechecking

Run `python3 check-receipts.py` from the extracted packet. It validates complete
finite manifest closure, archive membership, exact ready/tested seven-body
parity, raw outcomes/maps/profile/app-TU count, and executes the actual saved
oracle AST with the archived signed CBMEM reader against the two saved runs.
The successful saved phase/table/UART/guest result passes; failed run1 stays
rejected. It does not rebuild a guest or rerun media codecs from omitted images.
Optional `--originals --git` additionally compares preserved host originals
and actual commit/blob identities when those local resources are available.
The archived collector is a recorded preparation recipe, not an original
guest launch argv; original app/native commands and actual logs are retained.

Firmware/ROM, disk/store/media images, EFI/executable/object files, CMS/auth
input binaries and private keys are excluded. Only bounded public CBMEM
table/console byte snapshots are included as binary observations. Intermediate
cold persistence, reset/power cuts, enrolled key hierarchy, executable EDK2
differential, hardware and broader acceptance remain open.
