# Q35 cold DMA retirement/current prerequisite: bounded receipts

Checkpoint `91bb151ecc6879e70687e3dbce1f92629e7922fc` (parent
`dd78f90e20fe628b9d502950973a9483d00f31e5`) is a clean, signed eight-path
mechanism checkpoint: five production paths and three HOST fixture paths.
This packet preserves its actual build, HOST and cold-boot observations. It
does not prove a new private SMM caller, public E8 admission, platform endpoint,
runtime transaction, disk lifecycle, authenticated firmware installation or
hardware writer authority. The SMM helper compiled; its modeled HOST execution
is not actual SMM admission.

## Actual results

| Receipt | Actual result | Scope |
| --- | --- | --- |
| Author selected fresh full build, session 50513 | 0 | Normal selected graph; GNU raw 39.67 s |
| Author OFF fresh full build, session 29111 | 0 | Existing late allocation; broker buffers/auth policy/current capture OFF, broker/FMP owner retained; managed elapsed 49.039 s reported by author |
| Author two cold VMs, session 67429 | 0 | GNU raw 12.89 s; both pre-table retirement/current markers followed by actual FMP combined-state readback |
| Peer selected fresh full build, session 13962 | 0 | Independently owned output; elapsed time unmeasured |
| Peer two cold VMs, session 36039 | 0 | Independently owned output and matching ROM/tool; elapsed time unmeasured |
| Author final HOST script, session 37101 | 0 | GNU raw 15.75 s; O0/O2 modeled platform join and twelve targeted assertion-134 causal refusals |
| Peer exact HOST script, session 95627 | 0 | O0/O2 same actual join and twelve causal refusals; elapsed time unmeasured |

Reap/status evidence comes from the recorded tool receipts, not a log's PASS
line alone. Author managed durations are qualifications supplied with those
receipts; GNU timings are retained where present in raw logs. There is no
fabricated peer timing or post-completion systemd-property timing claim.

The two actual cold boots in each native run execute controller retirement and
the owned-table/current-deny check before coreboot table construction. Both
then read the real protected initial combined FMP state; the native runner
checks the second boot's media remains unchanged. This is not a version-present
or capsule-writer result. The normal observer is CDK2 signed checkpoint
`b09f1f02b4120cda7a1b34dfff19553cabd13fbc`, not a source mutation or guard bypass.

The selected author and peer ROMs compare byte-for-byte: SHA256
`d666a55fd55f3996b9303bbfaf33c77182346f25190c3d612c8205bd30004244`.
Their resolved producer config is SHA256
`f7e03a34d4fef9e58a052ba1c8b70b138ed794be63b6a316022028d45662fc34`.
The input manifests bind ROM, cbfstool and config/header identities without
including raw ROM or media in this public packet.

## HOST and source qualifications

The HOST fixtures use modeled PCI/MMIO, protected-resource and phase callbacks
to run the real current-join/controller/table/drain code. They are not firmware
or hardware authority. Six independently compiled guard-discard modes (ATS,
foreign root, PMR, torn owned memory, protected-resource range and phase before
reset) run at O0 and O2. Each requires the targeted assertion and exit 134,
rejects sanitizer-only failures, and reverse-reconstructs the complete source
TU for exact comparison. Strict ASAN/UBSAN flags and all fourteen source-input
hashes are recorded in the actual runner and artifacts.

`source/q35-current-91bb-eight-source.tar` captures the exact eight changed
signed paths. `source/helper-inputs-91bb.tar` captures their real helper/header
inputs at the same signed tree. These archives were made after testing. Each
changed signed blob was compared with the frozen tested working body; the
author and peer HOST pre-run manifests still verify. No pre-build manifest for
the five full-build production paths was recorded, and none is invented here.

Selected builds use the real owned MbedTLS gitlink `0beb`, the pinned vboot
source under `cdk2-portable-presence-pin/3rdparty/vboot`, genuine olddefconfig,
`UPDATED_SUBMODULES=1` and the ordinary full build. Peer full output is
`/home/sean/q35-cold-current-peer-full.khaqEM`; peer normal named native target
output is `/home/sean/q35-cold-current-peer-native.W4RMwz`, run `run.5Hrs2J`.
The existing `native-fmp-owner-cold-state-component-test` uses absolute owned
config/header paths and the matching new producer config/ROM/cbfstool, with no
READY bypass. Peer HOST artifacts are `/home/sean/q35-dma-cold.E92L4S`.

## Preserved failures and contents

The first selected build lacked its actual MbedTLS checkout; the next exposed
the nonexistent PCI lookup API, corrected to the real `pcidev_on_root` API.
Both failed logs remain under `failures/`. The initial native systemd launch
omitted its working directory and failed before a valid run. Earlier HOST
header/hlt/libc/type closure failures remain distinct from final successful
causal runs. No earlier failure is relabeled as a pass.

This additive packet contains finite raw logs, public configs/header, source
archives, the peer observer ELF/map and input hashes. It excludes private
keys, certificate-generation logs, raw ROM, pflash/store/NVMe images and full
scratch build trees. `SHA256SUMS` covers copied files; existing evidence packets
are not rewritten.
