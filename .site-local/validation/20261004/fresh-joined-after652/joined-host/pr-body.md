Three separately reviewed, signed test/harness commits:

- Cover HIGHLOW relocation, changed relocation values and malformed runtime transition inputs without changing runtime production code.
- Supply the actual Linux-reset mode to the UI-capture readiness fixture, preserving its assertions.
- Add the dedicated strict DMA-safe TPM-absent acceptance lane, with exact diagnostic and CBMEM/Linux inspection ordering checks. Existing lanes and DMA guards remain unchanged.

Root and a nonauthor worker reviewed all seven files and the joined commit bodies. The actual joined HOST run passed the four TPM/acceptance selftests, capture readiness, and fresh/historical admission, setup-controller and Linux-reset models. Source, tools, recipes and HEAD remained unchanged. Earlier failed attempts remain preserved and are not passing evidence.

The runtime author additionally ran eight default/strict O0/O2 ASAN/UBSAN model/entry checks; root independently replayed those executables. This PR does not claim a new firmware build, live TPM/PCR validation, hardware validation or project completion. Linear successor to PR651.
